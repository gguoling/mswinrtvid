/*
mswinrtcap.cpp

mediastreamer2 library - modular sound and video processing and streaming
Windows Audio Session API sound card plugin for mediastreamer2
Copyright (C) 2010-2015 Belledonne Communications, Grenoble, France

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


MSWinRTCapHelper::MSWinRTCapHelper() :
	mRotationKey({ 0xC380465D, 0x2271, 0x428C,{ 0x9B, 0x83, 0xEC, 0xEA, 0x3B, 0x4A, 0x85, 0xC1 } }),
	mDeviceOrientation(0), mAllocator(NULL)
{
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

	ms_mutex_init(&mMutex, NULL);
	mAllocator = ms_yuv_buf_allocator_new();
	ms_queue_init(&mSamplesQueue);
}

MSWinRTCapHelper::~MSWinRTCapHelper()
{
	if (mCapture.Get() != nullptr) {
		mCapture->Failed -= mMediaCaptureFailedEventRegistrationToken;
	}
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
}

void MSWinRTCapHelper::OnCaptureFailed(MediaCapture^ sender, MediaCaptureFailedEventArgs^ errorEventArgs)
{
	errorEventArgs->Message;
}

bool MSWinRTCapHelper::Initialize(Platform::String^ DeviceId)
{
	bool isInitialized = false;
	mCapture = ref new MediaCapture();
	mMediaCaptureFailedEventRegistrationToken = mCapture->Failed += ref new MediaCaptureFailedEventHandler(this, &MSWinRTCapHelper::OnCaptureFailed);
	MediaCaptureInitializationSettings^ initSettings = ref new MediaCaptureInitializationSettings();
	initSettings->MediaCategory = MediaCategory::Communications;
	initSettings->VideoDeviceId = DeviceId;
	initSettings->StreamingCaptureMode = StreamingCaptureMode::Video;
	IAsyncAction^ initAction = mCapture->InitializeAsync(initSettings);
	initAction->Completed = ref new AsyncActionCompletedHandler([this, &isInitialized](IAsyncAction^ asyncInfo, Windows::Foundation::AsyncStatus asyncStatus) {
		switch (asyncStatus) {
		case Windows::Foundation::AsyncStatus::Completed:
			ms_message("[MSWinRTCap] InitializeAsync completed");
			isInitialized = true;
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
	return isInitialized;
}

bool MSWinRTCapHelper::StartPreview(int DeviceOrientation)
{
	bool isStarted = false;
	mDeviceOrientation = DeviceOrientation;
	try {
		IAsyncAction^ previewAction = mCapture->StartPreviewAsync();
		previewAction->Completed = ref new AsyncActionCompletedHandler([this, &isStarted](IAsyncAction^ asyncAction, Windows::Foundation::AsyncStatus asyncStatus) {
			IAsyncAction^ previewPropertiesAction = nullptr;
			IMediaEncodingProperties^ props = nullptr;
			switch (asyncStatus) {
			case Windows::Foundation::AsyncStatus::Completed:
				ms_message("[MSWinRTCap] StartPreviewAsync completed");
				props = mCapture->VideoDeviceController->GetMediaStreamProperties(Capture::MediaStreamType::VideoPreview);
				props->Properties->Insert(mRotationKey, mDeviceOrientation);
				try {
					previewPropertiesAction = mCapture->SetEncodingPropertiesAsync(Capture::MediaStreamType::VideoPreview, props, nullptr);
					previewPropertiesAction->Completed = ref new AsyncActionCompletedHandler([this, &isStarted](IAsyncAction^ asyncAction, Windows::Foundation::AsyncStatus asyncStatus) {
						isStarted = true;
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
				} catch (Exception^ e) {
					ms_error("[MSWinRTCap] StartPreviewAsync exception 0x%x", e->HResult);
					// Apparently, sometimes the preview is not correctly stopped, try again...
					StopPreview();
					SetEvent(mPreviewStartCompleted);
				}
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
	}
	catch (Exception^ e) {
		ms_error("[MSWinRTCap] SetEncodingPropertiesAsync exception 0x%x", e->HResult);
		// Apparently, sometimes the preview is not correctly stopped, try again...
		StopPreview();
	}
	return isStarted;
}

void MSWinRTCapHelper::StopPreview()
{
	IAsyncAction^ action = mCapture->StopPreviewAsync();
	action->Completed = ref new AsyncActionCompletedHandler([this](IAsyncAction^ asyncAction, Windows::Foundation::AsyncStatus asyncStatus) {
		if (asyncStatus == Windows::Foundation::AsyncStatus::Completed) {
			ms_message("[MSWinRTCap] StopPreviewAsync completed");
		}
		else {
			ms_message("[MSWinRTCap] StopPreviewAsync failed");
		}
		SetEvent(mPreviewStopCompleted);
	});
	WaitForSingleObjectEx(mPreviewStopCompleted, INFINITE, FALSE);
}

bool MSWinRTCapHelper::StartCapture(MediaEncodingProfile^ EncodingProfile)
{
	bool isStarted = false;
	mEncodingProfile = EncodingProfile;
	MakeAndInitialize<MSWinRTMediaSink>(&mMediaSink, EncodingProfile->Video);
	static_cast<MSWinRTMediaSink *>(mMediaSink.Get())->SetCaptureFilter(this);
	ComPtr<IInspectable> spInspectable;
	HRESULT hr = mMediaSink.As(&spInspectable);
	if (FAILED(hr)) return false;
	IMediaExtension^ mediaExtension = safe_cast<IMediaExtension^>(reinterpret_cast<Object^>(spInspectable.Get()));
	IAsyncAction^ action = mCapture->StartRecordToCustomSinkAsync(EncodingProfile, mediaExtension);
	action->Completed = ref new AsyncActionCompletedHandler([this, &isStarted](IAsyncAction^ asyncAction, Windows::Foundation::AsyncStatus asyncStatus) {
		IAsyncAction^ capturePropertiesAction = nullptr;
		IMediaEncodingProperties^ props = nullptr;
		if (asyncStatus == Windows::Foundation::AsyncStatus::Completed) {
			ms_message("[MSWinRTCap] StartRecordToCustomSinkAsync completed");
			isStarted = true;
		} else {
			ms_error("[MSWinRTCap] StartRecordToCustomSinkAsync failed");
		}
		SetEvent(mStartCompleted);
	});
	WaitForSingleObjectEx(mStartCompleted, INFINITE, FALSE);
	return isStarted;
}

void MSWinRTCapHelper::StopCapture()
{
	static_cast<MSWinRTMediaSink *>(mMediaSink.Get())->SetCaptureFilter(nullptr);
	IAsyncAction^ action = mCapture->StopRecordAsync();
	action->Completed = ref new AsyncActionCompletedHandler([this](IAsyncAction^ asyncAction, Windows::Foundation::AsyncStatus asyncStatus) {
		static_cast<MSWinRTMediaSink *>(mMediaSink.Get())->Shutdown();
		mMediaSink = nullptr;
		if (asyncStatus == Windows::Foundation::AsyncStatus::Completed) {
			ms_message("[MSWinRTCap] StopRecordAsync completed");
		}
		else {
			ms_error("[MSWinRTCap] StopRecordAsync failed");
		}
		SetEvent(mStopCompleted);
	});
	WaitForSingleObjectEx(mStopCompleted, INFINITE, FALSE);
}

void MSWinRTCapHelper::OnSampleAvailable(BYTE *buf, DWORD bufLen, LONGLONG presentationTime)
{
	mblk_t *m;
	uint32_t timestamp = (uint32_t)((presentationTime / 10000LL) * 90LL);

	int w = mEncodingProfile->Video->Width;
	int h = mEncodingProfile->Video->Height;
	if ((mDeviceOrientation % 180) == 90) {
		w = mEncodingProfile->Video->Height;
		h = mEncodingProfile->Video->Width;
	}
	uint8_t *y = (uint8_t *)buf;
	uint8_t *cbcr = (uint8_t *)(buf + w * h);
	m = copy_ycbcrbiplanar_to_true_yuv_with_rotation(mAllocator, y, cbcr, mDeviceOrientation, w, h, mEncodingProfile->Video->Width, mEncodingProfile->Video->Width, TRUE);
	mblk_set_timestamp_info(m, timestamp);

	ms_mutex_lock(&mMutex);
	ms_queue_put(&mSamplesQueue, m);
	ms_mutex_unlock(&mMutex);
}

mblk_t * MSWinRTCapHelper::GetSample()
{
	ms_mutex_lock(&mMutex);
	mblk_t *m = ms_queue_get(&mSamplesQueue);
	ms_mutex_unlock(&mMutex);
	return m;
}

MSVideoSize MSWinRTCapHelper::SelectBestVideoSize(MSVideoSize vs)
{
	if ((CaptureDevice == nullptr) || (CaptureDevice->VideoDeviceController == nullptr)) {
		return vs;
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
		ms_warning("[MSWinRTCap] This camera does not support our video size, use requested size");
		return vs;
	}

	ms_message("[MSWinRTCap] Best video size is %ix%i", bestFoundSize.width, bestFoundSize.height);
	return bestFoundSize;
}


MSWinRTCap::MSWinRTCap()
	: mIsInitialized(false), mIsActivated(false), mIsStarted(false), mFps(15), mStartTime(0), mDeviceOrientation(0)
{
	if (smInstantiated) {
		ms_error("[MSWinRTCap] A video capture filter is already instantiated. A second one can not be created.");
		return;
	}

	mVideoSize.width = MS_VIDEO_SIZE_CIF_W;
	mVideoSize.height = MS_VIDEO_SIZE_CIF_H;
	mHelper = ref new MSWinRTCapHelper();
	smInstantiated = true;
}

MSWinRTCap::~MSWinRTCap()
{
	stop();
	deactivate();
	smInstantiated = false;
}


void MSWinRTCap::initialize()
{
	mIsInitialized = mHelper->Initialize(mDeviceId);
}

int MSWinRTCap::activate()
{
	if (!mIsInitialized) initialize();

	ms_average_fps_init(&mAvgFps, "[MSWinRTCap] fps=%f");
	configure();
	applyVideoSize();
	applyFps();
	mIsActivated = true;
	if (mIsActivated && (mCaptureElement != nullptr)) {
		if (mCaptureElement->Dispatcher->HasThreadAccess) {
			mCaptureElement->Source = mHelper->CaptureDevice.Get();
		} else {
			mCaptureElement->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([this]() {
				mCaptureElement->Source = mHelper->CaptureDevice.Get();
			}));
		}
	}
	return 0;
}

int MSWinRTCap::deactivate()
{
	//mHelper->StopPreview();
	mIsActivated = false;
	mIsInitialized = false;
	return 0;
}

void MSWinRTCap::start()
{
	if (!mIsStarted && mIsActivated) {
		/*mIsStarted = mHelper->StartPreview(mDeviceOrientation);
		if (mIsStarted)*/ mIsStarted = mHelper->StartCapture(mEncodingProfile);
	}
}

void MSWinRTCap::stop()
{
	mblk_t *m;

	if (!mIsStarted) return;
	mHelper->StopCapture();

	// Free the samples that have not been sent yet
	while ((m = mHelper->GetSample()) != NULL) {
		freemsg(m);
	}
	mIsStarted = false;
}

int MSWinRTCap::feed(MSFilter *f)
{
	mblk_t *im;

	// Send queued samples
	while ((im = mHelper->GetSample()) != NULL) {
		ms_queue_put(f->outputs[0], im);
		ms_average_fps_update(&mAvgFps, (uint32_t)f->ticker->time);
	}

	return 0;
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
	mVideoSize = mHelper->SelectBestVideoSize(vs);
}

void MSWinRTCap::setDeviceOrientation(int degrees)
{
	if (mFront) {
		mDeviceOrientation = degrees % 360;
	} else {
		mDeviceOrientation = (360 - degrees) % 360;
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
		MSVideoSize vs = mVideoSize;
		mEncodingProfile->Video->Width = vs.width;
		mEncodingProfile->Video->Height = vs.height;
		mEncodingProfile->Video->PixelAspectRatio->Numerator = vs.width;
		mEncodingProfile->Video->PixelAspectRatio->Denominator = vs.height;
	}
}

void MSWinRTCap::configure()
{
	mEncodingProfile = ref new MediaEncodingProfile();
	mEncodingProfile->Audio = nullptr;
	mEncodingProfile->Container = nullptr;
	MSVideoSize vs = mVideoSize;
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
	smCameras = NULL;
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
