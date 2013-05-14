/*
mswp8cap_reader.cpp

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
#include "mswp8cap_reader.h"

using namespace Microsoft::WRL;
using namespace Windows::Foundation;
using namespace Windows::Phone::Media::Capture;
using namespace mediastreamer2;


static const int defaultFps = 15;


bool MSWP8CapReader::smInstantiated = false;


MSWP8CapReader::MSWP8CapReader()
	: mIsInitialized(false), mIsActivated(false), mIsStarted(false),
	mRfc3984Packer(nullptr), mPackerMode(1), mStartTime(0), mSampleCount(-1), mFps(defaultFps),
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

	if (!selectBestSensorLocation()) {
		ms_error("[MSWP8Cap] It looks like this device does not have a camera");
		return;
	}

	mIsInitialized = true;
	smInstantiated = true;
}

MSWP8CapReader::~MSWP8CapReader()
{
	stop();
	ms_mutex_destroy(&mMutex);
	smInstantiated = false;
}


int MSWP8CapReader::activate()
{
	IAsyncOperation<AudioVideoCaptureDevice^> ^openOperation = nullptr;

	if (!mIsInitialized) return -1;
	if (!selectBestFormat()) return -1;

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
				MakeAndInitialize<MSWP8CapSampleSink>(&(this->mVideoSink), this);
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

int MSWP8CapReader::deactivate()
{
	if (mRfc3984Packer != nullptr) {
		rfc3984_destroy(mRfc3984Packer);
		mRfc3984Packer = nullptr;
	}
	mIsActivated = false;
	return 0;
}

void MSWP8CapReader::start()
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

void MSWP8CapReader::stop()
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

int MSWP8CapReader::feed(MSFilter *f)
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
	}
	ms_mutex_unlock(&mMutex);

	return 0;
}


static void freeSample(void *sample)
{
	delete[] sample;
}

void MSWP8CapReader::OnSampleAvailable(ULONGLONG hnsPresentationTime, ULONGLONG hnsSampleDuration, DWORD cbSample, BYTE* pSample)
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


void MSWP8CapReader::setFps(int fps)
{
	if (mIsActivated) {
		uint32 value = (uint32)fps;
		CameraCapturePropertyRange^ range = mVideoDevice->GetSupportedPropertyRange(mCameraLocation, KnownCameraAudioVideoProperties::VideoFrameRate);
		uint32_t min = safe_cast<uint32>(range->Min);
		uint32_t max = safe_cast<uint32>(range->Max);
		if (value < min) value = min;
		else if (value > max) value = max;
		mVideoDevice->SetProperty(KnownCameraAudioVideoProperties::VideoFrameRate, value);
		mFps = (uint32)mVideoDevice->GetProperty(KnownCameraAudioVideoProperties::VideoFrameRate);
	} else {
		mFps = fps;
	}
}


void MSWP8CapReader::bitstreamToMsgb(uint8_t *encoded_buf, size_t size, MSQueue *nalus) {
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

bool MSWP8CapReader::selectBestSensorLocation()
{
	Collections::IVectorView<CameraSensorLocation> ^availableSensorLocations;
	Collections::IIterator<CameraSensorLocation> ^availableSensorLocationsIterator;
	CameraSensorLocation requestedLocation = mCameraLocation;
	CameraSensorLocation bestLocation = CameraSensorLocation::Front;
	bool locationAvailable = false;

	availableSensorLocations = AudioVideoCaptureDevice::AvailableSensorLocations;
	availableSensorLocationsIterator = availableSensorLocations->First();
	while (availableSensorLocationsIterator->HasCurrent) {
		CameraSensorLocation currentLocation = availableSensorLocationsIterator->Current;
		locationAvailable = true;
		bestLocation = currentLocation;
		if (currentLocation == requestedLocation) break;
		availableSensorLocationsIterator->MoveNext();
	}

	if (locationAvailable) {
		mCameraLocation = bestLocation;
	}
	return locationAvailable;
}

bool MSWP8CapReader::selectBestFormat()
{
	Collections::IVectorView<Size> ^availableSizes;
	Collections::IIterator<Size> ^availableSizesIterator;
	MSVideoSize requestedSize;
	MSVideoSize bestFoundSize;

	bestFoundSize.width = bestFoundSize.height = 0;
	requestedSize.width = (int)mDimensions.Width;
	requestedSize.height = (int)mDimensions.Height;
	availableSizes = AudioVideoCaptureDevice::GetAvailableCaptureResolutions(mCameraLocation);
	availableSizesIterator = availableSizes->First();
	while (availableSizesIterator->HasCurrent) {
		MSVideoSize currentSize;
		currentSize.width = (int)availableSizesIterator->Current.Width;
		currentSize.height = (int)availableSizesIterator->Current.Height;
		ms_message("Seeing format %ix%i", currentSize.width, currentSize.height);
		if (ms_video_size_greater_than(requestedSize, currentSize)) {
			if (ms_video_size_greater_than(currentSize, bestFoundSize)) {
				bestFoundSize = currentSize;
			}
		}
		availableSizesIterator->MoveNext();
	}

	if ((bestFoundSize.width == 0) && bestFoundSize.height == 0) {
		ms_error("This camera does not support our format");
		return false;
	}

	mDimensions.Width = (float)bestFoundSize.width;
	mDimensions.Height = (float)bestFoundSize.height;
	ms_message("Best camera format is %ix%i", bestFoundSize.width, bestFoundSize.height);
	return true;
}

void MSWP8CapReader::configure()
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
		boxedSensorRotation = 0;
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
				ms_warning("This device does not support setting the H264 encoding profile");
			}
		}
	} else {
		ms_warning("This camera does not support H264 baseline profile");
	}

	// Define the video frame rate
	setFps(mFps);
}
