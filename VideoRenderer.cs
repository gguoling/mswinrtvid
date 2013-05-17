using Microsoft.Phone.Media;
using System;
using System.Diagnostics;
using System.Windows;

namespace mswp8vid
{
    internal class VideoRenderer
    {
        internal VideoRenderer()
        {
            Random rand = new Random();
            this.streamId = rand.Next(0, 65535);
            mswp8vid.Globals.Instance.renderStarted += Start;
            mswp8vid.Globals.Instance.renderStopped += Stop;
            mswp8vid.Globals.Instance.renderFormatChanged += ChangeFormat;
        }

        public Uri RemoteStreamUri
        {
            get
            {
                return new Uri("ms-media-stream-id:MediaStreamer-" + this.streamId);
            }
        }

        public static Uri FrontFacingCameraStreamUri = new Uri("ms-media-stream-id:camera-FrontFacing");
        public static Uri RearFacingCameraStreamUri = new Uri("ms-media-stream-id:camera-RearFacing");

        private void Start(String format, int width, int height)
        {
            if (this.isRendering)
            {
                return;
            }

            Deployment.Current.Dispatcher.BeginInvoke(() =>
            {
                try
                {
                    if (this.mediastreamer == null)
                    {
                        this.mediastreamer = MediaStreamerFactory.CreateMediaStreamer(this.streamId);
                    }
                    this.streamSource = new VideoStreamSource(format, width, height);
                    this.mediastreamer.SetSource(this.streamSource);
                    this.isRendering = true;
                }
                catch (Exception e)
                {
                    Debug.WriteLine("[MSWP8Vid] VideoRenderer.Start() failed: " + e.Message);
                }
            });
        }

        private void Stop()
        {
            Deployment.Current.Dispatcher.BeginInvoke(() =>
            {
                if (!this.isRendering)
                {
                    return;
                }

                this.streamSource.Shutdown();
                this.streamSource.Dispose();
                this.streamSource = null;
                this.mediastreamer.Dispose();
                this.mediastreamer = null;
                this.isRendering = false;
            });
        }

        private void ChangeFormat(String format, int width, int height)
        {
            Deployment.Current.Dispatcher.BeginInvoke(() =>
            {
                if (this.streamSource != null)
                {
                    this.streamSource.ChangeFormat(format, width, height);
                }
            });
        }

        private bool isRendering;
        private int streamId;
        private MediaStreamer mediastreamer;
        private VideoStreamSource streamSource;
    }
}
