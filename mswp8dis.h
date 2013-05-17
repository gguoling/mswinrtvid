/*
mswp8dis.h

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


#pragma once


#include "windows.h"
#include "implements.h"
#include <robuffer.h>
#include <windows.storage.streams.h>
#include "mediastreamer2/msfilter.h"
#include "mediastreamer2/rfc3984.h"


namespace mswp8vid
{
		public delegate void SampleReceivedEventHandler(Windows::Storage::Streams::IBuffer ^pBuffer, UINT64 hnsPresentationTime);

		public ref class DisplayEventDispatcher sealed {
		public:
			DisplayEventDispatcher();
			virtual ~DisplayEventDispatcher();

			void writeSample(BYTE* bytes, int byteCount, UINT64 hnsPresentationTime);

			event SampleReceivedEventHandler^ sampleReceived;
		};

		/// <summary>
		/// The purpose of this class is to transform byte buffers into an IBuffer
		/// </summary>
		class NativeBuffer : public Microsoft::WRL::RuntimeClass<
								Microsoft::WRL::RuntimeClassFlags< Microsoft::WRL::RuntimeClassType::WinRtClassicComMix >,
								ABI::Windows::Storage::Streams::IBuffer,
								Windows::Storage::Streams::IBufferByteAccess,
								Microsoft::WRL::FtmBase>
		{
		public:
			virtual ~NativeBuffer() {
				if (m_pBuffer && m_bIsOwner) {
					delete[] m_pBuffer;
					m_pBuffer = NULL;
				}
			}

			STDMETHODIMP RuntimeClassInitialize(BYTE* pBuffer, UINT totalSize, BOOL fTakeOwnershipOfPassedInBuffer) {
				m_uLength = totalSize;
				m_uFullSize = totalSize;
				m_pBuffer = pBuffer;
				m_bIsOwner = fTakeOwnershipOfPassedInBuffer;
				return S_OK;
			}

			STDMETHODIMP Buffer(BYTE **value) {
				*value = m_pBuffer;
				return S_OK;
			}

			STDMETHODIMP get_Capacity(UINT32 *value) {
				*value = m_uFullSize;
				return S_OK;
			}

			STDMETHODIMP get_Length(UINT32 *value) {
				*value = m_uLength;
				return S_OK;
			}

			STDMETHODIMP put_Length(UINT32 value) {
				if(value > m_uFullSize) {
					return E_INVALIDARG;
				}
				m_uLength = value;
				return S_OK;
			}

			static Windows::Storage::Streams::IBuffer^ GetIBufferFromNativeBuffer(Microsoft::WRL::ComPtr<NativeBuffer> spNativeBuffer) {
				auto iinspectable = reinterpret_cast<IInspectable*>(spNativeBuffer.Get());
				return reinterpret_cast<Windows::Storage::Streams::IBuffer^>(iinspectable);
			}

			static BYTE* GetBytesFromIBuffer(Windows::Storage::Streams::IBuffer^ buffer) {
				auto iinspectable = (IInspectable*)reinterpret_cast<IInspectable*>(buffer);
				Microsoft::WRL::ComPtr<Windows::Storage::Streams::IBufferByteAccess> spBuffAccess;
				HRESULT hr = iinspectable->QueryInterface(__uuidof(Windows::Storage::Streams::IBufferByteAccess), (void **)&spBuffAccess);
				if (hr == S_OK) {
					UCHAR * pReadBuffer;
					spBuffAccess->Buffer(&pReadBuffer);
					return pReadBuffer;
				}
				return nullptr;
			}

		private:
			UINT32 m_uLength;
			UINT32 m_uFullSize;
			BYTE* m_pBuffer;
			BOOL m_bIsOwner;
		};

		public delegate void RenderStarted(Platform::String^ format, int width, int height);
		public delegate void RenderStopped();

		public ref class Globals sealed
		{
		public:
			// Get the single instance of this class
			static property Globals^ Instance
			{
				Globals^ get();
			}

			// The singleton display event dispatcher object.
			property DisplayEventDispatcher^ VideoSampleDispatcher
			{
				DisplayEventDispatcher^ get();
			}

			event RenderStarted^ renderStarted;
			event RenderStopped^ renderStopped;

			void startRendering(Platform::String^ format, int width, int height);
			void stopRendering();

		private:
			Globals();
			~Globals();

			static Globals^ singleton;	// The single instance of this class
			DisplayEventDispatcher^ videoSampleDispatcher;
		};

		class MSWP8Dis {
		public:
			MSWP8Dis();
			virtual ~MSWP8Dis();

			int activate();
			int deactivate();
			bool isStarted() { return mIsStarted; }
			void start();
			void stop();
			int feed(MSFilter *f);

		private:
			int nalusToFrame(MSQueue *nalus, bool *new_sps_pps);
			void enlargeBitstream(int newSize);

			static bool smInstantiated;
			bool mIsInitialized;
			bool mIsActivated;
			bool mIsStarted;
			Rfc3984Context *mRfc3984Unpacker;
			int mBitstreamSize;
			uint8_t *mBitstream;
		};
}
