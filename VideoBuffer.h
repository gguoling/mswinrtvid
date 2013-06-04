/*
VideoBuffer.h

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


#include <windows.h>
#include <wrl.h>
#include <wrl/implements.h>
#include <wrl/client.h>
#include <robuffer.h>
#include <windows.storage.streams.h>


namespace Mediastreamer2
{
	namespace WP8Video
	{
		/// <summary>
		/// The purpose of this class is to transform byte buffers into an IBuffer
		/// </summary>
		class VideoBuffer : public Microsoft::WRL::RuntimeClass<
								Microsoft::WRL::RuntimeClassFlags< Microsoft::WRL::RuntimeClassType::WinRtClassicComMix >,
								ABI::Windows::Storage::Streams::IBuffer,
								Windows::Storage::Streams::IBufferByteAccess,
								Microsoft::WRL::FtmBase>
		{
		public:
			virtual ~VideoBuffer() {
				if (mBuffer) {
					delete[] mBuffer;
					mBuffer = NULL;
				}
			}

			STDMETHODIMP RuntimeClassInitialize(BYTE* pBuffer, UINT size) {
				mSize = size;
				mBuffer = new BYTE[size];
				memcpy((void*)mBuffer, (void*)pBuffer, size);
				return S_OK;
			}

			STDMETHODIMP Buffer(BYTE **value) {
				*value = mBuffer;
				return S_OK;
			}

			STDMETHODIMP get_Capacity(UINT32 *value) {
				*value = mSize;
				return S_OK;
			}

			STDMETHODIMP get_Length(UINT32 *value) {
				*value = mSize;
				return S_OK;
			}

			STDMETHODIMP put_Length(UINT32 value) {
				if(value > mSize) {
					return E_INVALIDARG;
				}
				mSize = value;
				return S_OK;
			}

			static Windows::Storage::Streams::IBuffer^ GetIBuffer(Microsoft::WRL::ComPtr<VideoBuffer> spVideoBuffer) {
				auto iinspectable = reinterpret_cast<IInspectable*>(spVideoBuffer.Get());
				return reinterpret_cast<Windows::Storage::Streams::IBuffer^>(iinspectable);
			}

		private:
			UINT32 mSize;
			BYTE* mBuffer;
		};
	}
}
