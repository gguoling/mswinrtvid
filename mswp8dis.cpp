/*
mswp8dis.cpp

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
#include "mswp8dis.h"

using namespace Microsoft::WRL;
using namespace mediastreamer2;


bool MSWP8Dis::smInstantiated = false;


MSWP8Dis::MSWP8Dis()
	: mIsInitialized(false), mIsStarted(false)
{
	if (smInstantiated) {
		ms_error("[MSWP8Dis] A video display filter is already instantiated. A second one can not be created.");
		return;
	}

	mIsInitialized = true;
	smInstantiated = true;
}

MSWP8Dis::~MSWP8Dis()
{
	stop();
	smInstantiated = false;
}

void MSWP8Dis::start()
{
	if (mIsInitialized) {
		mIsStarted = true;
	}
}

void MSWP8Dis::stop()
{
	mIsStarted = false;
}

int MSWP8Dis::feed(MSFilter *f)
{
	mblk_t *m;

	if (mIsStarted) {
		while ((m = ms_queue_get(f->inputs[0])) != NULL) {
			Globals::Instance->VideoSampleDispatcher->writeSample(m->b_rptr, m->b_wptr - m->b_rptr, f->ticker->time * 10000LL);
		}
	}

	return 0;
}



MSWP8DisplayEventDispatcher::MSWP8DisplayEventDispatcher()
{
}

MSWP8DisplayEventDispatcher::~MSWP8DisplayEventDispatcher()
{
}

void MSWP8DisplayEventDispatcher::writeSample(BYTE* bytes, int byteCount, UINT64 hnsPresentationTime)
{
	ComPtr<NativeBuffer> spNativeBuffer = NULL;
	BYTE* pBuf = new BYTE[byteCount];

	memcpy((void*)pBuf, (void*)bytes, byteCount);
	MakeAndInitialize<NativeBuffer>(&spNativeBuffer, pBuf, byteCount, TRUE);
	sampleReceived(NativeBuffer::GetIBufferFromNativeBuffer(spNativeBuffer), hnsPresentationTime);
}



Globals^ Globals::singleton = nullptr;

Globals::Globals()
	: videoSampleDispatcher(ref new MSWP8DisplayEventDispatcher())
{
}

Globals::~Globals()
{
}

Globals^ Globals::Instance::get()
{
	if (Globals::singleton == nullptr) {
		Globals::singleton = ref new Globals();
	}

	return Globals::singleton;
}

MSWP8DisplayEventDispatcher^ Globals::VideoSampleDispatcher::get()
{
	return this->videoSampleDispatcher;
}
