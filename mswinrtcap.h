/*
mswinrtcap.h

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


#pragma once


#include <mediastreamer2/mswebcam.h>
#include <mediastreamer2/rfc3984.h>
#include <mediastreamer2/videostarter.h>

#include "mswinrtvid.h"
#include "mswinrtmediasink.h"

#include <wrl\implements.h>
#include <ppltasks.h>

using namespace Windows::Media::Capture;
using namespace Windows::Media::Devices;
using namespace Windows::Media::MediaProperties;


namespace libmswinrtvid
{
	ref class MSWinRTCapHelper sealed {
	internal:
		MSWinRTCapHelper();
		bool Initialize(Platform::String^ DeviceId);
		bool StartPreview(int DeviceOrientation);
		void StopPreview();
		bool StartCapture(Windows::Media::MediaProperties::MediaEncodingProfile^ EncodingProfile);
		void StopCapture();
		void MSWinRTCapHelper::OnSampleAvailable(BYTE *buf, DWORD bufLen, LONGLONG presentationTime);
		MSVideoSize SelectBestVideoSize(MSVideoSize vs);
		mblk_t * GetSample();
		
		property Platform::Agile<MediaCapture^> CaptureDevice
		{
			Platform::Agile<MediaCapture^> get() { return mCapture; }
		}

	private:
		~MSWinRTCapHelper();
		void OnCaptureFailed(Windows::Media::Capture::MediaCapture^ sender, Windows::Media::Capture::MediaCaptureFailedEventArgs^ errorEventArgs);

		HANDLE mInitializationCompleted;
		HANDLE mStartCompleted;
		HANDLE mStopCompleted;
		HANDLE mPreviewStartCompleted;
		HANDLE mPreviewStopCompleted;
		const GUID mRotationKey;
		Platform::Agile<MediaCapture^> mCapture;
		Windows::Foundation::EventRegistrationToken mMediaCaptureFailedEventRegistrationToken;
		ComPtr<IMFMediaSink> mMediaSink;
		MediaEncodingProfile^ mEncodingProfile;
		int mDeviceOrientation;
		ms_mutex_t mMutex;
		MSYuvBufAllocator *mAllocator;
		MSQueue mSamplesQueue;
	};

	class MSWinRTCap {
	public:
		MSWinRTCap();
		virtual ~MSWinRTCap();

		void initialize();
		int activate();
		int deactivate();
		bool isStarted() { return mIsStarted; }
		void start();
		void stop();
		int feed(MSFilter *f);

		void OnSampleAvailable(BYTE *buf, DWORD bufLen, LONGLONG presentationTime);

		void setCaptureElement(Windows::UI::Xaml::Controls::CaptureElement^ captureElement) { mCaptureElement = captureElement; }
		void setDeviceId(Platform::String^ id) { mDeviceId = id; }
		void setFront(bool front) { mFront = front; }
		void setExternal(bool external) { mExternal = external; }
		MSPixFmt getPixFmt() { return MS_YUV420P; }
		float getFps() { return mFps; }
		float getAverageFps();
		void setFps(float fps);
		MSVideoSize getVideoSize();
		void setVideoSize(MSVideoSize vs);
		int getDeviceOrientation() { return mDeviceOrientation; }
		void setDeviceOrientation(int degrees);

		static void detectCameras(MSWebCamManager *manager, MSWebCamDesc *desc);

	private:
		void applyFps();
		void applyVideoSize();
		void selectBestVideoSize(MSVideoSize vs);
		void configure();
		static void addCamera(MSWebCamManager *manager, MSWebCamDesc *desc, Windows::Devices::Enumeration::DeviceInformation^ DeviceInfo);
		static void registerCameras(MSWebCamManager *manager);

		static bool smInstantiated;
		static MSList *smCameras;
		bool mIsInitialized;
		bool mIsActivated;
		bool mIsStarted;
		float mFps;
		MSAverageFPS mAvgFps;
		MSVideoSize mVideoSize;
		uint64_t mStartTime;
		int mDeviceOrientation;
		MSVideoStarter mStarter;
		Windows::UI::Xaml::Controls::CaptureElement^ mCaptureElement;
		Platform::String^ mDeviceId;
		bool mFront;
		bool mExternal;
		MSWinRTCapHelper^ mHelper;
		MediaEncodingProfile^ mEncodingProfile;
	};
}
