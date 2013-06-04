using Microsoft.Phone.Media;
using System;
using System.Diagnostics;
using System.Windows;

using Mediastreamer2.WP8Video;

namespace Mediastreamer2
{
    namespace WP8Video
    {
        public class VideoRenderer : IVideoRenderer
        {
            public VideoRenderer()
            {
                Random rand = new Random();
                this.streamId = rand.Next(0, 65535);
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

            public IVideoDispatcher GetDispatcher()
            {
                return this.streamSource;
            }

            private bool isRendering;
            private int streamId;
            private MediaStreamer mediastreamer;
            private VideoStreamSource streamSource;
        }
    }
}
