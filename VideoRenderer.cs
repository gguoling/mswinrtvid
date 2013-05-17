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
            mswp8vid.Globals.Instance.renderStarted += Start;
            mswp8vid.Globals.Instance.renderStopped += Stop;
            mswp8vid.Globals.Instance.renderFormatChanged += ChangeFormat;
        }

        public void Start(String format, int width, int height)
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

        public void ChangeFormat(String format, int width, int height)
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
        private MediaStreamer mediastreamer;
        private VideoStreamSource streamSource;
    }
}
