/*
mswinrtdis.cpp

mediastreamer2 library - modular sound and video processing and streaming
Windows Audio Session API sound card plugin for mediastreamer2
Copyright (C) 2010-2015 Belledonne Communications, Grenoble, France

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

#include <wrl.h>

#include "mswinrtdis.h"
#include "VideoBuffer.h"


using namespace libmswinrtvid;


MSWinRTDis::MSWinRTDis()
	: mIsInitialized(false), mIsActivated(false), mIsStarted(false)
{
	mRenderer = ref new MSWinRTRenderer();
	mIsInitialized = true;
}

MSWinRTDis::~MSWinRTDis()
{
	stop();
}

int MSWinRTDis::activate()
{
	if (!mIsInitialized) return -1;
	mIsActivated = true;
	return 0;
}

int MSWinRTDis::deactivate()
{
	mIsActivated = false;
	return 0;
}

void MSWinRTDis::start()
{
	if (!mIsStarted && mIsActivated) {
		mIsStarted = mRenderer->Start();
	}
}

void MSWinRTDis::stop()
{
	if (mIsStarted) {
		mIsStarted = false;
	}
}

int MSWinRTDis::feed(MSFilter *f)
{
	if (mIsStarted) {
		mblk_t *im;

		if ((f->inputs[0] != NULL) && ((im = ms_queue_peek_last(f->inputs[0])) != NULL)) {
			int size = 0;
			MSPicture buf;
			if (ms_yuv_buf_init_from_mblk(&buf, im) == 0) {
				if ((buf.w != mRenderer->Width) || (buf.h != mRenderer->Height)) {
					mRenderer->Width = buf.w;
					mRenderer->Height = buf.h;
					if (mBuffer) {
						ms_free(mBuffer);
						mBuffer = NULL;
					}
				}
				size = (buf.w * buf.h * 3) / 2;
				if (!mBuffer) mBuffer = (uint8_t *)ms_malloc(size);
				int ysize = buf.w * buf.h;
				int usize = ysize / 4;
				memcpy(mBuffer, buf.planes[0], ysize);
				for (int i = 0; i < usize; i++) {
					mBuffer[ysize + (i * 2)] = buf.planes[1][i];
					mBuffer[ysize + (i * 2) + 1] = buf.planes[2][i];
				}
				Microsoft::WRL::ComPtr<VideoBuffer> spVideoBuffer = NULL;
				Microsoft::WRL::MakeAndInitialize<VideoBuffer>(&spVideoBuffer, mBuffer, size);
				mRenderer->Feed(VideoBuffer::GetIBuffer(spVideoBuffer), buf.w, buf.h);
			}
		}
	}

	if (f->inputs[0] != NULL) {
		ms_queue_flush(f->inputs[0]);
	}
	if (f->inputs[1] != NULL) {
		ms_queue_flush(f->inputs[1]);
	}

	return 0;
}

MSVideoSize MSWinRTDis::getVideoSize()
{
	MSVideoSize vs;
	vs.width = mRenderer->Width;
	vs.height = mRenderer->Height;
	return vs;
}

void MSWinRTDis::setVideoSize(MSVideoSize vs)
{
	mRenderer->Width = vs.width;
	mRenderer->Height = vs.height;
}

void MSWinRTDis::setSwapChainPanel(Platform::String ^swapChainPanelName)
{
	mRenderer->SetSwapChainPanel(swapChainPanelName);
}
