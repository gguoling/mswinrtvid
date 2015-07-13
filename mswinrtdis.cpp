/*
mswinrtdis.cpp

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


#include "mswinrtdis.h"
#include "VideoBuffer.h"

using namespace Microsoft::WRL;
using namespace mswinrtvid;
//using namespace Mediastreamer2::WinRTVideo;



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



bool MSWinRTDis::smInstantiated = false;


MSWinRTDis::MSWinRTDis()
	: mIsInitialized(false), mIsActivated(false), mIsStarted(false), mWidth(MS_VIDEO_SIZE_CIF_W), mHeight(MS_VIDEO_SIZE_CIF_H),
	mRfc3984Unpacker(nullptr), mBitstreamSize(65536), mBitstream(nullptr), mSPS(nullptr), mPPS(nullptr),
#ifdef MS2_WINDOWS_PHONE
	mRenderer(nullptr),
#endif
	mFirstFrameReceived(false)
{
	if (smInstantiated) {
		ms_error("[MSWinRTDis] A video display filter is already instantiated. A second one can not be created.");
		return;
	}

	mIsInitialized = true;
	smInstantiated = true;
}

MSWinRTDis::~MSWinRTDis()
{
	stop();
	smInstantiated = false;
}

int MSWinRTDis::activate()
{
	if (!mIsInitialized) return -1;

	if (mPixFmt == MS_H264) mRfc3984Unpacker = rfc3984_new();
	mIsActivated = true;
	return 0;
}

int MSWinRTDis::deactivate()
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
	if (mBitstream) {
		ms_free(mBitstream);
		mBitstream = nullptr;
	}
	mIsActivated = false;
	return 0;
}

void MSWinRTDis::start()
{
	if (!mIsStarted && mIsActivated) {
		mIsStarted = true;
		Platform::String^ format;
		if (mPixFmt == MS_H264)
			format = ref new Platform::String(L"H264");
		else
			format = ref new Platform::String(L"YV12");
#ifdef MS2_WINDOWS_PHONE
		if (mRenderer != nullptr) {
			ms_message("[MSWinRTDis] Start renderer %s - %dx%d", ms_pix_fmt_to_string(mPixFmt), mWidth, mHeight);
			mRenderer->Start(format, mWidth, mHeight);
		}
#endif
	}
}

void MSWinRTDis::stop()
{
	if (mIsStarted) {
		mIsStarted = false;
		mFirstFrameReceived = false;
#ifdef MS2_WINDOWS_PHONE
		if (mRenderer != nullptr) {
			ms_message("[MSWinRTDis] Stop renderer");
			mRenderer->Stop();
		}
#endif
	}
}

int MSWinRTDis::feed(MSFilter *f)
{
	mblk_t *im;

	if (mIsStarted) {
		while ((im = ms_queue_get(f->inputs[0])) != NULL) {
			int size = 0;
			if (mPixFmt == MS_H264) {
				MSQueue nalus;
				ms_queue_init(&nalus);
				rfc3984_unpack(mRfc3984Unpacker, im, &nalus);
				if (!ms_queue_empty(&nalus)) {
					bool need_reinit = false;
					size = nalusToFrame(&nalus, &need_reinit);
					if (need_reinit) {
#ifdef MS2_WINDOWS_PHONE
						if (mRenderer != nullptr) {
							Platform::String^ format = ref new Platform::String(L"H264");
							ms_message("[MSWinRTDis] Change renderer format: %s - %dx%d", "H264", mWidth, mHeight);
							mRenderer->ChangeFormat(format, mWidth, mHeight);
						}
#endif
					}
				}
			}
			else {
				MSPicture buf;
				ms_yuv_buf_init_from_mblk(&buf, im);
				size = (buf.w * buf.h * 3) / 2;
				if (!mBitstream) mBitstream = (uint8_t *)ms_malloc(size);
				int ysize = buf.w * buf.h;
				int usize = ysize / 4;
				memcpy(mBitstream, buf.planes[0], ysize);
				memcpy(mBitstream + ysize, buf.planes[2], usize);
				memcpy(mBitstream + ysize + usize, buf.planes[1], usize);
				freemsg(im);
#ifdef MS2_WINDOWS_PHONE
				if ((mRenderer != nullptr) && ((buf.w != mWidth) || (buf.h != mHeight))) {
					mWidth = buf.w;
					mHeight = buf.h;
					if (mFirstFrameReceived) {
						stop();
						start();
					}
					else {
						Platform::String^ format = ref new Platform::String(L"YV12");
						mRenderer->ChangeFormat(format, mWidth, mHeight);
					}
				}
#endif
			}
#ifdef MS2_WINDOWS_PHONE
			if ((size > 0) && (mRenderer != nullptr)) {
				if (!mFirstFrameReceived) {
					mFirstFrameReceived = true;
					mRenderer->FirstFrameReceived();
				}
				if (mRenderer->Dispatcher != nullptr) {
					ComPtr<VideoBuffer> spVideoBuffer = NULL;
					MakeAndInitialize<VideoBuffer>(&spVideoBuffer, mBitstream, size);
					mRenderer->Dispatcher->OnSampleReceived(VideoBuffer::GetIBuffer(spVideoBuffer), f->ticker->time * 10000LL);
				}
			}
#endif
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

MSVideoSize MSWinRTDis::getVideoSize()
{
	MSVideoSize vs;
	vs.width = mWidth;
	vs.height = mHeight;
	return vs;
}

void MSWinRTDis::setVideoSize(MSVideoSize vs)
{
	mWidth = vs.width;
	mHeight = vs.height;
}

#ifdef MS2_WINDOWS_PHONE
Mediastreamer2::WinRTVideo::IVideoRenderer^ MSWinRTDis::getVideoRenderer()
{
	return mRenderer;
}

void MSWinRTDis::setVideoRenderer(IVideoRenderer^ renderer)
{
	mRenderer = renderer;
}
#endif

int MSWinRTDis::nalusToFrame(MSQueue *nalus, bool *new_sps_pps)
{
	mblk_t *im;
	uint8_t *dst, *src, *end;
	int nal_len;
	bool start_picture = true;
	uint8_t nalu_type;

	if (!mBitstream) mBitstream = (uint8_t *)ms_malloc(mBitstreamSize);
	dst = mBitstream;
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

void MSWinRTDis::enlargeBitstream(int newSize)
{
	mBitstreamSize = newSize;
	mBitstream = (uint8_t *)ms_realloc(mBitstream, mBitstreamSize);
}

bool MSWinRTDis::checkSPSChange(mblk_t *sps)
{
	bool ret = false;
	if (mSPS) {
		ret = (msgdsize(sps) != msgdsize(mSPS)) || (memcmp(mSPS->b_rptr, mSPS->b_rptr, msgdsize(sps)) != 0);
		if (ret) {
			ms_message("[MSWinRTDis] SPS changed ! %i,%i", msgdsize(sps), msgdsize(mSPS));
			updateSPS(sps);
			updatePPS(nullptr);
		}
	} else {
		ms_message("[MSWinRTDis] Receiving first SPS");
		updateSPS(sps);
		ret = true;
	}
	return ret;
}

bool MSWinRTDis::checkPPSChange(mblk_t *pps)
{
	bool ret = false;
	if (mPPS) {
		ret = (msgdsize(pps) != msgdsize(mPPS)) || (memcmp(mPPS->b_rptr, pps->b_rptr, msgdsize(pps)) != 0);
		if (ret) {
			ms_message("[MSWinRTDis] PPS changed ! %i,%i", msgdsize(pps), msgdsize(mPPS));
			updatePPS(pps);
		}
	} else {
		ms_message("[MSWinRTDis] Receiving first PPS");
		updatePPS(pps);
		ret = true;
	}
	return ret;
}

void MSWinRTDis::updateSPS(mblk_t *sps)
{
	if (mSPS) freemsg(mSPS);
	mSPS = dupb(sps);
	updateVideoSizeFromSPS();
}

void MSWinRTDis::updatePPS(mblk_t *pps)
{
	if (mPPS) freemsg(mPPS);
	if (pps) mPPS = dupb(pps);
	else mPPS = nullptr;
}

void MSWinRTDis::updateVideoSizeFromSPS()
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
	ms_message("[MSWinRTDis] Change video size from SPS: %ux%u", mWidth, mHeight);
	MS_UNUSED(dummy);
}