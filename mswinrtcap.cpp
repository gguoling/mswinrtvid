/*
mswinrtcap.cpp

mediastreamer2 library - modular sound and video processing and streaming
Windows Audio Session API sound card plugin for mediastreamer2
Copyright (C) 2010-2013 Belledonne Communications, Grenoble, France

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/


#include "mswinrtcap.h"

using namespace Microsoft::WRL;
using namespace Windows::Foundation;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Storage;
using namespace libmswinrtvid;


bool MSWinRTCap::smInstantiated = false;
MSList *MSWinRTCap::smCameras = NULL;


MSWinRTCap::MSWinRTCap()
	: mIsInitialized(false), mIsActivated(false), mIsStarted(false), mFps(15),
	mAllocator(NULL), mStartTime(0), mDeviceOrientation(0),
	mRotationKey({ 0xC380465D, 0x2271, 0x428C,{ 0x9B, 0x83, 0xEC, 0xEA, 0x3B, 0x4A, 0x85, 0xC1 } })
{
	if (smInstantiated) {
		ms_error("[MSWinRTCap] An video capture filter is already instantiated. A second one can not be created.");
		return;
	}

	mVideoSize.width = MS_VIDEO_SIZE_CIF_W;
	mVideoSize.height = MS_VIDEO_SIZE_CIF_H;

	ms_mutex_init(&mMutex, NULL);
	ms_queue_init(&mSampleToSendQueue);
	ms_queue_init(&mSampleToFreeQueue);
	mAllocator = ms_yuv_buf_allocator_new();

	mInitializationCompleted = CreateEventEx(NULL, L"Local\\MSWinRTCapInitialization", 0, EVENT_ALL_ACCESS);
	if (!mInitializationCompleted) {
		ms_error("[MSWinRTCap] Could not create initialization event [%i]", GetLastError());
		return;
	}
	mPreviewStartCompleted = CreateEventEx(NULL, L"Local\\MSWinRTCapPreviewStart", 0, EVENT_ALL_ACCESS);
	if (!mPreviewStartCompleted) {
		ms_error("[MSWinRTCap] Could not create preview start event [%i]", GetLastError());
		return;
	}
	mStartCompleted = CreateEventEx(NULL, L"Local\\MSWinRTCapStart", 0, EVENT_ALL_ACCESS);
	if (!mStartCompleted) {
		ms_error("[MSWinRTCap] Could not create start event [%i]", GetLastError());
		return;
	}
	mStopCompleted = CreateEventEx(NULL, L"Local\\MSWinRTCapStop", 0, EVENT_ALL_ACCESS);
	if (!mStopCompleted) {
		ms_error("[MSWinRTCap] Could not create stop event [%i]", GetLastError());
		return;
	}
	mPreviewStopCompleted = CreateEventEx(NULL, L"Local\\MSWinRTCapPreviewStop", 0, EVENT_ALL_ACCESS);
	if (!mPreviewStopCompleted) {
		ms_error("[MSWinRTCap] Could not create preview stop event [%i]", GetLastError());
		return;
	}

	smInstantiated = true;
}

MSWinRTCap::~MSWinRTCap()
{
	stop();
	deactivate();
	if (mPreviewStopCompleted) {
		CloseHandle(mPreviewStopCompleted);
		mPreviewStopCompleted = NULL;
	}
	if (mStopCompleted) {
		CloseHandle(mStopCompleted);
		mStopCompleted = NULL;
	}
	if (mPreviewStartCompleted) {
		CloseHandle(mPreviewStartCompleted);
		mPreviewStartCompleted = NULL;
	}
	if (mStartCompleted) {
		CloseHandle(mStartCompleted);
		mStartCompleted = NULL;
	}
	if (mInitializationCompleted) {
		CloseHandle(mInitializationCompleted);
		mInitializationCompleted = NULL;
	}
	if (mAllocator != NULL) {
		ms_yuv_buf_allocator_free(mAllocator);
		mAllocator = NULL;
	}
	ms_mutex_destroy(&mMutex);
	smInstantiated = false;
}


void MSWinRTCap::initialize()
{
	mCapture = ref new MediaCapture();
	MediaCaptureInitializationSettings^ initSettings = ref new MediaCaptureInitializationSettings();
	initSettings->MediaCategory = MediaCategory::Communications;
	initSettings->VideoDeviceId = mDeviceId;
	initSettings->StreamingCaptureMode = StreamingCaptureMode::Video;
	IAsyncAction^ initAction = mCapture->InitializeAsync(initSettings);
	initAction->Completed = ref new AsyncActionCompletedHandler([this](IAsyncAction^ asyncInfo, Windows::Foundation::AsyncStatus asyncStatus) {
		switch (asyncStatus) {
		case Windows::Foundation::AsyncStatus::Completed:
			ms_message("[MSWinRTCap] InitializeAsync completed");
			mIsInitialized = true;
			break;
		case Windows::Foundation::AsyncStatus::Canceled:
			ms_warning("[MSWinRTCap] InitializeAsync has been canceled");
			break;
		case Windows::Foundation::AsyncStatus::Error:
			ms_error("[MSWinRTCap] InitializeAsync failed [0x%x]", asyncInfo->ErrorCode);
			break;
		default:
			break;
		}
		SetEvent(mInitializationCompleted);
	});

	WaitForSingleObjectEx(mInitializationCompleted, INFINITE, FALSE);
}

int MSWinRTCap::activate()
{
	if (!mIsInitialized) return -1;

	ms_average_fps_init(&mAvgFps, "[MSWinRTCap] fps=%f");
	configure();
	applyVideoSize();
	applyFps();
	mIsActivated = true;
	if (mIsActivated && (mCaptureElement != nullptr)) {
		mCaptureElement->Source = mCapture.Get();
	}
	return 0;
}

int MSWinRTCap::deactivate()
{
	IAsyncAction^ action = mCapture->StopPreviewAsync();
	action->Completed = ref new AsyncActionCompletedHandler([this](IAsyncAction^ asyncAction, Windows::Foundation::AsyncStatus asyncStatus) {
		SetEvent(mPreviewStopCompleted);
	});
	WaitForSingleObjectEx(mPreviewStopCompleted, INFINITE, FALSE);
	mIsActivated = false;
	return 0;
}

void MSWinRTCap::start()
{
	if (!mIsStarted && mIsActivated) {
		IAsyncAction^ previewAction = mCapture->StartPreviewAsync();
		previewAction->Completed = ref new AsyncActionCompletedHandler([this](IAsyncAction^ asyncAction, Windows::Foundation::AsyncStatus asyncStatus) {
			IAsyncAction^ previewPropertiesAction = nullptr;
			IMediaEncodingProperties^ props = nullptr;
			switch (asyncStatus) {
			case Windows::Foundation::AsyncStatus::Completed:
				ms_message("[MSWinRTCap] StartPreviewAsync completed");
				props = mCapture->VideoDeviceController->GetMediaStreamProperties(Capture::MediaStreamType::VideoPreview);
				props->Properties->Insert(mRotationKey, mDeviceOrientation);
				previewPropertiesAction = mCapture->SetEncodingPropertiesAsync(Capture::MediaStreamType::VideoPreview, props, nullptr);
				previewPropertiesAction->Completed = ref new AsyncActionCompletedHandler([this](IAsyncAction^ asyncAction, Windows::Foundation::AsyncStatus asyncStatus) {
					mIsStarted = true;
					switch (asyncStatus) {
					case Windows::Foundation::AsyncStatus::Completed:
						ms_message("[MSWinRTCap] SetEncodingPropertiesAsync completed");
						break;
					case Windows::Foundation::AsyncStatus::Canceled:
						ms_error("[MSWinRTCap] SetEncodingPropertiesAsync has been cancelled");
						break;
					case Windows::Foundation::AsyncStatus::Error:
					{
						int res = asyncAction->ErrorCode.Value;
						ms_error("[MSWinRTCap] SetEncodingPropertiesAsync failed [0x%x]", res);
					}
					break;
					default:
						break;
					}
					SetEvent(mPreviewStartCompleted);
				});
				break;
			case Windows::Foundation::AsyncStatus::Canceled:
				ms_error("[MSWinRTCap] StartPreviewAsync has been cancelled");
				SetEvent(mPreviewStartCompleted);
				break;
			case Windows::Foundation::AsyncStatus::Error:
			{
				int res = asyncAction->ErrorCode.Value;
				ms_error("[MSWinRTCap] StartPreviewAsync failed [0x%x]", res);
				SetEvent(mPreviewStartCompleted);
			}
			break;
			default:
				SetEvent(mPreviewStartCompleted);
				break;
			}
		});
		WaitForSingleObjectEx(mPreviewStartCompleted, INFINITE, FALSE);

		MediaEncodingProfile^ mediaEncodingProfile = mEncodingProfile;
		mediaEncodingProfile->Video->Properties->Insert(mRotationKey, mDeviceOrientation);
		MakeAndInitialize<MSWinRTMediaSink>(&mMediaSink, mEncodingProfile->Video);
		static_cast<MSWinRTMediaSink *>(mMediaSink.Get())->SetCaptureFilter(this);
		ComPtr<IInspectable> spInspectable;
		HRESULT hr = mMediaSink.As(&spInspectable);
		if (FAILED(hr)) return;
		IMediaExtension^ mediaExtension = safe_cast<IMediaExtension^>(reinterpret_cast<Object^>(spInspectable.Get()));
		IAsyncAction^ action = mCapture->StartRecordToCustomSinkAsync(mEncodingProfile, mediaExtension);
		action->Completed = ref new AsyncActionCompletedHandler([this](IAsyncAction^ asyncAction, Windows::Foundation::AsyncStatus asyncStatus) {
			IAsyncAction^ capturePropertiesAction = nullptr;
			IMediaEncodingProperties^ props = nullptr;
			if (asyncStatus == Windows::Foundation::AsyncStatus::Completed)
				mIsStarted = true;
			else
				ms_error("[MSWinRTCap] StartRecordToCustomSinkAsync failed");
			SetEvent(mStartCompleted);
		});
		WaitForSingleObjectEx(mStartCompleted, INFINITE, FALSE);
	}
}

void MSWinRTCap::stop()
{
	mblk_t *m;

	if (!mIsStarted) return;

	static_cast<MSWinRTMediaSink *>(mMediaSink.Get())->SetCaptureFilter(NULL);
	IAsyncAction^ action = mCapture->StopRecordAsync();
	action->Completed = ref new AsyncActionCompletedHandler([this](IAsyncAction^ asyncAction, Windows::Foundation::AsyncStatus asyncStatus) {
		static_cast<MSWinRTMediaSink *>(mMediaSink.Get())->Shutdown();
		mMediaSink = nullptr;
		SetEvent(mStopCompleted);
	});
	WaitForSingleObjectEx(mStopCompleted, INFINITE, FALSE);

	ms_mutex_lock(&mMutex);
	// Free samples that have already been sent
	while ((m = ms_queue_get(&mSampleToFreeQueue)) != NULL) {
		freemsg(m);
	}
	// Free the samples that have not been sent yet
	while ((m = ms_queue_get(&mSampleToSendQueue)) != NULL) {
		freemsg(m);
	}
	ms_mutex_unlock(&mMutex);
	mIsStarted = false;
}

int MSWinRTCap::feed(MSFilter *f)
{
	mblk_t *im;

	ms_mutex_lock(&mMutex);
	// Free samples that have already been sent
	while ((im = ms_queue_get(&mSampleToFreeQueue)) != NULL) {
		freemsg(im);
	}
	// Send queued samples
	while ((im = ms_queue_get(&mSampleToSendQueue)) != NULL) {
		ms_queue_put(f->outputs[0], im);
		ms_average_fps_update(&mAvgFps, f->ticker->time);
	}
	ms_mutex_unlock(&mMutex);

	return 0;
}


void MSWinRTCap::OnSampleAvailable(BYTE *buf, DWORD bufLen, LONGLONG presentationTime)
{
	mblk_t *m;
	uint32_t timestamp = (uint32_t)((presentationTime / 10000LL) * 90LL);

	int w = mVideoSize.width;
	int h = mVideoSize.height;
	if ((mDeviceOrientation % 180) == 90) {
		w = mVideoSize.height;
		h = mVideoSize.width;
	}
	uint8_t *y = (uint8_t *)buf;
	uint8_t *cbcr = (uint8_t *)(buf + w * h);
	m = copy_ycbcrbiplanar_to_true_yuv_with_rotation(mAllocator, y, cbcr, 0, w, h, w, w, TRUE);
	mblk_set_timestamp_info(m, timestamp);

	ms_mutex_lock(&mMutex);
	ms_queue_put(&mSampleToSendQueue, m);
	ms_mutex_unlock(&mMutex);
}


void MSWinRTCap::setFps(float fps)
{
	mFps = fps;
	applyFps();
}

float MSWinRTCap::getAverageFps()
{
	return ms_average_fps_get(&mAvgFps);
}

MSVideoSize MSWinRTCap::getVideoSize()
{
	MSVideoSize vs;
	if ((mDeviceOrientation % 180) == 90) {
		vs.width = mVideoSize.height;
		vs.height = mVideoSize.width;
	} else {
		vs = mVideoSize;
	}
	return vs;
}

void MSWinRTCap::setVideoSize(MSVideoSize vs)
{
	selectBestVideoSize(vs);
	applyVideoSize();
}

void MSWinRTCap::selectBestVideoSize(MSVideoSize vs)
{
	if ((mCapture == nullptr) || (mCapture->VideoDeviceController == nullptr)) {
		mVideoSize = vs;
		return;
	}

	MSVideoSize requestedSize;
	MSVideoSize bestFoundSize;
	MSVideoSize minSize = { 65536, 65536 };
	bestFoundSize.width = bestFoundSize.height = 0;
	requestedSize.width = vs.width;
	requestedSize.height = vs.height;

	IVectorView<IMediaEncodingProperties^>^ props = mCapture->VideoDeviceController->GetAvailableMediaStreamProperties(MediaStreamType::VideoRecord);
	for (unsigned int i = 0; i < props->Size; i++) {
		IMediaEncodingProperties^ encodingProp = props->GetAt(i);
		if (encodingProp->Type == L"Video") {
			IVideoEncodingProperties^ videoProp = static_cast<IVideoEncodingProperties^>(encodingProp);
			String^ subtype = videoProp->Subtype;
			if ((subtype == L"NV12") || (subtype == L"Unknown")) {
				MSVideoSize currentSize;
				currentSize.width = (int)videoProp->Width;
				currentSize.height = (int)videoProp->Height;
				ms_message("[MSWinRTCap] Seeing video size %ix%i", currentSize.width, currentSize.height);
				if (ms_video_size_greater_than(requestedSize, currentSize)) {
					if (ms_video_size_greater_than(currentSize, bestFoundSize)) {
						bestFoundSize = currentSize;
					}
				}
				if (ms_video_size_greater_than(minSize, currentSize)) {
					minSize = currentSize;
				}
			}
		}
	}

	if ((bestFoundSize.width == 0) && bestFoundSize.height == 0) {
		ms_warning("[MSWinRTCap] This camera does not support our video size, use minimum size available");
		mVideoSize.width = minSize.width;
		mVideoSize.height = minSize.height;
		return;
	}

	mVideoSize.width = bestFoundSize.width;
	mVideoSize.height = bestFoundSize.height;
	ms_message("[MSWinRTCap] Best video size is %ix%i", bestFoundSize.width, bestFoundSize.height);
}

void MSWinRTCap::setDeviceOrientation(int degrees)
{
	if (mFront) {
		mDeviceOrientation = degrees;
	} else {
		mDeviceOrientation = 360 - degrees;
	}
}


void MSWinRTCap::applyFps()
{
	if (mEncodingProfile != nullptr) {
		mEncodingProfile->Video->FrameRate->Numerator = (unsigned int)mFps;
		mEncodingProfile->Video->FrameRate->Denominator = 1;
	}
}

void MSWinRTCap::applyVideoSize()
{
	if (mEncodingProfile != nullptr) {
		MSVideoSize vs = getVideoSize();
		mEncodingProfile->Video->Width = vs.width;
		mEncodingProfile->Video->Height = vs.height;
	}
}

void MSWinRTCap::configure()
{
	mEncodingProfile = ref new MediaEncodingProfile();
	mEncodingProfile->Audio = nullptr;
	mEncodingProfile->Container = nullptr;
	MSVideoSize vs = getVideoSize();
	mEncodingProfile->Video = VideoEncodingProperties::CreateUncompressed(MediaEncodingSubtypes::Nv12, vs.width, vs.height);
}

void MSWinRTCap::addCamera(MSWebCamManager *manager, MSWebCamDesc *desc, DeviceInformation^ DeviceInfo)
{
	size_t returnlen;
	size_t inputlen = wcslen(DeviceInfo->Name->Data()) + 1;
	char *name = (char *)ms_malloc(inputlen);
	if (wcstombs_s(&returnlen, name, inputlen, DeviceInfo->Name->Data(), inputlen) != 0) {
		ms_error("MSWinRTCap: Cannot convert webcam name to multi-byte string.");
		goto error;
	}

	MSWebCam *cam = ms_web_cam_new(desc);
	cam->name = ms_strdup(name);
	const wchar_t *id = DeviceInfo->Id->Data();
	WinRTWebcam *winrtwebcam = new WinRTWebcam();
	winrtwebcam->id_vector = new std::vector<wchar_t>(wcslen(id) + 1);
	wcscpy_s(&winrtwebcam->id_vector->front(), winrtwebcam->id_vector->size(), id);
	winrtwebcam->id = &winrtwebcam->id_vector->front();
	cam->data = winrtwebcam;
	if (DeviceInfo->EnclosureLocation != nullptr) {
		if (DeviceInfo->EnclosureLocation->Panel == Windows::Devices::Enumeration::Panel::Front) {
			winrtwebcam->external = FALSE;
			winrtwebcam->front = TRUE;
			smCameras = ms_list_append(smCameras, cam);
		} else {
			if (DeviceInfo->EnclosureLocation->Panel == Windows::Devices::Enumeration::Panel::Unknown) {
				winrtwebcam->external = TRUE;
				winrtwebcam->front = TRUE;
			} else {
				winrtwebcam->external = FALSE;
				winrtwebcam->front = FALSE;
			}
			smCameras = ms_list_prepend(smCameras, cam);
		}
	} else {
		winrtwebcam->external = TRUE;
		winrtwebcam->front = TRUE;
		smCameras = ms_list_prepend(smCameras, cam);
	}

error:
	ms_free(name);
}

void MSWinRTCap::registerCameras(MSWebCamManager *manager)
{
	if (ms_list_size(smCameras) == 0) {
		ms_warning("[MSWinRTCap] No camera detected!");
	}
	for (int i = 0; i < ms_list_size(smCameras); i++) {
		ms_web_cam_manager_prepend_cam(manager, (MSWebCam *)ms_list_nth_data(smCameras, i));
	}
	ms_list_free(smCameras);
}

void MSWinRTCap::detectCameras(MSWebCamManager *manager, MSWebCamDesc *desc)
{
	HANDLE eventCompleted = CreateEventEx(NULL, NULL, 0, EVENT_ALL_ACCESS);
	if (!eventCompleted) {
		ms_error("[MSWinRTCap] Could not create camera detection event [%i]", GetLastError());
		return;
	}
	IAsyncOperation<DeviceInformationCollection^>^ enumOperation = DeviceInformation::FindAllAsync(DeviceClass::VideoCapture);
	enumOperation->Completed = ref new AsyncOperationCompletedHandler<DeviceInformationCollection^>(
		[manager, desc, eventCompleted](IAsyncOperation<DeviceInformationCollection^>^ asyncOperation, Windows::Foundation::AsyncStatus asyncStatus) {
		if (asyncStatus == Windows::Foundation::AsyncStatus::Completed) {
			DeviceInformationCollection^ DeviceInfoCollection = asyncOperation->GetResults();
			if ((DeviceInfoCollection == nullptr) || (DeviceInfoCollection->Size == 0)) {
				ms_error("[MSWinRTCap] No webcam found");
			}
			else {
				try {
					for (unsigned int i = 0; i < DeviceInfoCollection->Size; i++) {
						addCamera(manager, desc, DeviceInfoCollection->GetAt(i));
					}
					registerCameras(manager);
				}
				catch (Platform::Exception^ e) {
					ms_error("[MSWinRTCap] Error of webcam detection");
				}
			}
		} else {
			ms_error("[MSWinRTCap] Cannot enumerate webcams");
		}
		SetEvent(eventCompleted);
	});
	WaitForSingleObjectEx(eventCompleted, INFINITE, FALSE);
}
