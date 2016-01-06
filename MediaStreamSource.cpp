/*
MediaStreamSource.cpp

mediastreamer2 library - modular sound and video processing and streaming
Windows Audio Session API sound card plugin for mediastreamer2
Copyright (C) 2010-2015 Belledonne Communications, Grenoble, France

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

#include "MediaStreamSource.h"
#include <mfapi.h>
#include <wrl.h>
#include <robuffer.h>

using namespace Windows::Media::Core;
using namespace Windows::Media::MediaProperties;
using Microsoft::WRL::ComPtr;


libmswinrtvid::MediaStreamSource::MediaStreamSource()
	: mMediaStreamSource(nullptr), mTimeStamp(0LL), mInitialTimeStamp(0LL)
{
	mDeferralQueue = ref new Platform::Collections::Vector<SampleRequestDeferral^>();
}

libmswinrtvid::MediaStreamSource::~MediaStreamSource()
{
}

libmswinrtvid::MediaStreamSource^ libmswinrtvid::MediaStreamSource::CreateMediaSource()
{
	libmswinrtvid::MediaStreamSource^ streamState = ref new libmswinrtvid::MediaStreamSource();
	VideoEncodingProperties^ videoProperties = VideoEncodingProperties::CreateUncompressed(MediaEncodingSubtypes::Nv12, 40, 40);
	streamState->mVideoDesc = ref new VideoStreamDescriptor(videoProperties);
	streamState->mVideoDesc->EncodingProperties->Width = 40;
	streamState->mVideoDesc->EncodingProperties->Height = 40;
	streamState->mMediaStreamSource = ref new Windows::Media::Core::MediaStreamSource(streamState->mVideoDesc);
	streamState->mMediaStreamSource->SampleRequested += ref new Windows::Foundation::TypedEventHandler<Windows::Media::Core::MediaStreamSource ^, MediaStreamSourceSampleRequestedEventArgs ^>(
		[streamState](Windows::Media::Core::MediaStreamSource^ sender, MediaStreamSourceSampleRequestedEventArgs^ args) {
			streamState->OnSampleRequested(sender, args);
		});
	return streamState;
}

void libmswinrtvid::MediaStreamSource::OnSampleRequested(Windows::Media::Core::MediaStreamSource ^sender, MediaStreamSourceSampleRequestedEventArgs ^args)
{
	MediaStreamSourceSampleRequest^ request = args->Request;
	if (request == nullptr) {
		return;
	}
	mMutex.lock();
	if (mSample == nullptr) {
		mDeferralQueue->Append(ref new SampleRequestDeferral(request, request->GetDeferral()));
	}
	else {
		AnswerSampleRequest(request);
	}
	mMutex.unlock();
}

void libmswinrtvid::MediaStreamSource::Feed(Windows::Storage::Streams::IBuffer^ pBuffer, int width, int height)
{
	mMutex.lock();
	mSample = ref new Sample(pBuffer, width, height);
	if (mDeferralQueue->Size > 0) {
		SampleRequestDeferral^ deferral = mDeferralQueue->GetAt(0);
		mDeferralQueue->RemoveAt(0);
		AnswerSampleRequest(deferral->Request);
		deferral->Deferral->Complete();
	}
	mMutex.unlock();
}

void libmswinrtvid::MediaStreamSource::AnswerSampleRequest(Windows::Media::Core::MediaStreamSourceSampleRequest^ sampleRequest)
{
	ComPtr<IMFMediaStreamSourceSampleRequest> spRequest;
	HRESULT hr = reinterpret_cast<IInspectable*>(sampleRequest)->QueryInterface(spRequest.ReleaseAndGetAddressOf());
	if (FAILED(hr)) return;
	ComPtr<IMFSample> spSample;
	hr = MFCreateSample(spSample.GetAddressOf());
	if (FAILED(hr)) return;
	LONGLONG timeStamp = GetTickCount64();
	if (mInitialTimeStamp == 0) {
		mInitialTimeStamp = timeStamp;
	}
	mTimeStamp = timeStamp;
	// Set frame 40ms into the future
	LONGLONG sampleTime = (mTimeStamp - mInitialTimeStamp + 40LL) * 10000LL;
	spSample->SetSampleTime(sampleTime);
	ComPtr<IMFMediaBuffer> mediaBuffer;
	if ((mVideoDesc->EncodingProperties->Width != mSample->Width) || (mVideoDesc->EncodingProperties->Height != mSample->Height)) {
		mVideoDesc->EncodingProperties->Width = mSample->Width;
		mVideoDesc->EncodingProperties->Height = mSample->Height;
	}
	hr = MFCreate2DMediaBuffer(mVideoDesc->EncodingProperties->Width, mVideoDesc->EncodingProperties->Height, 0x3231564E /* NV12 */, FALSE, mediaBuffer.GetAddressOf());
	if (FAILED(hr)) return;
	spSample->AddBuffer(mediaBuffer.Get());
	RenderFrame(mediaBuffer.Get());
	spRequest->SetSample(spSample.Get());
}

void libmswinrtvid::MediaStreamSource::RenderFrame(IMFMediaBuffer* mediaBuffer)
{
	ComPtr<IMF2DBuffer2> imageBuffer;
	if (FAILED(mediaBuffer->QueryInterface(imageBuffer.GetAddressOf()))) return;

	ComPtr<Windows::Storage::Streams::IBufferByteAccess> sampleByteAccess;
	if (FAILED(reinterpret_cast<IInspectable*>(mSample->Buffer)->QueryInterface(IID_PPV_ARGS(&sampleByteAccess)))) return;

	BYTE* destRawData;
	BYTE* buffer;
	LONG pitch;
	DWORD destMediaBufferSize;
	if (FAILED(imageBuffer->Lock2DSize(MF2DBuffer_LockFlags_Write, &destRawData, &pitch, &buffer, &destMediaBufferSize))) return;

	BYTE* srcRawData = nullptr;
	sampleByteAccess->Buffer(&srcRawData);
	memcpy(destRawData, srcRawData, destMediaBufferSize);
	imageBuffer->Unlock2D();
}
