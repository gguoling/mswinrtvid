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


bool MSWP8CapReader::smInstantiated = false;


MSWP8CapReader::MSWP8CapReader()
	: mIsInitialized(false), mIsActivated(false), mIsStarted(false),
	mCameraLocation(CameraSensorLocation::Front),
	mDimensions(MS_VIDEO_SIZE_CIF_W, MS_VIDEO_SIZE_CIF_H),
	mVideoDevice(nullptr)
{
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
				mNativeVideoDevice = pNativeDevice;
				mVideoDevice->VideoEncodingFormat = CameraCaptureVideoFormat::H264;
				// Create the sink and notify the start completion
				MakeAndInitialize<MSWP8CapSampleSink>(&(this->mVideoSink), this);
				mNativeVideoDevice->SetVideoSampleSink(mVideoSink);
				mIsActivated = true;
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
}

void MSWP8CapReader::OnSampleAvailable(ULONGLONG hnsPresentationTime, ULONGLONG hnsSampleDuration, DWORD cbSample, BYTE* pSample)
{
	// TODO
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
	requestedSize.width = mDimensions.Width;
	requestedSize.height = mDimensions.Height;
	availableSizes = AudioVideoCaptureDevice::GetAvailableCaptureResolutions(mCameraLocation);
	availableSizesIterator = availableSizes->First();
	while (availableSizesIterator->HasCurrent) {
		MSVideoSize currentSize;
		currentSize.width = availableSizesIterator->Current.Width;
		currentSize.height = availableSizesIterator->Current.Height;
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

	mDimensions.Width = bestFoundSize.width;
	mDimensions.Height = bestFoundSize.height;
	ms_message("Best camera format is %ix%i", bestFoundSize.width, bestFoundSize.height);
	return true;
}