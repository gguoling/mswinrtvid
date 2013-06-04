/*
mswp8dis.h

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
#include <robuffer.h>
#include <windows.storage.streams.h>
#include "mediastreamer2/msfilter.h"
#include "mediastreamer2/rfc3984.h"

#include "IVideoRenderer.h"


namespace mswp8vid
{
	class MSWP8Dis {
	public:
		MSWP8Dis();
		virtual ~MSWP8Dis();

		int activate();
		int deactivate();
		bool isStarted() { return mIsStarted; }
		void start();
		void stop();
		int feed(MSFilter *f);
		Mediastreamer2::WP8Video::IVideoRenderer^ getVideoRenderer();
		void setVideoRenderer(Mediastreamer2::WP8Video::IVideoRenderer^ renderer);

	private:
		int nalusToFrame(MSQueue *nalus, bool *new_sps_pps);
		void enlargeBitstream(int newSize);
		bool checkSPSChange(mblk_t *sps);
		bool checkPPSChange(mblk_t *pps);
		void updateSPS(mblk_t *sps);
		void updatePPS(mblk_t *pps);
		void updateVideoSizeFromSPS();

		static bool smInstantiated;
		bool mIsInitialized;
		bool mIsActivated;
		bool mIsStarted;
		int mWidth;
		int mHeight;
		Rfc3984Context *mRfc3984Unpacker;
		int mBitstreamSize;
		uint8_t *mBitstream;
		mblk_t *mSPS;
		mblk_t *mPPS;
		Mediastreamer2::WP8Video::IVideoRenderer^ mRenderer;
	};
}
