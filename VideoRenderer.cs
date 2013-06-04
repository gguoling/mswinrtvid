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
                this.streamId = GetHashCode();
            }

            public IVideoDispatcher Dispatcher
            {
                get
                {
                    return this.streamSource;
                }
            }

            public static Uri StreamUri(Int32 streamId)
            {
                return new Uri("ms-media-stream-id:MediaStreamer-" + streamId);
            }

            public static Uri FrontFacingCameraStreamUri = new Uri("ms-media-stream-id:camera-FrontFacing");
            public static Uri RearFacingCameraStreamUri = new Uri("ms-media-stream-id:camera-RearFacing");

            public int GetNativeWindowId()
            {
                return this.streamId;
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
                            this.mediastreamer = MediaStreamerFactory.CreateMediaStreamer(this.streamId);
                        }
                        this.streamSource = new VideoStreamSource(format, width, height);
                        this.mediastreamer.SetSource(this.streamSource);
                        this.isRendering = true;
                    }
                    catch (Exception e)
                    {
                        Debug.WriteLine("[VideoRenderer] VideoRenderer.Start() failed: " + e.Message);
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
            private Int32 streamId;
            private MediaStreamer mediastreamer;
            private VideoStreamSource streamSource;
        }
    }
}
