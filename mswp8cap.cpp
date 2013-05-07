/*
mswp8cap.cpp

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

#include "mswp8cap_reader.h"

using namespace mediastreamer2;


/******************************************************************************
 * Methods to (de)initialize and run the WP8 Video capture filter             *
 *****************************************************************************/

static void ms_wp8cap_read_init(MSFilter *f) {
	MSWP8CapReader *r = new MSWP8CapReader();
	f->data = r;
}

static void ms_wp8cap_read_preprocess(MSFilter *f) {
	MSWP8CapReader *r = static_cast<MSWP8CapReader *>(f->data);
	r->activate();
}

static void ms_wp8cap_read_process(MSFilter *f) {
	MSWP8CapReader *r = static_cast<MSWP8CapReader *>(f->data);
	if (!r->isStarted()) {
		r->start();
	}
	r->feed(f);
}

static void ms_wp8cap_read_postprocess(MSFilter *f) {
	MSWP8CapReader *r = static_cast<MSWP8CapReader *>(f->data);
	r->stop();
	r->deactivate();
}

static void ms_wp8cap_read_uninit(MSFilter *f) {
	MSWP8CapReader *r = static_cast<MSWP8CapReader *>(f->data);
	delete r;
}


/******************************************************************************
 * Methods to configure the Windows Phone 8 Video capture filter              *
 *****************************************************************************/

static int ms_wp8cap_set_fps(MSFilter *f, void *arg) {
	MSWP8CapReader *r = static_cast<MSWP8CapReader *>(f->data);
	r->setFps(*((int*)arg));
	return 0;
}

static int ms_wp8cap_get_pix_fmt(MSFilter *f, void *arg) {
	// TODO
	return 0;
}

static int ms_wp8cap_set_vsize(MSFilter *f, void *arg) {
	// TODO
	return 0;
}

static int ms_wp8cap_get_vsize(MSFilter *f, void *arg) {
	// TODO
	return 0;
}

static MSFilterMethod ms_wp8cap_read_methods[] = {
	{	MS_FILTER_SET_FPS,			ms_wp8cap_set_fps		},
	{	MS_FILTER_GET_PIX_FMT,		ms_wp8cap_get_pix_fmt	},
	{	MS_FILTER_SET_VIDEO_SIZE,	ms_wp8cap_set_vsize		},
	{	MS_FILTER_GET_VIDEO_SIZE,	ms_wp8cap_get_vsize		},
	{	0,							NULL					}
};


/******************************************************************************
 * Definition of the Windows Phone 8 Video Capture filter                     *
 *****************************************************************************/

#define MS_WP8CAP_READ_ID			MS_FILTER_PLUGIN_ID
#define MS_WP8CAP_READ_NAME			"MSWP8CapRead"
#define MS_WP8CAP_READ_DESCRIPTION	"Windows Phone 8 Video capture"
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



static void ms_wp8cap_detect(MSWebCamManager *m);
static MSFilter *ms_wp8cap_create_reader(MSWebCam *cam);

static MSWebCamDesc ms_wp8cap_desc = {
	"MSWP8Cap",
	ms_wp8cap_detect,
	NULL,
	ms_wp8cap_create_reader,
	NULL
};

static MSWebCam *ms_wp8cap_new(void) {
	MSWebCam *cam = ms_web_cam_new(&ms_wp8cap_desc);
	cam->name = ms_strdup("WP8 Camera");
	return cam;
}

static void ms_wp8cap_detect(MSWebCamManager *m) {
	MSWebCam *cam = ms_wp8cap_new();
	ms_web_cam_manager_prepend_cam(m, cam);
}

static MSFilter *ms_wp8cap_create_reader(MSWebCam *cam) {
	MS_UNUSED(cam);
	return ms_filter_new_from_desc(&ms_wp8cap_read_desc);
}




#ifdef _MSC_VER
#define MS_PLUGIN_DECLARE(type) extern "C" __declspec(dllexport) type
#else
#define MS_PLUGIN_DECLARE(type) extern "C" type
#endif

MS_PLUGIN_DECLARE(void) libmswp8cap_init(void) {
	MSWebCamManager *manager = ms_web_cam_manager_get();
	ms_web_cam_manager_register_desc(manager, &ms_wp8cap_desc);
	ms_message("libmswp8cap plugin loaded");
}
