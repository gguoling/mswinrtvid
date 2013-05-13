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
	mStartTime(0), mSampleCount(-1), mFps(defaultFps),
	mCameraLocation(CameraSensorLocation::Front),
	mDimensions(MS_VIDEO_SIZE_CIF_W, MS_VIDEO_SIZE_CIF_H),
	mVideoDevice(nullptr)
{
	ms_mutex_init(&mMutex, NULL);
	qinit(&mQueue);

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
	flushq(&mQueue, 0);
	ms_mutex_destroy(&mMutex);
	smInstantiated = false;
}


int MSWP8CapReader::activate()
{
	IAsyncOperation<AudioVideoCaptureDevice^> ^openOperation = nullptr;

	if (!mIsInitialized) return -1;
	if (!selectBestFormat()) return -1;

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
	flushq(&mQueue, 0);
	ms_mutex_unlock(&mMutex);
}

int MSWP8CapReader::feed(MSFilter *f)
{
	mblk_t *m;
	uint32_t timestamp;

	if (isTimeToSend(f->ticker->time)) {
		mblk_t *om = NULL;
		/* Keep the most recent sample if several samples have been captured */
		while((m = getSample()) != NULL) {
			if (om != NULL) freemsg(om);
			om = m;
		}
		if (om != NULL) {
			timestamp = (uint32_t)(f->ticker->time * 90); /* RTP uses a 90000 Hz clockrate for video */
			mblk_set_timestamp_info(om, timestamp);
			ms_queue_put(f->outputs[0], om);
		}
	}

	return 0;
}


static void dummy_free_fct(void *)
{}

void MSWP8CapReader::OnSampleAvailable(ULONGLONG hnsPresentationTime, ULONGLONG hnsSampleDuration, DWORD cbSample, BYTE* pSample)
{
	MS_UNUSED(hnsPresentationTime);
	MS_UNUSED(hnsSampleDuration);
	mblk_t *m = esballoc(pSample, cbSample, 0, dummy_free_fct);
	m->b_wptr += cbSample;
	ms_mutex_lock(&mMutex);
	putq(&mQueue, ms_yuv_buf_alloc_from_buffer((int)mDimensions.Width, (int)mDimensions.Height, m));
	ms_mutex_unlock(&mMutex);
}


void MSWP8CapReader::setFps(int fps)
{
	if (mIsActivated) {
		uint32 value = (uint32)fps;
		mVideoDevice->SetProperty(KnownCameraAudioVideoProperties::VideoFrameRate, value);
		mFps = (uint32)mVideoDevice->GetProperty(KnownCameraAudioVideoProperties::VideoFrameRate);
	} else {
		mFps = fps;
	}
}


bool MSWP8CapReader::isTimeToSend(uint64_t tickerTime)
{
	if (mSampleCount == -1) {
		mStartTime = tickerTime;
		mSampleCount = 0;
	}
	int curSample = (int)(((tickerTime - mStartTime) * mFps) / 1000.0);
	if (curSample > mSampleCount) {
		mSampleCount++;
		return true;
	}
	return false;
}

mblk_t * MSWP8CapReader::getSample()
{
	mblk_t *m = NULL;
	ms_mutex_lock(&mMutex);
	m = getq(&mQueue);
	ms_mutex_unlock(&mMutex);
	return m;
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

	mVideoDevice->VideoEncodingFormat = CameraCaptureVideoFormat::H264;
	setFps(mFps);
	mVideoDevice->SetProperty(KnownCameraAudioVideoProperties::UnmuteAudioWhileRecording, unMuteAudio);
}
