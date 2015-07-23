/*
mswinrtdis.h

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


#include "mswinrtvid.h"

#include <mediastreamer2/rfc3984.h>

#include <collection.h>
#include <ppltasks.h>
#include <mutex>
#include <robuffer.h>
#include <windows.storage.streams.h>


namespace libmswinrtvid
{
	class MSWinRTDis;

	private ref class MSWinRTDisSample sealed
	{
	public:
		MSWinRTDisSample(Windows::Storage::Streams::IBuffer^ pBuffer, UINT64 filterTime)
		{
			this->Buffer = pBuffer;
			this->FilterTime = filterTime;
		}

		property Windows::Storage::Streams::IBuffer^ Buffer
		{
			Windows::Storage::Streams::IBuffer^ get() { return mBuffer; };
			void set(Windows::Storage::Streams::IBuffer^ value) { mBuffer = value; };
		}

		property UINT64 FilterTime
		{
			UINT64 get() { return mFilterTime; };
			void set(UINT64 value) { mFilterTime = value; };
		}

	private:
		~MSWinRTDisSample() {};

		Windows::Storage::Streams::IBuffer^ mBuffer;
		UINT64 mFilterTime;
	};

	private ref class MSWinRTDisSampleHandler sealed
	{
	public:
		MSWinRTDisSampleHandler();
		virtual ~MSWinRTDisSampleHandler();
		void StartMediaElement();
		void StopMediaElement();
		void Feed(Windows::Storage::Streams::IBuffer^ pBuffer, UINT64 filterTime);
		void OnSampleRequested(Windows::Media::Core::MediaStreamSource ^sender, Windows::Media::Core::MediaStreamSourceSampleRequestedEventArgs ^args);
		void RequestMediaElementRestart();

		property unsigned int PixFmt
		{
			unsigned int get() { return mPixFmt; }
			void set(unsigned int value) { mPixFmt = (MSPixFmt)value; }
		}

		property Windows::UI::Xaml::Controls::MediaElement^ MediaElement
		{
			Windows::UI::Xaml::Controls::MediaElement^ get() { return mMediaElement; }
			void set(Windows::UI::Xaml::Controls::MediaElement^ value) { mMediaElement = value; }
		}

		property int Width
		{
			int get() { return mWidth; }
			void set(int value) { mWidth = value; }
		}

		property int Height
		{
			int get() { return mHeight; }
			void set(int value) { mHeight = value; }
		}

	private:
		void AnswerSampleRequest(Windows::Media::Core::MediaStreamSourceSampleRequest^ sampleRequest);

		Platform::Collections::Vector<MSWinRTDisSample^>^ mSampleQueue;
		Windows::Media::Core::MediaStreamSourceSampleRequest^ mSampleRequest;
		Windows::Media::Core::MediaStreamSourceSampleRequestDeferral^ mSampleRequestDeferral;
		Windows::UI::Xaml::Controls::MediaElement^ mMediaElement;
		UINT64 mLastPresentationTime;
		UINT64 mLastFilterTime;
		std::mutex mMutex;
		MSPixFmt mPixFmt;
		int mWidth;
		int mHeight;
	};


	class MSWinRTDis {
	public:
		MSWinRTDis();
		virtual ~MSWinRTDis();

		int activate();
		int deactivate();
		bool isStarted() { return mIsStarted; }
		void start();
		void stop();
		int feed(MSFilter *f);
		MSVideoSize getVideoSize();
		void setVideoSize(MSVideoSize vs);
		void setMediaElement(Windows::UI::Xaml::Controls::MediaElement^ mediaElement) { mSampleHandler->MediaElement = mediaElement; }

	private:
		static bool smInstantiated;
		bool mIsInitialized;
		bool mIsActivated;
		bool mIsStarted;
		uint8_t *mBuffer;
		MSWinRTDisSampleHandler^ mSampleHandler;
		Windows::Media::Core::MediaStreamSource^ mMediaStreamSource;
	};
}
