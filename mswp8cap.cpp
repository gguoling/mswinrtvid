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


MSWP8Cap::MSWP8Cap()
	: mIsInitialized(false), mIsActivated(false), mIsStarted(false),
	mRfc3984Packer(nullptr), mPackerMode(1), mStartTime(0), mSamplesCount(0), mFps(defaultFps), mBitrate(defaultBitrate),
	mCameraLocation(CameraSensorLocation::Front),
	mDimensions(MS_VIDEO_SIZE_CIF_W, MS_VIDEO_SIZE_CIF_H),
	mVideoDevice(nullptr)
{
	ms_mutex_init(&mMutex, NULL);
	ms_queue_init(&mSampleToSendQueue);
	ms_queue_init(&mSampleToFreeQueue);

	if (smInstantiated) {
		ms_error("[MSWP8Cap] An video capture filter is already instantiated. A second one can not be created.");
		return;
	}

	mStartCompleted = CreateEventEx(NULL, NULL, CREATE_EVENT_MANUAL_RESET, EVENT_ALL_ACCESS);
	if (!mStartCompleted) {
		ms_error("[MSWP8Cap] Could not create start event [%i]", GetLastError());
		return;
	}
	mStopCompleted = CreateEventEx(NULL, NULL, CREATE_EVENT_MANUAL_RESET, EVENT_ALL_ACCESS);
	if (!mStopCompleted) {
		ms_error("[MSWP8Cap] Could not create shutdown event [%i]", GetLastError());
		return;
	}

	mIsInitialized = true;
	smInstantiated = true;
}

MSWP8Cap::~MSWP8Cap()
{
	stop();
	ms_mutex_destroy(&mMutex);
	smInstantiated = false;
}


int MSWP8Cap::activate()
{
	IAsyncOperation<AudioVideoCaptureDevice^> ^openOperation = nullptr;

	if (!mIsInitialized) return -1;

	selectBestVideoSize();
	mRfc3984Packer = rfc3984_new();
	rfc3984_set_mode(mRfc3984Packer, mPackerMode);
	rfc3984_enable_stap_a(mRfc3984Packer, FALSE);

	openOperation = AudioVideoCaptureDevice::OpenForVideoOnlyAsync(mCameraLocation, mDimensions);
	openOperation->Completed = ref new AsyncOperationCompletedHandler<AudioVideoCaptureDevice^>([this] (IAsyncOperation<AudioVideoCaptureDevice^> ^operation, Windows::Foundation::AsyncStatus status) {
		if (status == Windows::Foundation::AsyncStatus::Completed) {
			IAudioVideoCaptureDeviceNative *pNativeDevice = nullptr; 

			ms_debug("[MSWP8Cap] OpenAsyncOperation completed");
			mVideoDevice = operation->GetResults();
			HRESULT hr = reinterpret_cast<IUnknown*>(mVideoDevice)->QueryInterface(__uuidof(IAudioVideoCaptureDeviceNative), (void**) &pNativeDevice);
			if ((pNativeDevice == nullptr) || FAILED(hr)) {
				ms_error("[MSWP8Cap] Unable to query interface IAudioVideoCaptureDeviceNative");
			} else {
				mIsActivated = true;
				mNativeVideoDevice = pNativeDevice;
				configure();

				// Create the sink and notify the start completion
				MakeAndInitialize<SampleSink>(&(this->mVideoSink), this);
				mNativeVideoDevice->SetVideoSampleSink(mVideoSink);
				SetEvent(mStartCompleted);
			}
		}
		else if (status == Windows::Foundation::AsyncStatus::Canceled) {
			ms_warning("[MSWP8Cap] OpenAsyncOperation has been canceled");
		}
		else if (status == Windows::Foundation::AsyncStatus::Error) {
			ms_error("[MSWP8Cap] OpenAsyncOperation failed");
		}
	});

	return 0;
}

int MSWP8Cap::deactivate()
{
	if (mRfc3984Packer != nullptr) {
		rfc3984_destroy(mRfc3984Packer);
		mRfc3984Packer = nullptr;
	}
	mIsActivated = false;
	return 0;
}

void MSWP8Cap::start()
{
	DWORD waitResult;

	if (!mIsStarted && mIsActivated) {
		waitResult = WaitForSingleObjectEx(mStartCompleted, 0, FALSE);
		if (waitResult == WAIT_OBJECT_0) {
			mIsStarted = true;
			mVideoCaptureAction = mVideoDevice->StartRecordingToSinkAsync();
			mVideoCaptureAction->Completed = ref new AsyncActionCompletedHandler([this] (IAsyncAction ^asyncInfo, Windows::Foundation::AsyncStatus status) {
				if (status == Windows::Foundation::AsyncStatus::Completed) {
					ms_message("[MSWP8Cap] StartRecordingToSinkAsync completed");
				}
				else if ((status == Windows::Foundation::AsyncStatus::Error) || (status == Windows::Foundation::AsyncStatus::Canceled)) {
					ms_error("[MSWP8Cap] StartRecordingToSinkAsync did not complete");
				}
			});
		}
	}
}

void MSWP8Cap::stop()
{
	mblk_t *m;

	if (!mIsStarted) return;

	if (mVideoDevice) {
		try {
			mVideoDevice->StopRecordingAsync()->Completed = ref new AsyncActionCompletedHandler([this] (IAsyncAction ^action, Windows::Foundation::AsyncStatus status) {
				if (status == Windows::Foundation::AsyncStatus::Completed) {
					ms_message("[MSWP8Cap] Video successfully stopped");
				} else {
					ms_error("[MSWP8Cap] Error while stopping recording");
				}
				mVideoCaptureAction = nullptr;
				mVideoDevice = nullptr;
				mIsStarted = false;
				SetEvent(mStopCompleted);
			});
		} catch(...) {
			// A Platform::ObjectDisposedException can be raised if the app has had its access
			// to video revoked (most commonly when the app is going out of the foreground)
			ms_warning("[MSWP8Cap] Exception caught while destroying video capture");
			mVideoCaptureAction = nullptr;
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
	mblk_t *m;
	MSQueue nalus;
	uint32_t timestamp;

	ms_mutex_lock(&mMutex);
	// Free samples that have already been sent
	while ((m = ms_queue_get(&mSampleToFreeQueue)) != NULL) {
		freemsg(m);
	}
	// Send queued samples
	while ((m = ms_queue_get(&mSampleToSendQueue)) != NULL) {
		ms_queue_init(&nalus);
		timestamp = mblk_get_timestamp_info(m);
		bitstreamToMsgb(m->b_rptr, m->b_wptr - m->b_rptr, &nalus);
		rfc3984_pack(mRfc3984Packer, &nalus, f->outputs[0], timestamp);
		ms_queue_put(&mSampleToFreeQueue, m);

		if (mSamplesCount == 0) {
			starter.FirstIdrFrame(f->ticker->time);
		} else if (starter.IdrFrameNeeded(f->ticker->time)) {
			// Send I frame 2 seconds and 4 seconds after the beginning
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
	BYTE* pMem = new BYTE[cbSample];

	memcpy(pMem, pSample, cbSample);
	m = esballoc(pMem, cbSample, 0, freeSample);
	m->b_wptr += cbSample;
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
	if (mIsActivated) {
		uint32 value = (uint32)fps;
		CameraCapturePropertyRange^ range = mVideoDevice->GetSupportedPropertyRange(mCameraLocation, KnownCameraAudioVideoProperties::VideoFrameRate);
		uint32_t min = safe_cast<uint32>(range->Min);
		uint32_t max = safe_cast<uint32>(range->Max);
		if (value < min) value = min;
		else if (value > max) value = max;
		mVideoDevice->SetProperty(KnownCameraAudioVideoProperties::VideoFrameRate, value);
		value = safe_cast<uint32>(mVideoDevice->GetProperty(KnownCameraAudioVideoProperties::VideoFrameRate));
		mFps = (float)value;
	} else {
		mFps = fps;
	}
}

void MSWP8Cap::setBitrate(int bitrate)
{
	// TODO
}

MSVideoSize MSWP8Cap::getVideoSize()
{
	MSVideoSize vs;
	vs.width = (int)mDimensions.Width;
	vs.height = (int)mDimensions.Height;
	return vs;
}

void MSWP8Cap::setVideoSize(MSVideoSize vs)
{
	mDimensions.Width = (float)vs.width;
	mDimensions.Height = (float)vs.height;

	if (mIsActivated) {
		IAsyncAction^ action = nullptr;

		selectBestVideoSize();
		action = mVideoDevice->SetCaptureResolutionAsync(mDimensions);
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
}

void MSWP8Cap::requestIdrFrame()
{
	if (mIsStarted) {
		Platform::Boolean value = true;
		mVideoDevice->SetProperty(KnownCameraAudioVideoProperties::H264RequestIdrFrame, value);
	}
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
	requestedSize.width = (int)mDimensions.Width;
	requestedSize.height = (int)mDimensions.Height;
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
		mDimensions.Width = (float)minSize.width;
		mDimensions.Height = (float)minSize.height;
		return false;
	}

	mDimensions.Width = (float)bestFoundSize.width;
	mDimensions.Height = (float)bestFoundSize.height;
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
		uint32 rotation = 360 - mVideoDevice->SensorRotationInDegrees;
		boxedSensorRotation = (rotation == 360) ? 0 : rotation;
	} else if (mCameraLocation == CameraSensorLocation::Back) {
		boxedSensorRotation = mVideoDevice->SensorRotationInDegrees;
	} else {
		uint32 rotation = 0;
		boxedSensorRotation = rotation;
	}
	mVideoDevice->SetProperty(KnownCameraGeneralProperties::EncodeWithOrientation, boxedSensorRotation);

	// Do not mute audio while recording video
	mVideoDevice->SetProperty(KnownCameraAudioVideoProperties::UnmuteAudioWhileRecording, unMuteAudio);

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
			Platform::Object^ boxedProfile = H264EncoderProfile::Baseline;
			mVideoDevice->SetProperty(KnownCameraAudioVideoProperties::H264EncodingProfile, boxedProfile);
		} catch (Platform::COMException^ e) {
			if (e->HResult == E_NOTIMPL) {
				ms_warning("[MSWP8Cap] This device does not support setting the H264 encoding profile");
			}
		}
	} else {
		ms_warning("[MSWP8Cap] This camera does not support H264 baseline profile");
	}

	// Define the video frame rate
	setFps(mFps);
}


void MSWP8Cap::detectCameras(MSWebCamManager *manager, MSWebCamDesc *desc)
{
	Collections::IVectorView<CameraSensorLocation> ^availableSensorLocations;
	Collections::IIterator<CameraSensorLocation> ^availableSensorLocationsIterator;
	int count = 0;

	availableSensorLocations = AudioVideoCaptureDevice::AvailableSensorLocations;
	availableSensorLocationsIterator = availableSensorLocations->First();
	while (availableSensorLocationsIterator->HasCurrent) {
		char buffer[8];
		MSWebCam *cam = ms_web_cam_new(desc);
		memset(buffer, '\0', sizeof(buffer));
		wcstombs(buffer, availableSensorLocationsIterator->Current.ToString()->Data(), sizeof(buffer) - 1);
		cam->name = ms_strdup(buffer);
		cam->data = (void *)availableSensorLocationsIterator->Current;
		ms_web_cam_manager_add_cam(manager, cam);
		availableSensorLocationsIterator->MoveNext();
		count++;
	}

	if (count == 0) {
		ms_warning("[MSWP8Cap] This device does not have a camera");
	}
}



VideoStarter::VideoStarter()
	: mNextTime(0), mIdrFrameCount(0)
{
}

VideoStarter::~VideoStarter()
{
}

void VideoStarter::FirstIdrFrame(uint64_t curtime)
{
	mNextTime = curtime + 2000;
}

bool VideoStarter::IdrFrameNeeded(uint64_t curtime)
{
	if (mNextTime == 0) return false;
	if (curtime >= mNextTime) {
		mIdrFrameCount++;
		if (mIdrFrameCount == 1) {
			mNextTime += 2000;
		} else {
			mNextTime = 0;
		}
		return true;
	}
	return false;
}
