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
        }

        public void Start()
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
                        this.mediastreamer = MediaStreamerFactory.CreateMediaStreamer(5060);
                    }
                    this.streamSource = new VideoStreamSource(640, 480);
                    this.mediastreamer.SetSource(this.streamSource);
                    this.isRendering = true;
                }
                catch (Exception e)
                {
                    Debug.WriteLine("[MSWP8Vid] VideoRenderer.Start() failed: " + e.Message);
                }
            });
        }

        public void Stop()
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

        private bool isRendering;
        private MediaStreamer mediastreamer;
        private VideoStreamSource streamSource;
    }
}
