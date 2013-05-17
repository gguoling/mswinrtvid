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
using namespace mswp8vid;


bool MSWP8Dis::smInstantiated = false;


MSWP8Dis::MSWP8Dis()
	: mIsInitialized(false), mIsActivated(false), mIsStarted(false)
{
	if (smInstantiated) {
		ms_error("[MSWP8Dis] A video display filter is already instantiated. A second one can not be created.");
		return;
	}

	mBitstreamSize = 65536;
	mBitstream = (uint8_t *)ms_malloc0(mBitstreamSize);
	mIsInitialized = true;
	smInstantiated = true;
}

MSWP8Dis::~MSWP8Dis()
{
	stop();
	ms_free(mBitstream);
	smInstantiated = false;
}

int MSWP8Dis::activate()
{
	if (!mIsInitialized) return -1;

	mRfc3984Unpacker = rfc3984_new();
	mIsActivated = true;
	return 0;
}

int MSWP8Dis::deactivate()
{
	if (mRfc3984Unpacker != nullptr) {
		rfc3984_destroy(mRfc3984Unpacker);
		mRfc3984Unpacker = nullptr;
	}
	mIsActivated = false;
	return 0;
}

void MSWP8Dis::start()
{
	if (!mIsStarted && mIsActivated) {
		mIsStarted = true;
		Platform::String^ format = ref new Platform::String(L"H264");
		Globals::Instance->startRendering(format, 640, 480);
	}
}

void MSWP8Dis::stop()
{
	if (mIsStarted) {
		mIsStarted = false;
		Globals::Instance->stopRendering();
	}
}

int MSWP8Dis::feed(MSFilter *f)
{
	mblk_t *m;
	MSQueue nalus;

	if (mIsStarted) {
		ms_queue_init(&nalus);
		while ((m = ms_queue_get(f->inputs[0])) != NULL) {
			rfc3984_unpack(mRfc3984Unpacker, m, &nalus);
			if (!ms_queue_empty(&nalus)) {
				bool need_reinit = false;
				int size = nalusToFrame(&nalus, &need_reinit);
				if (size > 0) {
					Globals::Instance->VideoSampleDispatcher->writeSample(mBitstream, size, f->ticker->time * 10000LL);
				}
			}
		}
	}

	return 0;
}

int MSWP8Dis::nalusToFrame(MSQueue *nalus, bool *new_sps_pps)
{
	mblk_t *im;
	uint8_t *dst = mBitstream, *src, *end;
	int nal_len;
	bool start_picture = true;
	uint8_t nalu_type;

	*new_sps_pps = false;
	end = mBitstream + mBitstreamSize;

	while ((im = ms_queue_get(nalus)) != NULL) {
		src = im->b_rptr;
		nal_len = im->b_wptr - src;
		if (dst + nal_len + 100 > end) {
			int pos = dst - mBitstream;
			enlargeBitstream(mBitstreamSize + nal_len + 100);
			dst = mBitstream + pos;
			end = mBitstream + mBitstreamSize;
		}
		if (src[0] == 0 && src[1] == 0 && src[2] == 0 && src[3] == 1) {
			int size = im->b_wptr - src;
			/* Workaround for stupid RTP H264 sender that includes nal markers */
			memcpy(dst, src, size);
			dst += size;
		} else {
			nalu_type = (*src) & ((1 << 5) - 1);
			/*if (nalu_type == 7)
				*new_sps_pps = check_sps_change(d, im) || *new_sps_pps;
			if (nalu_type == 8)
				*new_sps_pps = check_pps_change(d, im) || *new_sps_pps;*/
			if (start_picture || nalu_type == 7/*SPS*/ || nalu_type == 8/*PPS*/ ) {
				*dst++ = 0;
				start_picture = false;
			}

			/* Prepend nal marker */
			*dst++ = 0;
			*dst++ = 0;
			*dst++ = 1;
			*dst++ = *src++;
			while (src < (im->b_wptr - 3)) {
				if (src[0] == 0 && src[1] == 0 && src[2] < 3) {
					*dst++ = 0;
					*dst++ = 0;
					*dst++ = 3;
					src += 2;
				}
				*dst++ = *src++;
			}
			*dst++ = *src++;
			*dst++ = *src++;
			*dst++ = *src++;
		}
		freemsg(im);
	}
	return dst - mBitstream;
}

void MSWP8Dis::enlargeBitstream(int newSize)
{
	mBitstreamSize = newSize;
	mBitstream = (uint8_t *)ms_realloc(mBitstream, mBitstreamSize);
}



DisplayEventDispatcher::DisplayEventDispatcher()
{
}

DisplayEventDispatcher::~DisplayEventDispatcher()
{
}

void DisplayEventDispatcher::writeSample(BYTE* bytes, int byteCount, UINT64 hnsPresentationTime)
{
	ComPtr<NativeBuffer> spNativeBuffer = NULL;
	BYTE* pBuf = new BYTE[byteCount];

	memcpy((void*)pBuf, (void*)bytes, byteCount);
	MakeAndInitialize<NativeBuffer>(&spNativeBuffer, pBuf, byteCount, TRUE);
	sampleReceived(NativeBuffer::GetIBufferFromNativeBuffer(spNativeBuffer), hnsPresentationTime);
}



Globals^ Globals::singleton = nullptr;

Globals::Globals()
	: videoSampleDispatcher(ref new DisplayEventDispatcher())
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

DisplayEventDispatcher^ Globals::VideoSampleDispatcher::get()
{
	return this->videoSampleDispatcher;
}

void Globals::startRendering(Platform::String^ format, int width, int height)
{
	renderStarted(format, width, height);
}

void Globals::stopRendering()
{
	renderStopped();
}