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


static const float defaultFps = 15.0f;
static const int defaultBitrate = 384000;


bool MSWinRTCap::smInstantiated = false;


#define MS_H264_CONF(required_bitrate, bitrate_limit, resolution, fps, ncpus) \
	{ required_bitrate, bitrate_limit, { MS_VIDEO_SIZE_ ## resolution ## _W, MS_VIDEO_SIZE_ ## resolution ## _H }, fps, ncpus, NULL }

static const MSVideoConfiguration h264_conf_list[] = {
	MS_H264_CONF( 300000, 500000,   VGA, 12, 1),
	MS_H264_CONF( 170000, 300000,  QVGA, 12, 1),
	MS_H264_CONF( 120000,  170000, QVGA,  8, 1),
	MS_H264_CONF(      0,  120000, QVGA,  5 ,1)
};


MSWinRTCap::MSWinRTCap()
	: mIsInitialized(false), mIsActivated(false), mIsStarted(false),
	mRfc3984Packer(NULL), mAllocator(NULL), mPackerMode(1), mStartTime(0), mSamplesCount(0), mBitrate(defaultBitrate),
	mCameraSensorRotation(0), mDeviceOrientation(0), mPixFmt(MS_YUV420P)
{
	if (smInstantiated) {
		ms_error("[MSWinRTCap] An video capture filter is already instantiated. A second one can not be created.");
		return;
	}

	ms_mutex_init(&mMutex, NULL);
	ms_queue_init(&mSampleToSendQueue);
	ms_queue_init(&mSampleToFreeQueue);
	mAllocator = ms_yuv_buf_allocator_new();
	mVConf = ms_video_find_best_configuration_for_bitrate(h264_conf_list, mBitrate, ms_get_cpu_count());

	mActivationCompleted = CreateEventEx(NULL, L"Local\\MSWinRTCapActivation", 0, EVENT_ALL_ACCESS);
	if (!mActivationCompleted) {
		ms_error("[MSWinRTCap] Could not create activation event [%i]", GetLastError());
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

	mIsInitialized = true;
	smInstantiated = true;
}

MSWinRTCap::~MSWinRTCap()
{
	stop();
	deactivate();
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
	if (mActivationCompleted) {
		CloseHandle(mActivationCompleted);
		mActivationCompleted = NULL;
	}
	if (mAllocator != NULL) {
		ms_yuv_buf_allocator_free(mAllocator);
		mAllocator = NULL;
	}
	ms_mutex_destroy(&mMutex);
	smInstantiated = false;
}


int MSWinRTCap::activate()
{
	if (!mIsInitialized) return -1;

	mRfc3984Packer = rfc3984_new();
	rfc3984_set_mode(mRfc3984Packer, mPackerMode);
	rfc3984_enable_stap_a(mRfc3984Packer, FALSE);
	ms_video_starter_init(&mStarter);

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
			configure();
			applyVideoSize();
			applyFps();
			mIsActivated = true;
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
		SetEvent(mActivationCompleted);
	});

	WaitForSingleObjectEx(mActivationCompleted, INFINITE, FALSE);
	if (mIsActivated && (mCaptureElement != nullptr)) {
		mCaptureElement->Source = mCapture.Get();
		mMediaSink = ref new MSWinRTMediaSinkProxy();
	}
	return 0;
}

int MSWinRTCap::deactivate()
{
	if (mRfc3984Packer != nullptr) {
		rfc3984_destroy(mRfc3984Packer);
		mRfc3984Packer = nullptr;
	}
	mCameraSensorRotation = 0;
	mIsActivated = false;
	return 0;
}

void MSWinRTCap::start()
{
	if (!mIsStarted && mIsActivated) {
		IAsyncAction^ previewAction = mCapture->StartPreviewAsync();
		previewAction->Completed = ref new AsyncActionCompletedHandler([this](IAsyncAction^ asyncAction, Windows::Foundation::AsyncStatus asyncStatus) {
			switch (asyncStatus) {
			case Windows::Foundation::AsyncStatus::Completed:
				ms_message("[MSWinRTCap] StartPreviewAsync completed");
				mIsStarted = true;
				break;
			case Windows::Foundation::AsyncStatus::Canceled:
				ms_error("[MSWinRTCap] StartPreviewAsync has been cancelled");
				break;
			case Windows::Foundation::AsyncStatus::Error:
			{
				int res = asyncAction->ErrorCode.Value;
				ms_error("[MSWinRTCap] StartPreviewAsync failed [0x%x]", res);
			}
			break;
			default:
				break;
			}
			SetEvent(mPreviewStartCompleted);
		});
		WaitForSingleObjectEx(mPreviewStartCompleted, INFINITE, FALSE);

		MediaEncodingProfile^ mediaEncodingProfile = mEncodingProfile;
		Concurrency::create_task(mMediaSink->InitializeAsync(mediaEncodingProfile->Video)).then([this, mediaEncodingProfile](Windows::Media::IMediaExtension^ mediaExtension)
		{
			return Concurrency::create_task(mCapture->StartRecordToCustomSinkAsync(mediaEncodingProfile, mediaExtension)).then([this](Concurrency::task<void>& asyncInfo)
			{
				try {
					asyncInfo.get();
					//_recordingStarted = true;
				}
				catch (Platform::Exception^) {
					//CleanupSink();
					throw;
				}
			});
		});
	}
}

void MSWinRTCap::stop()
{
	mblk_t *m;

	if (!mIsStarted) return;

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
		if (mPixFmt == MS_H264) {
			MSQueue nalus;
			ms_queue_init(&nalus);
			uint32_t timestamp = mblk_get_timestamp_info(im);
			bitstreamToMsgb(im->b_rptr, im->b_wptr - im->b_rptr, &nalus);
			rfc3984_pack(mRfc3984Packer, &nalus, f->outputs[0], timestamp);
			ms_queue_put(&mSampleToFreeQueue, im);
		} else {
			ms_queue_put(f->outputs[0], im);
		}
		
		if (mSamplesCount == 0) {
			ms_video_starter_first_frame(&mStarter, f->ticker->ticks);
		} else if (ms_video_starter_need_i_frame(&mStarter, f->ticker->time)) {
			requestIdrFrame();
		}
		mSamplesCount++;
	}
	ms_mutex_unlock(&mMutex);

	return 0;
}


static void freeSample(void *sample)
{
	delete[] sample;
}

void MSWinRTCap::OnSampleAvailable(ULONGLONG hnsPresentationTime, ULONGLONG hnsSampleDuration, DWORD cbSample, BYTE* pSample)
{
	MS_UNUSED(hnsSampleDuration);
	mblk_t *m;
	uint32_t timestamp = (uint32_t)((hnsPresentationTime / 10000LL) * 90LL);

	if (mPixFmt == MS_H264) {
		BYTE* pMem = new BYTE[cbSample];
		memcpy(pMem, pSample, cbSample);
		m = esballoc(pMem, cbSample, 0, freeSample);
		m->b_wptr += cbSample;
	} else {
		int w = mVConf.vsize.width;
		int h = mVConf.vsize.height;
		if ((mDeviceOrientation % 180) == 0) {
			w = mVConf.vsize.height;
			h = mVConf.vsize.width;
		}
		uint8_t *y = (uint8_t *)pSample;
		uint8_t *cbcr = (uint8_t *)(pSample + w * h);
		m = copy_ycbcrbiplanar_to_true_yuv_with_rotation(mAllocator, y, cbcr, 0, w, h, w, w, TRUE);
	}
	mblk_set_timestamp_info(m, timestamp);

	ms_mutex_lock(&mMutex);
	ms_queue_put(&mSampleToSendQueue, m);
	ms_mutex_unlock(&mMutex);
}


void MSWinRTCap::setFps(float fps)
{
	mVConf.fps = fps;
	setConfiguration(&mVConf);
}

void MSWinRTCap::setBitrate(int bitrate)
{
	if (mIsActivated) {
		/* Encoding is already ongoing, do not change video size, only bitrate. */
		mVConf.required_bitrate = bitrate;
		setConfiguration(&mVConf);
	} else {
		MSVideoConfiguration best_vconf = ms_video_find_best_configuration_for_bitrate(h264_conf_list, bitrate, ms_get_cpu_count());
		setConfiguration(&best_vconf);
	}
}

MSVideoSize MSWinRTCap::getVideoSize()
{
	MSVideoSize vs;
	if ((mDeviceOrientation % 180) == 0) {
		vs.width = mVConf.vsize.height;
		vs.height = mVConf.vsize.width;
	} else {
		vs = mVConf.vsize;
	}
	return vs;
}

void MSWinRTCap::setVideoSize(MSVideoSize vs)
{
	MSVideoConfiguration best_vconf;
	best_vconf = ms_video_find_best_configuration_for_size(h264_conf_list, vs, ms_get_cpu_count());
	mVConf.vsize = vs;
	mVConf.fps = best_vconf.fps;
	mVConf.bitrate_limit = best_vconf.bitrate_limit;
	selectBestVideoSize();
	setConfiguration(&mVConf);
}

const MSVideoConfiguration * MSWinRTCap::getConfigurationList()
{
	return h264_conf_list;
}

void MSWinRTCap::setConfiguration(const MSVideoConfiguration *vconf)
{
	if (vconf != &mVConf) memcpy(&mVConf, vconf, sizeof(MSVideoConfiguration));

	if (mVConf.required_bitrate > mVConf.bitrate_limit)
		mVConf.required_bitrate = mVConf.bitrate_limit;

	if (mIsActivated) {
		applyVideoSize();
	}

	applyFps();
}

void MSWinRTCap::setDeviceOrientation(int degrees)
{
	mDeviceOrientation = degrees;
}

void MSWinRTCap::requestIdrFrame()
{
	if (mIsStarted) {
		if (mPixFmt == MS_H264) {
			Platform::Boolean value = true;
		}
	}
}


void MSWinRTCap::applyFps()
{
	if (mEncodingProfile != nullptr) {
		mEncodingProfile->Video->FrameRate->Numerator = (unsigned int)mVConf.fps;
		mEncodingProfile->Video->FrameRate->Denominator = 1;
	}
}

void MSWinRTCap::applyVideoSize()
{
	if (mEncodingProfile != nullptr) {
		mEncodingProfile->Video->Width = mVConf.vsize.width;
		mEncodingProfile->Video->Height = mVConf.vsize.height;
	}
}

void MSWinRTCap::bitstreamToMsgb(uint8_t *encoded_buf, size_t size, MSQueue *nalus) {
	size_t idx = 0;
	size_t frame_start_idx;
	mblk_t *m;
	int nal_len;

	if ((size < 5) || (encoded_buf[0] != 0) || (encoded_buf[1] != 0) || (encoded_buf[2] != 0) || (encoded_buf[3] != 1))
		return;

	frame_start_idx = idx = 4;
	while (frame_start_idx < size) {
		uint8_t *buf = encoded_buf + idx;
		while ((buf = (uint8_t *)memchr(buf, 0, encoded_buf + size - buf)) != NULL) {
			if (encoded_buf + size - buf < 4)
				break;
			if (*((int*)(buf)) == 0x01000000)
				break;
			++buf;
		}

		idx = buf ? buf - encoded_buf : size;
		nal_len = idx - frame_start_idx;
		if (nal_len > 0) {
			m = esballoc(&encoded_buf[frame_start_idx], nal_len, 0, NULL);
			m->b_wptr += nal_len;
			ms_queue_put(nalus, m);
		}
		frame_start_idx = idx = idx + 4;
	}
}

bool MSWinRTCap::selectBestVideoSize()
{
	MSVideoSize requestedSize;
	requestedSize.width = mVConf.vsize.width;
	requestedSize.height = mVConf.vsize.height;
	MediaCapture^ mediaCapture = ref new MediaCapture();
	if (MediaCapture::IsVideoProfileSupported(mDeviceId)) {
		Windows::Foundation::Collections::IVectorView<MediaCaptureVideoProfile^>^ profiles = mediaCapture->FindAllVideoProfiles(mDeviceId);
		for (unsigned int i = 0; i < profiles->Size; i++) {
			MediaCaptureVideoProfile^ profile = profiles->GetAt(i);
			Windows::Foundation::Collections::IVectorView<MediaCaptureVideoProfileMediaDescription^>^ descriptions = profile->SupportedRecordMediaDescription;
			for (unsigned int j = 0; j < descriptions->Size; j++) {
				MediaCaptureVideoProfileMediaDescription^ description = descriptions->GetAt(j);
			}
		}
	} else {
		ms_warning("[MSWinRTCap] Video profile is not supported by the camera, default to requested video size");
		mVConf.vsize.width = requestedSize.width;
		mVConf.vsize.height = requestedSize.height;
		return false;
	}
	return true;
}

void MSWinRTCap::configure()
{
	if (mPixFmt == MS_H264) {
		mEncodingProfile = MediaEncodingProfile::CreateMp4(VideoEncodingQuality::Auto);
		mEncodingProfile->Container = nullptr;
		mEncodingProfile->Video->ProfileId = H264ProfileIds::Baseline;
		//mEncodingProfile->Video->Width = mVConf.vsize.width;
		//mEncodingProfile->Video->Height = mVConf.vsize.height;
	} else {
		mEncodingProfile = ref new MediaEncodingProfile();
		mEncodingProfile->Container = nullptr;
		mEncodingProfile->Video = VideoEncodingProperties::CreateUncompressed(MediaEncodingSubtypes::Nv12, mVConf.vsize.width, mVConf.vsize.height);
	}
}

void MSWinRTCap::addCamera(MSWebCamManager *manager, MSWebCamDesc *desc, Platform::String^ DeviceId, Platform::String^ DeviceName)
{
	size_t returnlen;
	size_t inputlen = wcslen(DeviceName->Data()) + 1;
	char *name = (char *)ms_malloc(inputlen);
	if (wcstombs_s(&returnlen, name, inputlen, DeviceName->Data(), inputlen) != 0) {
		ms_error("MSWinRTCap: Cannot convert webcam name to multi-byte string.");
		goto error;
	}

	MSWebCam *cam = ms_web_cam_new(desc);
	cam->name = ms_strdup(name);
	const wchar_t *id = DeviceId->Data();
	WinRTWebcam *winrtwebcam = new WinRTWebcam();
	winrtwebcam->id_vector = new std::vector<wchar_t>(wcslen(id) + 1);
	wcscpy_s(&winrtwebcam->id_vector->front(), winrtwebcam->id_vector->size(), id);
	winrtwebcam->id = &winrtwebcam->id_vector->front();
	cam->data = winrtwebcam;
	ms_web_cam_manager_prepend_cam(manager, cam);

error:
	ms_free(name);
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
						DeviceInformation^ DeviceInfo = DeviceInfoCollection->GetAt(i);
						addCamera(manager, desc, DeviceInfo->Id, DeviceInfo->Name);
					}
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

void MSWinRTCap::printProperties()
{
}
