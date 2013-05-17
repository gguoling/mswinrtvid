/*
mswp8cap.h

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


#include "windows.h"
#include "implements.h"
#include <Windows.Phone.Media.Capture.h>
#include <Windows.Phone.Media.Capture.Native.h>
#include "mediastreamer2/msfilter.h"
#include "mediastreamer2/mswebcam.h"
#include "mediastreamer2/rfc3984.h"


namespace mswp8vid
{
		class MSWP8CapSampleSink;

		class MSWP8Cap {
		public:
			MSWP8Cap();
			virtual ~MSWP8Cap();

			int activate();
			int deactivate();
			bool isStarted() { return mIsStarted; }
			void start();
			void stop();
			int feed(MSFilter *f);

			void OnSampleAvailable(ULONGLONG hnsPresentationTime, ULONGLONG hnsSampleDuration, DWORD cbSample, BYTE* pSample);

			void setCameraLocation(uint32 location);
			int getFps() { return mFps; }
			void setFps(int fps);
			int getBitrate() { return mBitrate; }
			void setBitrate(int bitrate);
			MSVideoSize getVideoSize();
			void setVideoSize(MSVideoSize vs);
			void requestIdrFrame();

			static void detectCameras(MSWebCamManager *manager, MSWebCamDesc *desc);

		private:
			void bitstreamToMsgb(uint8_t *encoded_buf, size_t size, MSQueue *nalus);
			bool selectBestVideoSize();
			void configure();

			static bool smInstantiated;
			bool mIsInitialized;
			bool mIsActivated;
			bool mIsStarted;
			MSQueue mSampleToSendQueue;
			MSQueue mSampleToFreeQueue;
			ms_mutex_t mMutex;
			Rfc3984Context *mRfc3984Packer;
			int mPackerMode;
			uint64_t mStartTime;
			int mSampleCount;
			int mFps;
			int mBitrate;
			HANDLE mStartCompleted;
			HANDLE mStopCompleted;
			Windows::Phone::Media::Capture::CameraSensorLocation mCameraLocation;
			Windows::Foundation::Size mDimensions;
			Windows::Phone::Media::Capture::AudioVideoCaptureDevice^ mVideoDevice;
			MSWP8CapSampleSink *mVideoSink;
			IAudioVideoCaptureDeviceNative* mNativeVideoDevice;
			Windows::Foundation::IAsyncAction^ mVideoCaptureAction;
		};

		class MSWP8CapSampleSink
			: public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::RuntimeClassType::ClassicCom>, ICameraCaptureSampleSink>
		{
		public:
			STDMETHODIMP RuntimeClassInitialize(MSWP8Cap *reader) {
				m_dwSampleCount = 0;
				m_reader = reader;
				return S_OK;
			}

			DWORD GetSampleCount() {
				return m_dwSampleCount;
			}

			IFACEMETHODIMP_(void) OnSampleAvailable(ULONGLONG hnsPresentationTime, ULONGLONG hnsSampleDuration, DWORD cbSample, BYTE* pSample) {
				m_dwSampleCount++;
				if (m_reader) {
					m_reader->OnSampleAvailable(hnsPresentationTime, hnsSampleDuration, cbSample, pSample);
				}
			}

		private:
			DWORD m_dwSampleCount;
			MSWP8Cap *m_reader;
		};
}
