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
	class MSWinRTCap {
	public:
		MSWinRTCap();
		virtual ~MSWinRTCap();

		int activate();
		int deactivate();
		bool isStarted() { return mIsStarted; }
		void start();
		void stop();
		int feed(MSFilter *f);

		void OnSampleAvailable(ULONGLONG hnsPresentationTime, ULONGLONG hnsSampleDuration, DWORD cbSample, BYTE* pSample);

		void setCaptureElement(Windows::UI::Xaml::Controls::CaptureElement^ captureElement) { mCaptureElement = captureElement; };
		void setDeviceId(Platform::String^ id) { mDeviceId = id; };
		MSPixFmt getPixFmt() { return mPixFmt; }
		void setPixFmt(MSPixFmt pixFmt) { mPixFmt = pixFmt; }
		float getFps() { return mVConf.fps; }
		void setFps(float fps);
		int getBitrate() { return mBitrate; }
		void setBitrate(int bitrate);
		MSVideoSize getVideoSize();
		void setVideoSize(MSVideoSize vs);
		const MSVideoConfiguration * getConfigurationList();
		void setConfiguration(const MSVideoConfiguration *vconf);
		int getCameraSensorRotation() { return mCameraSensorRotation; }
		void setDeviceOrientation(int degrees);
		void requestIdrFrame();

		static void detectCameras(MSWebCamManager *manager, MSWebCamDesc *desc);

	private:
		void applyFps();
		void applyVideoSize();
		void bitstreamToMsgb(uint8_t *encoded_buf, size_t size, MSQueue *nalus);
		bool selectBestVideoSize();
		void configure();
		void printProperties();
		static void addCamera(MSWebCamManager *manager, MSWebCamDesc *desc, Platform::String^ DeviceId, Platform::String^ DeviceName);

		static bool smInstantiated;
		bool mIsInitialized;
		bool mIsActivated;
		bool mIsStarted;
		MSQueue mSampleToSendQueue;
		MSQueue mSampleToFreeQueue;
		ms_mutex_t mMutex;
		Rfc3984Context *mRfc3984Packer;
		MSYuvBufAllocator *mAllocator;
		int mPackerMode;
		uint64_t mStartTime;
		int mSamplesCount;
		int mBitrate;
		int mCameraSensorRotation;
		int mDeviceOrientation;
		MSVideoStarter mStarter;
		MSVideoConfiguration mVConf;
		MSPixFmt mPixFmt;
		HANDLE mActivationCompleted;
		HANDLE mStartCompleted;
		HANDLE mStopCompleted;
		HANDLE mPreviewStartCompleted;
		Windows::UI::Xaml::Controls::CaptureElement^ mCaptureElement;
		Platform::String^ mDeviceId;
		Platform::Agile<MediaCapture^> mCapture;
		MediaEncodingProfile^ mEncodingProfile;
		MSWinRTMediaSinkProxy^ mMediaSink;
	};
}
