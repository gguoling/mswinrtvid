/*
mswinrtvid.cpp

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

#include "mswinrtcap.h"
#include "mswinrtdis.h"
#include "IVideoRenderer.h"


using namespace mswinrtvid;
//using namespace Mediastreamer2::WinRTVideo;


/******************************************************************************
 * Methods to (de)initialize and run the WP8 video capture filter             *
 *****************************************************************************/

static void ms_winrtcap_read_init(MSFilter *f) {
	MSWinRTCap *r = new MSWinRTCap();
	f->data = r;
}

static void ms_winrtcap_read_preprocess(MSFilter *f) {
	MSWinRTCap *r = static_cast<MSWinRTCap *>(f->data);
	r->activate();
}

static void ms_winrtcap_read_process(MSFilter *f) {
	MSWinRTCap *r = static_cast<MSWinRTCap *>(f->data);
	if (!r->isStarted()) {
		r->start();
	}
	r->feed(f);
}

static void ms_winrtcap_read_postprocess(MSFilter *f) {
	MSWinRTCap *r = static_cast<MSWinRTCap *>(f->data);
	r->stop();
	r->deactivate();
}

static void ms_winrtcap_read_uninit(MSFilter *f) {
	MSWinRTCap *r = static_cast<MSWinRTCap *>(f->data);
	delete r;
}


/******************************************************************************
 * Methods to configure the Windows Phone 8 video capture filter              *
 *****************************************************************************/

static int ms_winrtcap_get_fps(MSFilter *f, void *arg) {
	MSWinRTCap *r = static_cast<MSWinRTCap *>(f->data);
	*((float *)arg) = r->getFps();
	return 0;
}

static int ms_winrtcap_set_fps(MSFilter *f, void *arg) {
	MSWinRTCap *r = static_cast<MSWinRTCap *>(f->data);
	r->setFps(*((float *)arg));
	return 0;
}

static int ms_winrtcap_get_pix_fmt(MSFilter *f, void *arg) {
	MSWinRTCap *r = static_cast<MSWinRTCap *>(f->data);
	MSPixFmt *fmt = static_cast<MSPixFmt *>(arg);
	*fmt = r->getPixFmt();
	return 0;
}

static int ms_winrtcap_set_pix_fmt(MSFilter *f, void *arg) {
	MSWinRTCap *r = static_cast<MSWinRTCap *>(f->data);
	MSPixFmt *fmt = static_cast<MSPixFmt *>(arg);
	r->setPixFmt(*fmt);
	return 0;
}

static int ms_winrtcap_get_vsize(MSFilter *f, void *arg) {
	MSWinRTCap *r = static_cast<MSWinRTCap *>(f->data);
	MSVideoSize *vs = static_cast<MSVideoSize *>(arg);
	*vs = r->getVideoSize();
	return 0;
}

static int ms_winrtcap_set_vsize(MSFilter *f, void *arg) {
	MSWinRTCap *r = static_cast<MSWinRTCap *>(f->data);
	MSVideoSize *vs = static_cast<MSVideoSize *>(arg);
	r->setVideoSize(*vs);
	return 0;
}

static int ms_winrtcap_get_bitrate(MSFilter *f, void *arg) {
	MSWinRTCap *r = static_cast<MSWinRTCap *>(f->data);
	*((int *)arg) = r->getBitrate();
	return 0;
}

static int ms_winrtcap_set_bitrate(MSFilter *f, void *arg) {
	MSWinRTCap *r = static_cast<MSWinRTCap *>(f->data);
	r->setBitrate(*((int *)arg));
	return 0;
}

static int ms_winrtcap_req_vfu(MSFilter *f, void *arg) {
	MS_UNUSED(arg);
	MSWinRTCap *r = static_cast<MSWinRTCap *>(f->data);
	r->requestIdrFrame();
	return 0;
}

static int ms_winrtcap_get_configuration_list(MSFilter *f, void *arg) {
	MSWinRTCap *r = static_cast<MSWinRTCap *>(f->data);
	const MSVideoConfiguration **vconf_list = (const MSVideoConfiguration **)arg;
	*vconf_list = r->getConfigurationList();
	return 0;
}

static int ms_winrtcap_set_configuration(MSFilter *f, void *arg) {
	MSWinRTCap *r = static_cast<MSWinRTCap *>(f->data);
	const MSVideoConfiguration *vconf = (const MSVideoConfiguration *)arg;
	r->setConfiguration(vconf);
	return 0;
}

static int ms_winrtcap_get_camera_sensor_rotation(MSFilter *f, void *arg) {
	MSWinRTCap *r = static_cast<MSWinRTCap *>(f->data);
	*((int *)arg) = r->getCameraSensorRotation();
	return 0;
}

static int ms_winrtcap_set_device_orientation(MSFilter *f, void *arg) {
	MSWinRTCap *r = static_cast<MSWinRTCap *>(f->data);
	r->setDeviceOrientation(*((int *)arg));
	return 0;
}

static MSFilterMethod ms_winrtcap_read_methods[] = {
	{	MS_FILTER_GET_FPS,			ms_winrtcap_get_fps		},
	{	MS_FILTER_SET_FPS,			ms_winrtcap_set_fps		},
	{	MS_FILTER_GET_PIX_FMT,		ms_winrtcap_get_pix_fmt	},
	{	MS_FILTER_SET_PIX_FMT,		ms_winrtcap_set_pix_fmt	},
	{	MS_FILTER_GET_VIDEO_SIZE,	ms_winrtcap_get_vsize		},
	{	MS_FILTER_SET_VIDEO_SIZE,	ms_winrtcap_set_vsize		},
	{	MS_FILTER_GET_BITRATE,		ms_winrtcap_get_bitrate	},
	{	MS_FILTER_SET_BITRATE,		ms_winrtcap_set_bitrate	},
	{	MS_FILTER_REQ_VFU,			ms_winrtcap_req_vfu		},
	{	MS_VIDEO_ENCODER_REQ_VFU,	ms_winrtcap_req_vfu		},
	{	MS_VIDEO_ENCODER_GET_CONFIGURATION_LIST, ms_winrtcap_get_configuration_list },
	{	MS_VIDEO_ENCODER_SET_CONFIGURATION,	ms_winrtcap_set_configuration },
	{	MS_VIDEO_CAPTURE_GET_CAMERA_SENSOR_ROTATION,	ms_winrtcap_get_camera_sensor_rotation	},
	{	MS_VIDEO_CAPTURE_SET_DEVICE_ORIENTATION,	ms_winrtcap_set_device_orientation	},
	{	0,							NULL					}
};


/******************************************************************************
 * Definition of the Windows Phone 8 video capture filter                     *
 *****************************************************************************/

#define MS_WINRTCAP_READ_ID			MS_FILTER_PLUGIN_ID
#define MS_WINRTCAP_READ_NAME			"MSWinRTCap"
#define MS_WINRTCAP_READ_DESCRIPTION	"WinRT video capture"
#define MS_WINRTCAP_READ_CATEGORY		MS_FILTER_ENCODING_CAPTURER
#define MS_WINRTCAP_READ_ENC_FMT		"H264"
#define MS_WINRTCAP_READ_NINPUTS		0
#define MS_WINRTCAP_READ_NOUTPUTS		1
#define MS_WINRTCAP_READ_FLAGS		0

MSFilterDesc ms_winrtcap_read_desc = {
	MS_WINRTCAP_READ_ID,
	MS_WINRTCAP_READ_NAME,
	MS_WINRTCAP_READ_DESCRIPTION,
	MS_WINRTCAP_READ_CATEGORY,
	MS_WINRTCAP_READ_ENC_FMT,
	MS_WINRTCAP_READ_NINPUTS,
	MS_WINRTCAP_READ_NOUTPUTS,
	ms_winrtcap_read_init,
	ms_winrtcap_read_preprocess,
	ms_winrtcap_read_process,
	ms_winrtcap_read_postprocess,
	ms_winrtcap_read_uninit,
	ms_winrtcap_read_methods,
	MS_WINRTCAP_READ_FLAGS
};

MS_FILTER_DESC_EXPORT(ms_winrtcap_read_desc)



/******************************************************************************
 * Definition of the Windows Phone 8 video camera detection                   *
 *****************************************************************************/

static void ms_winrtcap_detect(MSWebCamManager *m);

static MSFilter *ms_winrtcap_create_reader(MSWebCam *cam) {
	MSFilter *f = ms_filter_new_from_desc(&ms_winrtcap_read_desc);
	MSWinRTCap *r = static_cast<MSWinRTCap *>(f->data);
	r->setCameraLocation((uint32)cam->data);
	return f;
}

static bool_t ms_winrtcap_encode_to_mime_type(MSWebCam *cam, const char *mime_type) {
	MS_UNUSED(cam);
	if (strcmp(mime_type, "H264") == 0) return TRUE;
	return FALSE;
}

static MSWebCamDesc ms_winrtcap_desc = {
	"MSWinRTCap",
	ms_winrtcap_detect,
	NULL,
	ms_winrtcap_create_reader,
	NULL,
	ms_winrtcap_encode_to_mime_type
};

static void ms_winrtcap_detect(MSWebCamManager *m) {
	MSWinRTCap::detectCameras(m, &ms_winrtcap_desc);
}



/******************************************************************************
 * Methods to (de)initialize and run the WP8 video display filter             *
 *****************************************************************************/

static void ms_winrtdis_init(MSFilter *f) {
	MSWinRTDis *w = new MSWinRTDis();
	f->data = w;
}

static void ms_winrtdis_preprocess(MSFilter *f) {
	MSWinRTDis *w = static_cast<MSWinRTDis *>(f->data);
	w->activate();
}

static void ms_winrtdis_process(MSFilter *f) {
	MSWinRTDis *w = static_cast<MSWinRTDis *>(f->data);
	if (!w->isStarted()) {
		w->start();
	}
	if (w->isStarted()) {
		w->feed(f);
	}
}

static void ms_winrtdis_postprocess(MSFilter *f) {
	MSWinRTDis *w = static_cast<MSWinRTDis *>(f->data);
	w->stop();
	w->deactivate();
}

static void ms_winrtdis_uninit(MSFilter *f) {
	MSWinRTDis *w = static_cast<MSWinRTDis *>(f->data);
	delete w;
}


/******************************************************************************
 * Methods to configure the Windows Phone 8 video display filter              *
 *****************************************************************************/

static int ms_winrtdis_get_vsize(MSFilter *f, void *arg) {
	MSWinRTDis *w = static_cast<MSWinRTDis *>(f->data);
	*((MSVideoSize *)arg) = w->getVideoSize();
	return 0;
}

static int ms_winrtdis_set_vsize(MSFilter *f, void *arg) {
	MSWinRTDis *w = static_cast<MSWinRTDis *>(f->data);
	MSVideoSize *vs = static_cast<MSVideoSize *>(arg);
	w->setVideoSize(*vs);
	return 0;
}

static int ms_winrtdis_set_pix_fmt(MSFilter *f, void *arg) {
	MSWinRTDis *w = static_cast<MSWinRTDis *>(f->data);
	MSPixFmt *fmt = static_cast<MSPixFmt *>(arg);
	w->setPixFmt(*fmt);
	return 0;
}

static int ms_winrtdis_support_rendering(MSFilter *f, void *arg) {
	MS_UNUSED(f);
	MSVideoDisplayDecodingSupport *decoding_support = static_cast<MSVideoDisplayDecodingSupport *>(arg);
	if (strcmp(decoding_support->mime_type, "H264") == 0) decoding_support->supported = TRUE;
	else decoding_support->supported = FALSE;
	return 0;
}

template <class T> class RefToPtrProxy
{
public:
	RefToPtrProxy(T obj) : mObj(obj) {}
	~RefToPtrProxy() { mObj = nullptr; }
	T Ref() { return mObj; }
private:
	T mObj;
};

static int ms_winrtdis_set_native_window_id(MSFilter *f, void *arg) {
	MSWinRTDis *w = static_cast<MSWinRTDis *>(f->data);
	unsigned long *ptr = (unsigned long *)arg;
#ifdef MS2_WINDOWS_PHONE
	RefToPtrProxy<IVideoRenderer^> *proxy = reinterpret_cast< RefToPtrProxy<IVideoRenderer^> *>(*ptr);
	IVideoRenderer^ renderer = (proxy) ? proxy->Ref() : nullptr;
	w->setVideoRenderer(renderer);
#endif
	return 0;
}

static MSFilterMethod ms_winrtdis_methods[] = {
	{	MS_FILTER_GET_VIDEO_SIZE,				ms_winrtdis_get_vsize				},
	{	MS_FILTER_SET_VIDEO_SIZE,				ms_winrtdis_set_vsize				},
	{	MS_FILTER_SET_PIX_FMT,					ms_winrtdis_set_pix_fmt			},
	{	MS_VIDEO_DECODER_SUPPORT_RENDERING,		ms_winrtdis_support_rendering		},
	{	MS_VIDEO_DISPLAY_SET_NATIVE_WINDOW_ID,	ms_winrtdis_set_native_window_id	},
	{	0,										NULL							}
};


/******************************************************************************
 * Definition of the Windows Phone 8 video display filter                     *
 *****************************************************************************/

#define MS_WINRTDIS_ID			MS_FILTER_PLUGIN_ID
#define MS_WINRTDIS_NAME			"MSWinRTDis"
#define MS_WINRTDIS_DESCRIPTION	"Windows Phone 8 video display"
#define MS_WINRTDIS_CATEGORY		MS_FILTER_DECODER_RENDERER
#define MS_WINRTDIS_ENC_FMT		"H264"
#define MS_WINRTDIS_NINPUTS		2
#define MS_WINRTDIS_NOUTPUTS		0
#define MS_WINRTDIS_FLAGS			0

MSFilterDesc ms_winrtdis_desc = {
	MS_WINRTDIS_ID,
	MS_WINRTDIS_NAME,
	MS_WINRTDIS_DESCRIPTION,
	MS_WINRTDIS_CATEGORY,
	MS_WINRTDIS_ENC_FMT,
	MS_WINRTDIS_NINPUTS,
	MS_WINRTDIS_NOUTPUTS,
	ms_winrtdis_init,
	ms_winrtdis_preprocess,
	ms_winrtdis_process,
	ms_winrtdis_postprocess,
	ms_winrtdis_uninit,
	ms_winrtdis_methods,
	MS_WINRTDIS_FLAGS
};

MS_FILTER_DESC_EXPORT(ms_winrtdis_desc)




extern "C" __declspec(dllexport) void libmswinrtvid_init(void) {
	MSWebCamManager *manager = ms_web_cam_manager_get();
	ms_web_cam_manager_register_desc(manager, &ms_winrtcap_desc);
	ms_filter_register(&ms_winrtcap_read_desc);
	ms_filter_register(&ms_winrtdis_desc);
	ms_message("libmswinrtvid plugin loaded");
}
