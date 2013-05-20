/*
mswp8vid.cpp

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


#include "mediastreamer2/msfilter.h"
#include "mediastreamer2/msvideo.h"
#include "mediastreamer2/mswebcam.h"

#include "mswp8cap.h"
#include "mswp8dis.h"

using namespace mswp8vid;


/******************************************************************************
 * Methods to (de)initialize and run the WP8 video capture filter             *
 *****************************************************************************/

static void ms_wp8cap_read_init(MSFilter *f) {
	MSWP8Cap *r = new MSWP8Cap();
	f->data = r;
}

static void ms_wp8cap_read_preprocess(MSFilter *f) {
	MSWP8Cap *r = static_cast<MSWP8Cap *>(f->data);
	r->activate();
}

static void ms_wp8cap_read_process(MSFilter *f) {
	MSWP8Cap *r = static_cast<MSWP8Cap *>(f->data);
	if (!r->isStarted()) {
		r->start();
	}
	r->feed(f);
}

static void ms_wp8cap_read_postprocess(MSFilter *f) {
	MSWP8Cap *r = static_cast<MSWP8Cap *>(f->data);
	r->stop();
	r->deactivate();
}

static void ms_wp8cap_read_uninit(MSFilter *f) {
	MSWP8Cap *r = static_cast<MSWP8Cap *>(f->data);
	delete r;
}


/******************************************************************************
 * Methods to configure the Windows Phone 8 video capture filter              *
 *****************************************************************************/

static int ms_wp8cap_get_fps(MSFilter *f, void *arg) {
	MSWP8Cap *r = static_cast<MSWP8Cap *>(f->data);
	*((float *)arg) = r->getFps();
	return 0;
}

static int ms_wp8cap_set_fps(MSFilter *f, void *arg) {
	MSWP8Cap *r = static_cast<MSWP8Cap *>(f->data);
	r->setFps(*((float *)arg));
	return 0;
}

static int ms_wp8cap_get_pix_fmt(MSFilter *f, void *arg) {
	MS_UNUSED(f);
	MSPixFmt *fmt = static_cast<MSPixFmt *>(arg);
	*fmt = MS_PIX_FMT_UNKNOWN;
	return 0;
}

static int ms_wp8cap_get_vsize(MSFilter *f, void *arg) {
	MSWP8Cap *r = static_cast<MSWP8Cap *>(f->data);
	MSVideoSize *vs = static_cast<MSVideoSize *>(arg);
	*vs = r->getVideoSize();
	return 0;
}

static int ms_wp8cap_set_vsize(MSFilter *f, void *arg) {
	MSWP8Cap *r = static_cast<MSWP8Cap *>(f->data);
	MSVideoSize *vs = static_cast<MSVideoSize *>(arg);
	r->setVideoSize(*vs);
	return 0;
}

static int ms_wp8cap_get_bitrate(MSFilter *f, void *arg) {
	MSWP8Cap *r = static_cast<MSWP8Cap *>(f->data);
	*((int *)arg) = r->getBitrate();
	return 0;
}

static int ms_wp8cap_set_bitrate(MSFilter *f, void *arg) {
	MSWP8Cap *r = static_cast<MSWP8Cap *>(f->data);
	r->setBitrate(*((int *)arg));
	return 0;
}

static int ms_wp8cap_req_vfu(MSFilter *f, void *arg) {
	MS_UNUSED(arg);
	MSWP8Cap *r = static_cast<MSWP8Cap *>(f->data);
	r->requestIdrFrame();
	return 0;
}

static MSFilterMethod ms_wp8cap_read_methods[] = {
	{	MS_FILTER_GET_FPS,			ms_wp8cap_get_fps		},
	{	MS_FILTER_SET_FPS,			ms_wp8cap_set_fps		},
	{	MS_FILTER_GET_PIX_FMT,		ms_wp8cap_get_pix_fmt	},
	{	MS_FILTER_GET_VIDEO_SIZE,	ms_wp8cap_get_vsize		},
	{	MS_FILTER_SET_VIDEO_SIZE,	ms_wp8cap_set_vsize		},
	{	MS_FILTER_GET_BITRATE,		ms_wp8cap_get_bitrate	},
	{	MS_FILTER_SET_BITRATE,		ms_wp8cap_set_bitrate	},
	{	MS_FILTER_REQ_VFU,			ms_wp8cap_req_vfu		},
	{	MS_VIDEO_ENCODER_REQ_VFU,	ms_wp8cap_req_vfu		},
	{	0,							NULL					}
};


/******************************************************************************
 * Definition of the Windows Phone 8 video capture filter                     *
 *****************************************************************************/

#define MS_WP8CAP_READ_ID			MS_FILTER_PLUGIN_ID
#define MS_WP8CAP_READ_NAME			"MSWP8CapRead"
#define MS_WP8CAP_READ_DESCRIPTION	"Windows Phone 8 video capture"
#define MS_WP8CAP_READ_CATEGORY		MS_FILTER_OTHER
#define MS_WP8CAP_READ_ENC_FMT		NULL
#define MS_WP8CAP_READ_NINPUTS		0
#define MS_WP8CAP_READ_NOUTPUTS		1
#define MS_WP8CAP_READ_FLAGS		0

#ifndef _MSC_VER

MSFilterDesc ms_wp8cap_read_desc = {
	.id = MS_WP8CAP_READ_ID,
	.name = MS_WP8CAP_READ_NAME,
	.text = MS_WP8CAP_READ_DESCRIPTION,
	.category = MS_WP8CAP_READ_CATEGORY,
	.enc_fmt = MS_WP8CAP_READ_ENC_FMT,
	.ninputs = MS_WP8CAP_READ_NINPUTS,
	.noutputs = MS_WP8CAP_READ_NOUTPUTS,
	.init = ms_wp8cap_read_init,
	.preprocess = ms_wp8cap_read_preprocess,
	.process = ms_wp8cap_read_process,
	.postprocess = ms_wp8cap_read_postprocess,
	.uninit = ms_wp8cap_read_uninit,
	.methods = ms_wp8cap_read_methods,
	.flags = MS_WP8CAP_READ_FLAGS
};

#else

MSFilterDesc ms_wp8cap_read_desc = {
	MS_WP8CAP_READ_ID,
	MS_WP8CAP_READ_NAME,
	MS_WP8CAP_READ_DESCRIPTION,
	MS_WP8CAP_READ_CATEGORY,
	MS_WP8CAP_READ_ENC_FMT,
	MS_WP8CAP_READ_NINPUTS,
	MS_WP8CAP_READ_NOUTPUTS,
	ms_wp8cap_read_init,
	ms_wp8cap_read_preprocess,
	ms_wp8cap_read_process,
	ms_wp8cap_read_postprocess,
	ms_wp8cap_read_uninit,
	ms_wp8cap_read_methods,
	MS_WP8CAP_READ_FLAGS
};

#endif

MS_FILTER_DESC_EXPORT(ms_wp8cap_read_desc)



/******************************************************************************
 * Definition of the Windows Phone 8 video camera detection                   *
 *****************************************************************************/

static void ms_wp8cap_detect(MSWebCamManager *m);
static MSFilter *ms_wp8cap_create_reader(MSWebCam *cam);

static MSWebCamDesc ms_wp8cap_desc = {
	"MSWP8Cap",
	ms_wp8cap_detect,
	NULL,
	ms_wp8cap_create_reader,
	NULL
};

static void ms_wp8cap_detect(MSWebCamManager *m) {
	MSWP8Cap::detectCameras(m, &ms_wp8cap_desc);
}

static MSFilter *ms_wp8cap_create_reader(MSWebCam *cam) {
	MSFilter *f = ms_filter_new_from_desc(&ms_wp8cap_read_desc);
	MSWP8Cap *r = static_cast<MSWP8Cap *>(f->data);
	r->setCameraLocation((uint32)cam->data);
	return f;
}



/******************************************************************************
 * Methods to (de)initialize and run the WP8 video display filter             *
 *****************************************************************************/

static void ms_wp8dis_init(MSFilter *f) {
	MSWP8Dis *w = new MSWP8Dis();
	f->data = w;
}

static void ms_wp8dis_preprocess(MSFilter *f) {
	MSWP8Dis *w = static_cast<MSWP8Dis *>(f->data);
	w->activate();
}

static void ms_wp8dis_process(MSFilter *f) {
	MSWP8Dis *w = static_cast<MSWP8Dis *>(f->data);
	if (!w->isStarted()) {
		w->start();
	}
	if (w->isStarted()) {
		w->feed(f);
	}
}

static void ms_wp8dis_postprocess(MSFilter *f) {
	MSWP8Dis *w = static_cast<MSWP8Dis *>(f->data);
	w->stop();
	w->deactivate();
}

static void ms_wp8dis_uninit(MSFilter *f) {
	MSWP8Dis *w = static_cast<MSWP8Dis *>(f->data);
	delete w;
}


/******************************************************************************
 * Methods to configure the Windows Phone 8 video display filter              *
 *****************************************************************************/

static MSFilterMethod ms_wp8dis_methods[] = {
	{	0,							NULL					}
};


/******************************************************************************
 * Definition of the Windows Phone 8 video display filter                     *
 *****************************************************************************/

#define MS_WP8DIS_ID			MS_FILTER_PLUGIN_ID
#define MS_WP8DIS_NAME			"MSWP8Dis"
#define MS_WP8DIS_DESCRIPTION	"Windows Phone 8 video display"
#define MS_WP8DIS_CATEGORY		MS_FILTER_OTHER
#define MS_WP8DIS_ENC_FMT		NULL
#define MS_WP8DIS_NINPUTS		1
#define MS_WP8DIS_NOUTPUTS		0
#define MS_WP8DIS_FLAGS			0

#ifndef _MSC_VER

MSFilterDesc ms_wp8dis_desc = {
	.id = MS_WP8DIS_ID,
	.name = MS_WP8DIS_NAME,
	.text = MS_WP8DIS_DESCRIPTION,
	.category = MS_WP8DIS_CATEGORY,
	.enc_fmt = MS_WP8DIS_ENC_FMT,
	.ninputs = MS_WP8DIS_NINPUTS,
	.noutputs = MS_WP8DIS_NOUTPUTS,
	.init = ms_wp8dis_init,
	.preprocess = ms_wp8dis_preprocess,
	.process = ms_wp8dis_process,
	.postprocess = ms_wp8dis_postprocess,
	.uninit = ms_wp8dis_uninit,
	.methods = ms_wp8dis_methods,
	.flags = MS_WP8DIS_FLAGS
};

#else

MSFilterDesc ms_wp8dis_desc = {
	MS_WP8DIS_ID,
	MS_WP8DIS_NAME,
	MS_WP8DIS_DESCRIPTION,
	MS_WP8DIS_CATEGORY,
	MS_WP8DIS_ENC_FMT,
	MS_WP8DIS_NINPUTS,
	MS_WP8DIS_NOUTPUTS,
	ms_wp8dis_init,
	ms_wp8dis_preprocess,
	ms_wp8dis_process,
	ms_wp8dis_postprocess,
	ms_wp8dis_uninit,
	ms_wp8dis_methods,
	MS_WP8DIS_FLAGS
};

#endif

MS_FILTER_DESC_EXPORT(ms_wp8dis_desc)




#ifdef _MSC_VER
#define MS_PLUGIN_DECLARE(type) extern "C" __declspec(dllexport) type
#else
#define MS_PLUGIN_DECLARE(type) extern "C" type
#endif

MS_PLUGIN_DECLARE(void) libmswp8vid_init(void) {
	MSWebCamManager *manager = ms_web_cam_manager_get();
	ms_web_cam_manager_register_desc(manager, &ms_wp8cap_desc);
	ms_filter_register(&ms_wp8dis_desc);
	ms_message("libmswp8vid plugin loaded");
}
