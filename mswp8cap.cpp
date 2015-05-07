/*
mswp8cap.cpp

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


#include "mediastreamer2/mscommon.h"
#include "mediastreamer2/msticker.h"
#include "mediastreamer2/msvideo.h"
#include "mswp8cap.h"

using namespace Microsoft::WRL;
using namespace Windows::Foundation;
using namespace Windows::Phone::Media::Capture;
using namespace mswp8vid;


static const float defaultFps = 15.0f;
static const int defaultBitrate = 384000;


bool MSWP8Cap::smInstantiated = false;


#define MS_H264_CONF(required_bitrate, bitrate_limit, resolution, fps, ncpus) \
	{ required_bitrate, bitrate_limit, { MS_VIDEO_SIZE_ ## resolution ## _W, MS_VIDEO_SIZE_ ## resolution ## _H }, fps, ncpus, NULL }

static const MSVideoConfiguration h264_conf_list[] = {
	MS_H264_CONF( 300000, 500000,   VGA, 12, 1),
	MS_H264_CONF( 170000, 300000,  QVGA, 12, 1),
	MS_H264_CONF( 120000,  170000, QVGA,  8, 1),
	MS_H264_CONF(      0,  120000, QVGA,  5 ,1)
};


MSWP8Cap::MSWP8Cap()
	: mIsInitialized(false), mIsActivated(false), mIsStarted(false),
	mRfc3984Packer(nullptr), mPackerMode(1), mStartTime(0), mSamplesCount(0), mBitrate(defaultBitrate),
	mCameraSensorRotation(0), mDeviceOrientation(0), mPixFmt(MS_YUV420P), mCameraLocation(CameraSensorLocation::Front),
	mVideoDevice(nullptr)
{
	if (smInstantiated) {
		ms_error("[MSWP8Cap] An video capture filter is already instantiated. A second one can not be created.");
		return;
	}

	ms_mutex_init(&mMutex, NULL);
	ms_queue_init(&mSampleToSendQueue);
	ms_queue_init(&mSampleToFreeQueue);
	mVConf = ms_video_find_best_configuration_for_bitrate(h264_conf_list, mBitrate, ms_get_cpu_count());

	mActivationCompleted = CreateEventEx(NULL, L"Local\\MSWP8CapActivation", 0, EVENT_ALL_ACCESS);
	if (!mActivationCompleted) {
		ms_error("[MSWP8Cap] Could not create activation event [%i]", GetLastError());
		return;
	}
	mStartCompleted = CreateEventEx(NULL, L"Local\\MSWP8CapStart", 0, EVENT_ALL_ACCESS);
	if (!mStartCompleted) {
		ms_error("[MSWP8Cap] Could not create start event [%i]", GetLastError());
		return;
	}
	mStopCompleted = CreateEventEx(NULL, L"Local\\MSWP8CapStop", 0, EVENT_ALL_ACCESS);
	if (!mStopCompleted) {
		ms_error("[MSWP8Cap] Could not create stop event [%i]", GetLastError());
		return;
	}

	mIsInitialized = true;
	smInstantiated = true;
}

MSWP8Cap::~MSWP8Cap()
{
	stop();
	deactivate();
	if (mStopCompleted) {
		CloseHandle(mStopCompleted);
		mStopCompleted = NULL;
	}
	if (mStartCompleted) {
		CloseHandle(mStartCompleted);
		mStartCompleted = NULL;
	}
	if (mActivationCompleted) {
		CloseHandle(mActivationCompleted);
		mActivationCompleted = NULL;
	}
	ms_mutex_destroy(&mMutex);
	smInstantiated = false;
}


int MSWP8Cap::activate()
{
	IAsyncOperation<AudioVideoCaptureDevice^> ^openOperation = nullptr;

	if (!mIsInitialized) return -1;

	mRfc3984Packer = rfc3984_new();
	rfc3984_set_mode(mRfc3984Packer, mPackerMode);
	rfc3984_enable_stap_a(mRfc3984Packer, FALSE);
	ms_video_starter_init(&mStarter);

	Size dimensions;
	dimensions.Width = (float)mVConf.vsize.width;
	dimensions.Height = (float)mVConf.vsize.height;
	openOperation = AudioVideoCaptureDevice::OpenForVideoOnlyAsync(mCameraLocation, dimensions);
	openOperation->Completed = ref new AsyncOperationCompletedHandler<AudioVideoCaptureDevice^>([this] (IAsyncOperation<AudioVideoCaptureDevice^> ^operation, Windows::Foundation::AsyncStatus status) {
		switch (status) {
		case Windows::Foundation::AsyncStatus::Completed:
		{
			IAudioVideoCaptureDeviceNative *pNativeDevice = nullptr;
			ms_message("[MSWP8Cap] OpenAsyncOperation completed");
			mVideoDevice = operation->GetResults();
			mCameraSensorRotation = (int)mVideoDevice->SensorRotationInDegrees;
			HRESULT hr = reinterpret_cast<IUnknown*>(mVideoDevice)->QueryInterface(__uuidof(IAudioVideoCaptureDeviceNative), (void**)&pNativeDevice);
			if ((pNativeDevice == nullptr) || FAILED(hr)) {
				ms_error("[MSWP8Cap] Unable to query interface IAudioVideoCaptureDeviceNative");
			} else {
				mNativeVideoDevice = pNativeDevice;
				configure();
				MakeAndInitialize<SampleSink>(&(this->mVideoSink), this);
				mNativeVideoDevice->SetVideoSampleSink(mVideoSink);
				applyVideoSize();
				applyFps();
				mIsActivated = true;
			}
		}
			break;
		case Windows::Foundation::AsyncStatus::Canceled:
			ms_warning("[MSWP8Cap] OpenAsyncOperation has been canceled");
			break;
		case Windows::Foundation::AsyncStatus::Error:
			ms_error("[MSWP8Cap] OpenAsyncOperation failed [0x%x]", operation->ErrorCode);
			break;
		default:
			break;
		}
		SetEvent(mActivationCompleted);
	});
	WaitForSingleObjectEx(mActivationCompleted, INFINITE, FALSE);
	return 0;
}

int MSWP8Cap::deactivate()
{
	if (mRfc3984Packer != nullptr) {
		rfc3984_destroy(mRfc3984Packer);
		mRfc3984Packer = nullptr;
	}
	mCameraSensorRotation = 0;
	mIsActivated = false;
	return 0;
}

void MSWP8Cap::start()
{
	if (!mIsStarted && mIsActivated) {
		mVideoDevice->StartRecordingToSinkAsync()->Completed = ref new AsyncActionCompletedHandler([this] (IAsyncAction ^asyncAction, Windows::Foundation::AsyncStatus status) {
			switch (status) {
			case Windows::Foundation::AsyncStatus::Completed:
				ms_message("[MSWP8Cap] StartRecordingToSinkAsync completed");
				break;
			case Windows::Foundation::AsyncStatus::Canceled:
				ms_error("[MSWP8Cap] StartRecordingToSinkAsync has been cancelled");
				break;
			case Windows::Foundation::AsyncStatus::Error:
				ms_error("[MSWP8Cap] StartRecordingToSinkAsync failed");
				break;
			default:
				break;
			}
			mIsStarted = true;
			SetEvent(mStartCompleted);
		});

#if 0
		printProperties();
#endif
		WaitForSingleObjectEx(mStartCompleted, INFINITE, FALSE);
	}
}

void MSWP8Cap::stop()
{
	mblk_t *m;

	if (!mIsStarted) return;

	if (mVideoDevice) {
		try {
			mVideoDevice->StopRecordingAsync()->Completed = ref new AsyncActionCompletedHandler([this] (IAsyncAction ^asyncAction, Windows::Foundation::AsyncStatus status) {
				switch (status) {
				case Windows::Foundation::AsyncStatus::Completed:
					ms_message("[MSWP8Cap] StopRecordingAsync completed");
					break;
				case Windows::Foundation::AsyncStatus::Canceled:
					ms_error("[MSWP8Cap] StopRecordingAsync has been cancelled");
					break;
				case Windows::Foundation::AsyncStatus::Error:
					ms_error("[MSWP8Cap] StopRecordingAsync failed");
					break;
				default:
					break;
				}
				mVideoDevice = nullptr;
				mIsStarted = false;
				SetEvent(mStopCompleted);
			});
		} catch(Platform::ObjectDisposedException^ e) {
			// A Platform::ObjectDisposedException can be raised if the app has had its access
			// to video revoked (most commonly when the app is going out of the foreground)
			ms_warning("[MSWP8Cap] Exception caught during StopRecordingAsync");
			mVideoDevice = nullptr;
			mIsStarted = false;
			SetEvent(mStopCompleted);
		}

		if (mNativeVideoDevice) {
			mNativeVideoDevice->Release();
			mNativeVideoDevice = nullptr;
		}
		if (mVideoSink) {
			mVideoSink->Release();
			mVideoSink = nullptr;
		}

		WaitForSingleObjectEx(mStopCompleted, INFINITE, FALSE);
	}

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

int MSWP8Cap::feed(MSFilter *f)
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

void MSWP8Cap::OnSampleAvailable(ULONGLONG hnsPresentationTime, ULONGLONG hnsSampleDuration, DWORD cbSample, BYTE* pSample)
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
		m = copy_ycbcrbiplanar_to_true_yuv_with_rotation(y, cbcr, 0, w, h, w, w, TRUE);
	}
	mblk_set_timestamp_info(m, timestamp);

	ms_mutex_lock(&mMutex);
	ms_queue_put(&mSampleToSendQueue, m);
	ms_mutex_unlock(&mMutex);
}


void MSWP8Cap::setCameraLocation(uint32 location)
{
	mCameraLocation = (CameraSensorLocation)location;
}

void MSWP8Cap::setFps(float fps)
{
	mVConf.fps = fps;
	setConfiguration(&mVConf);
}

void MSWP8Cap::setBitrate(int bitrate)
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

MSVideoSize MSWP8Cap::getVideoSize()
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

void MSWP8Cap::setVideoSize(MSVideoSize vs)
{
	MSVideoConfiguration best_vconf;
	best_vconf = ms_video_find_best_configuration_for_size(h264_conf_list, vs, ms_get_cpu_count());
	mVConf.vsize = vs;
	mVConf.fps = best_vconf.fps;
	mVConf.bitrate_limit = best_vconf.bitrate_limit;
	selectBestVideoSize();
	setConfiguration(&mVConf);
}

const MSVideoConfiguration * MSWP8Cap::getConfigurationList()
{
	return h264_conf_list;
}

void MSWP8Cap::setConfiguration(const MSVideoConfiguration *vconf)
{
	if (vconf != &mVConf) memcpy(&mVConf, vconf, sizeof(MSVideoConfiguration));

	if (mVConf.required_bitrate > mVConf.bitrate_limit)
		mVConf.required_bitrate = mVConf.bitrate_limit;

	if (mIsActivated) {
		applyVideoSize();
	}

	applyFps();
}

void MSWP8Cap::setDeviceOrientation(int degrees)
{
	mDeviceOrientation = degrees;
}

void MSWP8Cap::requestIdrFrame()
{
	if (mIsStarted) {
		if (mPixFmt == MS_H264) {
			Platform::Boolean value = true;
			mVideoDevice->SetProperty(KnownCameraAudioVideoProperties::H264RequestIdrFrame, value);
		}
	}
}


void MSWP8Cap::applyFps()
{
	// WARNING: Do not change the FPS while the capture is active. The SetProperty call does not return in this case!
	if (mVideoDevice && !mIsActivated) {
		uint32 value = (uint32)mVConf.fps;
		CameraCapturePropertyRange^ range = mVideoDevice->GetSupportedPropertyRange(mCameraLocation, KnownCameraAudioVideoProperties::VideoFrameRate);
		uint32 min = safe_cast<uint32>(range->Min);
		uint32 max = safe_cast<uint32>(range->Max);
		if (value < min) value = min;
		else if (value > max) value = max;
		mVideoDevice->SetProperty(KnownCameraAudioVideoProperties::VideoFrameRate, value);
		value = safe_cast<uint32>(mVideoDevice->GetProperty(KnownCameraAudioVideoProperties::VideoFrameRate));
	}
}

void MSWP8Cap::applyVideoSize()
{
	IAsyncAction^ action = nullptr;
	Size dimensions;
	dimensions.Width = (float)mVConf.vsize.width;
	dimensions.Height = (float)mVConf.vsize.height;
	action = mVideoDevice->SetCaptureResolutionAsync(dimensions);
	action->Completed = ref new AsyncActionCompletedHandler([this] (IAsyncAction^ action, Windows::Foundation::AsyncStatus status) {
		if (status == Windows::Foundation::AsyncStatus::Completed) {
			ms_debug("[MSWP8Cap] AsyncAction completed");
		}
		else if (status == Windows::Foundation::AsyncStatus::Canceled) {
			ms_warning("[MSWP8Cap] AsyncAction has been canceled");
		}
		else if (status == Windows::Foundation::AsyncStatus::Error) {
			ms_error("[MSWP8Cap] AsyncAction failed");
		}
	});
}

void MSWP8Cap::bitstreamToMsgb(uint8_t *encoded_buf, size_t size, MSQueue *nalus) {
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

bool MSWP8Cap::selectBestVideoSize()
{
	Collections::IVectorView<Size> ^availableSizes;
	Collections::IIterator<Size> ^availableSizesIterator;
	MSVideoSize requestedSize;
	MSVideoSize bestFoundSize;
	MSVideoSize minSize = { 65536, 65536 };

	bestFoundSize.width = bestFoundSize.height = 0;
	requestedSize.width = mVConf.vsize.width;
	requestedSize.height = mVConf.vsize.height;
	availableSizes = AudioVideoCaptureDevice::GetAvailableCaptureResolutions(mCameraLocation);
	availableSizesIterator = availableSizes->First();
	while (availableSizesIterator->HasCurrent) {
		MSVideoSize currentSize;
		currentSize.width = (int)availableSizesIterator->Current.Width;
		currentSize.height = (int)availableSizesIterator->Current.Height;
		ms_message("[MSWP8Cap] Seeing video size %ix%i", currentSize.width, currentSize.height);
		if (ms_video_size_greater_than(requestedSize, currentSize)) {
			if (ms_video_size_greater_than(currentSize, bestFoundSize)) {
				bestFoundSize = currentSize;
			}
		}
		if (ms_video_size_greater_than(minSize, currentSize)) {
			minSize = currentSize;
		}
		availableSizesIterator->MoveNext();
	}

	if ((bestFoundSize.width == 0) && bestFoundSize.height == 0) {
		ms_warning("[MSWP8Cap] This camera does not support our video size, use minimum size available");
		mVConf.vsize.width = minSize.width;
		mVConf.vsize.height = minSize.height;
		return false;
	}

	mVConf.vsize.width = bestFoundSize.width;
	mVConf.vsize.height = bestFoundSize.height;
	ms_message("[MSWP8Cap] Best video size is %ix%i", bestFoundSize.width, bestFoundSize.height);
	return true;
}

void MSWP8Cap::configure()
{
	bool unMuteAudio = true;
	bool supportH264BaselineProfile = false;
	Platform::Object^ boxedSensorRotation;
	Collections::IVectorView<Platform::Object^>^ values;
	Collections::IIterator<Platform::Object^> ^valuesIterator;

	// Configure the sensor rotation for the capture
	if (mCameraLocation == CameraSensorLocation::Front) {
		boxedSensorRotation = (360 - mCameraSensorRotation + 360 - mDeviceOrientation) % 360;
	} else if (mCameraLocation == CameraSensorLocation::Back) {
		boxedSensorRotation = (mCameraSensorRotation + mDeviceOrientation) % 360;
	} else {
		boxedSensorRotation = mDeviceOrientation;
	}
	mVideoDevice->SetProperty(KnownCameraGeneralProperties::EncodeWithOrientation, boxedSensorRotation);

	// Do not mute audio while recording video
	mVideoDevice->SetProperty(KnownCameraAudioVideoProperties::UnmuteAudioWhileRecording, unMuteAudio);

	if (mPixFmt == MS_H264) {
		// Use the H264 encoding format
		mVideoDevice->VideoEncodingFormat = CameraCaptureVideoFormat::H264;

		// Use the H264 baseline profile
		values = mVideoDevice->GetSupportedPropertyValues(mCameraLocation, KnownCameraAudioVideoProperties::H264EncodingProfile);
		valuesIterator = values->First();
		while (valuesIterator->HasCurrent) {
			H264EncoderProfile profile = (H264EncoderProfile)safe_cast<int>(valuesIterator->Current);
			if (profile == H264EncoderProfile::Baseline) {
				supportH264BaselineProfile = true;
			}
			valuesIterator->MoveNext();
		}
		if (supportH264BaselineProfile) {
			try {
				mVideoDevice->SetProperty(KnownCameraAudioVideoProperties::H264EncodingProfile, H264EncoderProfile::Baseline);
			}
			catch (Platform::COMException^ e) {
				if (e->HResult == E_NOTIMPL) {
					ms_warning("[MSWP8Cap] This device does not support setting the H264 encoding profile");
				}
			}
		}
		else {
			ms_warning("[MSWP8Cap] This camera does not support H264 baseline profile");
		}
		try {
			mVideoDevice->SetProperty(KnownCameraAudioVideoProperties::H264EncodingLevel, H264EncoderLevel::Level1_3);
		}
		catch (Platform::COMException^ e) {
			if (e->HResult == E_NOTIMPL) {
				ms_warning("[MSWP8Cap] This device does not support setting the H264 encoding level");
			}
		}
		try {
			mVideoDevice->SetProperty(KnownCameraAudioVideoProperties::H264QuantizationParameter, 32);
		}
		catch (Platform::COMException^ e) {
			if (e->HResult == E_NOTIMPL) {
				ms_warning("[MSWP8Cap] This device does not support setting the H264 quantization parameter");
			}
		}
	}
	else {
		mVideoDevice->VideoEncodingFormat = CameraCaptureVideoFormat::Nv12;
	}
}


void MSWP8Cap::detectCameras(MSWebCamManager *manager, MSWebCamDesc *desc)
{
	Collections::IVectorView<CameraSensorLocation> ^availableSensorLocations;
	Collections::IIterator<CameraSensorLocation> ^availableSensorLocationsIterator;
	MSList *camlist = NULL;
	int i;

	availableSensorLocations = AudioVideoCaptureDevice::AvailableSensorLocations;
	availableSensorLocationsIterator = availableSensorLocations->First();
	while (availableSensorLocationsIterator->HasCurrent) {
		char buffer[8];
		MSWebCam *cam = ms_web_cam_new(desc);
		memset(buffer, '\0', sizeof(buffer));
		wcstombs(buffer, availableSensorLocationsIterator->Current.ToString()->Data(), sizeof(buffer) - 1);
		cam->name = ms_strdup(buffer);
		cam->data = (void *)availableSensorLocationsIterator->Current;
		if (strcmp(cam->name, "Front") == 0) {
			camlist = ms_list_append(camlist, cam);
		} else {
			camlist = ms_list_prepend(camlist, cam);
		}
		availableSensorLocationsIterator->MoveNext();
	}

	if (ms_list_size(camlist) == 0) {
		ms_warning("[MSWP8Cap] This device does not have a camera");
	}
	for (i = 0; i < ms_list_size(camlist); i++) {
		ms_web_cam_manager_prepend_cam(manager, (MSWebCam *)ms_list_nth_data(camlist, i));
	}
	ms_list_free(camlist);
}

void MSWP8Cap::printProperties()
{
	Collections::IVectorView<Platform::Object^>^ values = mVideoDevice->GetSupportedPropertyValues(mCameraLocation, KnownCameraAudioVideoProperties::H264EnableKeyframes);
	ms_message("[MSWP8Cap] H264EnableKeyFrames supported values:");
	Collections::IIterator<Platform::Object^> ^valuesIterator = values->First();
	while (valuesIterator->HasCurrent) {
		bool enable = safe_cast<bool>(valuesIterator->Current);
		ms_message("[MSWP8Cap]   %s", enable ? "true" : "false");
		valuesIterator->MoveNext();
	}
	bool H264EnableKeyFrames = safe_cast<bool>(mVideoDevice->GetProperty(KnownCameraAudioVideoProperties::H264EnableKeyframes));
	ms_message("[MSWP8Cap] H264EnableKeyFrames value: %s", H264EnableKeyFrames ? "true" : "false");

	values = mVideoDevice->GetSupportedPropertyValues(mCameraLocation, KnownCameraAudioVideoProperties::H264EncodingLevel);
	ms_message("[MSWP8Cap] H264EncodingLevel supported values:");
	valuesIterator = values->First();
	while (valuesIterator->HasCurrent) {
		int value = safe_cast<int>(valuesIterator->Current);
		ms_message("[MSWP8Cap]   %d", value);
		valuesIterator->MoveNext();
	}
	auto H264EncodingLevel = mVideoDevice->GetProperty(KnownCameraAudioVideoProperties::H264EncodingLevel);
	if (H264EncodingLevel != nullptr) {
		int level = safe_cast<int>(H264EncodingLevel);
		ms_message("[MSWP8Cap] H264EncodingLevel value: %d", level);
	} else {
		ms_message("[MSWP8Cap] Cannot get H264EncodingLevel");
	}

	values = mVideoDevice->GetSupportedPropertyValues(mCameraLocation, KnownCameraAudioVideoProperties::H264EncodingProfile);
	ms_message("[MSWP8Cap] H264EncodingProfile supported values:");
	valuesIterator = values->First();
	while (valuesIterator->HasCurrent) {
		int value = safe_cast<int>(valuesIterator->Current);
		ms_message("[MSWP8Cap]   %d", value);
		valuesIterator->MoveNext();
	}
	auto H264EncodingProfile = mVideoDevice->GetProperty(KnownCameraAudioVideoProperties::H264EncodingProfile);
	if (H264EncodingProfile != nullptr) {
		int profile = safe_cast<int>(H264EncodingProfile);
		ms_message("[MSWP8Cap] H264EncodingProfile value: %u", profile);
	} else {
		ms_message("[MSWP8Cap] Cannot get H264EncodingProfile");
	}

	CameraCapturePropertyRange^ range = mVideoDevice->GetSupportedPropertyRange(mCameraLocation, KnownCameraAudioVideoProperties::H264QuantizationParameter);
	ms_message("[MSWP8Cap] H264QuantizationParameter range: %u-%u", safe_cast<uint32>(range->Min), safe_cast<uint32>(range->Max));
	auto H264QuantizationParameter = mVideoDevice->GetProperty(KnownCameraAudioVideoProperties::H264QuantizationParameter);
	if (H264QuantizationParameter != nullptr) {
		uint32 qp = safe_cast<uint32>(H264QuantizationParameter);
		ms_message("[MSWP8Cap] H264QuantizationParameter value: %u", qp);
	} else {
		ms_message("[MSWP8Cap] Cannot get H264QuantizationParameter");
	}

	range = mVideoDevice->GetSupportedPropertyRange(mCameraLocation, KnownCameraAudioVideoProperties::VideoFrameRate);
	ms_message("[MSWP8Cap] VideoFrameRate range: %u-%u", safe_cast<uint32>(range->Min), safe_cast<uint32>(range->Max));
	uint32 fps = safe_cast<uint32>(mVideoDevice->GetProperty(KnownCameraAudioVideoProperties::VideoFrameRate));
	ms_message("[MSWP8Cap] VideoFrameRate value: %u", fps);

	values = mVideoDevice->GetSupportedPropertyValues(mCameraLocation, KnownCameraAudioVideoProperties::VideoTorchMode);
	ms_message("[MSWP8Cap] VideoTorchMode supported values:");
	valuesIterator = values->First();
	while (valuesIterator->HasCurrent) {
		uint32 value = safe_cast<uint32>(valuesIterator->Current);
		ms_message("[MSWP8Cap]   %u", value);
		valuesIterator->MoveNext();
	}
	auto VideoTorchMode = mVideoDevice->GetProperty(KnownCameraAudioVideoProperties::VideoTorchMode);
	if (VideoTorchMode != nullptr) {
		uint32 mode = safe_cast<uint32>(VideoTorchMode);
		ms_message("[MSWP8Cap] VideoTorchMode value: %u", mode);
	} else {
		ms_message("[MSWP8Cap] Cannot get VideoTorchMode");
	}

	range = mVideoDevice->GetSupportedPropertyRange(mCameraLocation, KnownCameraAudioVideoProperties::VideoTorchPower);
	ms_message("[MSWP8Cap] VideoTorchPower range: %u-%u", safe_cast<uint32>(range->Min), safe_cast<uint32>(range->Max));
	uint32 power = safe_cast<uint32>(mVideoDevice->GetProperty(KnownCameraAudioVideoProperties::VideoTorchPower));
	ms_message("[MSWP8Cap] VideoTorchPower value: %u", power);
}
