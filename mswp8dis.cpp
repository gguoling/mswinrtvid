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
#include "VideoBuffer.h"

using namespace Microsoft::WRL;
using namespace mswp8vid;
using namespace Mediastreamer2::WP8Video;



static uint8_t get_u8_at_bit_offset(uint8_t *data, uint32_t *bit_offset) {
	uint8_t res = ((data[*bit_offset / 8] << (*bit_offset % 8)) | (data[(*bit_offset / 8) + 1] >> (8 - (*bit_offset % 8))));
	*bit_offset += 8;
	return res;
}

static uint8_t get_u1_at_bit_offset(uint8_t *data, uint32_t *bit_offset) {
	uint8_t mod = *bit_offset % 8;
	uint8_t res = (data[*bit_offset / 8] & ((1 << (8 - mod)) - 1)) >> (7 - mod);
	*bit_offset += 1;
	return res;
}

static uint8_t get_ue_at_bit_offset(uint8_t *data, uint32_t *bit_offset) {
	uint8_t b;
	uint8_t i;
	uint8_t res = 0;
	int leading_zero_bits = -1;
	for (b = 0; !b; leading_zero_bits++) {
		b = get_u1_at_bit_offset(data, bit_offset);
	}
	for (i = 0; i < leading_zero_bits; i++) {
		b = get_u1_at_bit_offset(data, bit_offset);
		res <<= 1;
		res |= b;
	}
	res += (1 << leading_zero_bits) - 1;
	return res;
}



bool MSWP8Dis::smInstantiated = false;


MSWP8Dis::MSWP8Dis()
	: mIsInitialized(false), mIsActivated(false), mIsStarted(false), mWidth(MS_VIDEO_SIZE_CIF_W), mHeight(MS_VIDEO_SIZE_CIF_H),
	mRfc3984Unpacker(nullptr), mBitstreamSize(65536), mSPS(nullptr), mPPS(nullptr), mRenderer(nullptr)
{
	if (smInstantiated) {
		ms_error("[MSWP8Dis] A video display filter is already instantiated. A second one can not be created.");
		return;
	}

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
	if (mSPS) {
		freemsg(mSPS);
		mSPS = nullptr;
	}
	if (mPPS) {
		freemsg(mPPS);
		mPPS = nullptr;
	}
	mIsActivated = false;
	return 0;
}

void MSWP8Dis::start()
{
	if (!mIsStarted && mIsActivated) {
		mIsStarted = true;
		Platform::String^ format = ref new Platform::String(L"H264");
		if (mRenderer != nullptr) {
			ms_message("[MSWP8Dis] Start renderer %s - %dx%d", "H264", mWidth, mHeight);
			mRenderer->Start(format, mWidth, mHeight);
		}
	}
}

void MSWP8Dis::stop()
{
	if (mIsStarted) {
		mIsStarted = false;
		if (mRenderer != nullptr) {
			ms_message("[MSWP8Dis] Stop renderer");
			mRenderer->Stop();
		}
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
				if (need_reinit) {
					Platform::String^ format = ref new Platform::String(L"H264");
					if (mRenderer != nullptr) {
						ms_message("[MSWP8Dis] Change renderer format: %s - %dx%d", "H264", mWidth, mHeight);
						mRenderer->ChangeFormat(format, mWidth, mHeight);
					}
				}
				if ((size > 0) && (mRenderer != nullptr)) {
					if (mRenderer->Dispatcher != nullptr) {
						ComPtr<VideoBuffer> spVideoBuffer = NULL;
						MakeAndInitialize<VideoBuffer>(&spVideoBuffer, (BYTE *)mBitstream, size);
						mRenderer->Dispatcher->OnSampleReceived(VideoBuffer::GetIBuffer(spVideoBuffer), f->ticker->time * 10000LL);
					}
				}
			}
		}
	} else {
		if (f->inputs[0] != NULL) {
			ms_queue_flush(f->inputs[0]);
		}
	}

	if (f->inputs[1] != NULL) {
		ms_queue_flush(f->inputs[1]);
	}

	return 0;
}

MSVideoSize MSWP8Dis::getVideoSize()
{
	MSVideoSize vs;
	vs.width = mWidth;
	vs.height = mHeight;
	return vs;
}

Mediastreamer2::WP8Video::IVideoRenderer^ MSWP8Dis::getVideoRenderer()
{
	return mRenderer;
}

void MSWP8Dis::setVideoRenderer(IVideoRenderer^ renderer)
{
	mRenderer = renderer;
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
			if (nalu_type == 7)
				*new_sps_pps = checkSPSChange(im) || *new_sps_pps;
			if (nalu_type == 8)
				*new_sps_pps = checkPPSChange(im) || *new_sps_pps;
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

bool MSWP8Dis::checkSPSChange(mblk_t *sps)
{
	bool ret = false;
	if (mSPS) {
		ret = (msgdsize(sps) != msgdsize(mSPS)) || (memcmp(mSPS->b_rptr, mSPS->b_rptr, msgdsize(sps)) != 0);
		if (ret) {
			ms_message("[MSWP8Dis] SPS changed ! %i,%i", msgdsize(sps), msgdsize(mSPS));
			updateSPS(sps);
			updatePPS(nullptr);
		}
	} else {
		ms_message("[MSWP8Dis] Receiving first SPS");
		updateSPS(sps);
		ret = true;
	}
	return ret;
}

bool MSWP8Dis::checkPPSChange(mblk_t *pps)
{
	bool ret = false;
	if (mPPS) {
		ret = (msgdsize(pps) != msgdsize(mPPS)) || (memcmp(mPPS->b_rptr, pps->b_rptr, msgdsize(pps)) != 0);
		if (ret) {
			ms_message("[MSWP8Dis] PPS changed ! %i,%i", msgdsize(pps), msgdsize(mPPS));
			updatePPS(pps);
		}
	} else {
		ms_message("[MSWP8Dis] Receiving first PPS");
		updatePPS(pps);
		ret = true;
	}
	return ret;
}

void MSWP8Dis::updateSPS(mblk_t *sps)
{
	if (mSPS) freemsg(mSPS);
	mSPS = dupb(sps);
	updateVideoSizeFromSPS();
}

void MSWP8Dis::updatePPS(mblk_t *pps)
{
	if (mPPS) freemsg(mPPS);
	if (pps) mPPS = dupb(pps);
	else mPPS = nullptr;
}

void MSWP8Dis::updateVideoSizeFromSPS()
{
	uint8_t *data = (uint8_t *)mSPS->b_rptr;
	uint32_t bit_offset = 8;	// Skip nal header, we know if it is only 1 byte long since it is an SPS
	uint8_t i;
	uint8_t profile_idc;
	uint8_t chroma_format_idc;
	uint8_t seq_scaling_matrix_present_flag;
	uint8_t pic_order_cnt_type;
	uint8_t num_ref_frames_in_pic_order_cnt_cycle;
	uint8_t pic_width_in_mbs_minus1;
	uint8_t pic_height_in_map_units_minus1;
	uint8_t dummy;

	profile_idc = get_u8_at_bit_offset(data, &bit_offset);
	dummy = get_u8_at_bit_offset(data, &bit_offset); // constraint_set*
	dummy = get_u8_at_bit_offset(data, &bit_offset); // level_idc
	dummy = get_ue_at_bit_offset(data, &bit_offset); // seq_parameter_set_id
	if ((profile_idc == 100) || (profile_idc == 110) || (profile_idc == 122) || (profile_idc == 244) || (profile_idc == 44)
		|| (profile_idc == 83) || (profile_idc == 86) || (profile_idc == 118) || (profile_idc == 128)) {
		chroma_format_idc = get_ue_at_bit_offset(data, &bit_offset);
		if (chroma_format_idc == 3) {
			dummy = get_u1_at_bit_offset(data, &bit_offset);
		}
		dummy = get_ue_at_bit_offset(data, &bit_offset); // bit_depth_luma_minus8
		dummy = get_ue_at_bit_offset(data, &bit_offset); // bit_depth_chroma_minus8
		dummy = get_u1_at_bit_offset(data, &bit_offset); // qpprime_y_zero_transform_bypass_flag
		seq_scaling_matrix_present_flag = get_u1_at_bit_offset(data, &bit_offset);
		if (seq_scaling_matrix_present_flag) {
			for (i = 0; i < ((chroma_format_idc != 3) ? 8 : 12); i++) {
				dummy = get_u1_at_bit_offset(data, &bit_offset); // seq_scaling_list_present_flag[i]
			}
		}
	}
	dummy = get_ue_at_bit_offset(data, &bit_offset); // log2_max_frame_num_minus4
	pic_order_cnt_type = get_ue_at_bit_offset(data, &bit_offset);
	if (pic_order_cnt_type == 0) {
		dummy = get_ue_at_bit_offset(data, &bit_offset); // log2_max_pic_order_cnt_lsb_minus4
	} else if (pic_order_cnt_type == 1) {
		dummy = get_u1_at_bit_offset(data, &bit_offset); // delta_pic_order_always_zero_flag
		dummy = get_ue_at_bit_offset(data, &bit_offset); // offset_for_non_ref_pic
		dummy = get_ue_at_bit_offset(data, &bit_offset); // offset_for_top_to_bottom_field
		num_ref_frames_in_pic_order_cnt_cycle = get_ue_at_bit_offset(data, &bit_offset);
		for (i = 0; i < num_ref_frames_in_pic_order_cnt_cycle; i++) {
			dummy = get_ue_at_bit_offset(data, &bit_offset); // offset_for_ref_frame[i]
		}
	}
	dummy = get_ue_at_bit_offset(data, &bit_offset); // max_num_ref_frames
	dummy = get_u1_at_bit_offset(data, &bit_offset); // gaps_in_frame_num_value_allowed_flag
	pic_width_in_mbs_minus1 = get_ue_at_bit_offset(data, &bit_offset);
	pic_height_in_map_units_minus1 = get_ue_at_bit_offset(data, &bit_offset);
 	mWidth = (pic_width_in_mbs_minus1 + 1) * 16;
 	mHeight = (pic_height_in_map_units_minus1 + 1) * 16;
	ms_message("[MSWP8Dis] Change video size from SPS: %ux%u", mWidth, mHeight);
	MS_UNUSED(dummy);
}
