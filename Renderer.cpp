/*
Renderer.cpp

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

#include "Renderer.h"
#include "ScopeLock.h"

#include <mediastreamer2/mscommon.h>

using namespace libmswinrtvid;
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::Foundation::Collections;


MSWinRTRenderer::MSWinRTRenderer() :
	mWidth(0), mHeight(0), mMediaStreamSource(nullptr),
	mForegroundProcess(nullptr), mMemoryMapping(nullptr), mSharedData(nullptr), mLock(nullptr), mShutdownEvent(nullptr), mEventAvailableEvent(nullptr)
{
}

MSWinRTRenderer::~MSWinRTRenderer()
{
	Close();
}

void MSWinRTRenderer::SetSwapChainPanel(Platform::String ^swapChainPanelName)
{
	Close();

	mMemoryMapping = OpenFileMappingFromApp(FILE_MAP_READ | FILE_MAP_WRITE, TRUE, swapChainPanelName->Data());
	if ((mMemoryMapping == nullptr) || (mMemoryMapping == INVALID_HANDLE_VALUE)) {
		DWORD error = GetLastError();
		throw ref new Platform::COMException(HRESULT_FROM_WIN32(error));
	}
	mSharedData = (SharedData*)MapViewOfFileFromApp(mMemoryMapping, FILE_MAP_READ | FILE_MAP_WRITE, 0LL, sizeof(*mSharedData));
	if (mSharedData == nullptr) {
		DWORD error = GetLastError();
		Close();
		throw ref new Platform::COMException(HRESULT_FROM_WIN32(error));
	}
	mSharedData->backgroundProcessId = GetCurrentProcessId();
	mForegroundProcess = OpenProcess(PROCESS_DUP_HANDLE, TRUE, mSharedData->foregroundProcessId);
	if ((mForegroundProcess == nullptr) || (mForegroundProcess == INVALID_HANDLE_VALUE)) {
		DWORD error = GetLastError();
		Close();
		throw ref new Platform::COMException(HRESULT_FROM_WIN32(error));
	}
	if (!DuplicateHandle(mForegroundProcess, mSharedData->foregroundLockMutex, GetCurrentProcess(), &mLock, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
		DWORD error = GetLastError();
		mLock = nullptr;
		Close();
		throw ref new Platform::COMException(HRESULT_FROM_WIN32(error));
	}
	if (!DuplicateHandle(mForegroundProcess, mSharedData->foregroundShutdownEvent, GetCurrentProcess(), &mShutdownEvent, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
		DWORD error = GetLastError();
		mLock = nullptr;
		Close();
		throw ref new Platform::COMException(HRESULT_FROM_WIN32(error));
	}
	if (!DuplicateHandle(mForegroundProcess, mSharedData->foregroundEventAvailableEvent, GetCurrentProcess(), &mEventAvailableEvent, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
		DWORD error = GetLastError();
		mLock = nullptr;
		Close();
		throw ref new Platform::COMException(HRESULT_FROM_WIN32(error));
	}
}

void MSWinRTRenderer::Close()
{
	if (mSharedData != nullptr)
	{
		UnmapViewOfFile(mSharedData);
		mSharedData = nullptr;
	}
	if (mMemoryMapping != nullptr)
	{
		CloseHandle(mMemoryMapping);
		mMemoryMapping = nullptr;
	}
	if (mLock != nullptr)
	{
		CloseHandle(mLock);
		mLock = nullptr;
	}
	if (mShutdownEvent != nullptr)
	{
		CloseHandle(mShutdownEvent);
		mShutdownEvent = nullptr;
	}
	if (mEventAvailableEvent != nullptr)
	{
		CloseHandle(mEventAvailableEvent);
		mEventAvailableEvent = nullptr;
	}
	if (mForegroundProcess != nullptr)
	{
		CloseHandle(mForegroundProcess);
		mForegroundProcess = nullptr;
	}
}

bool MSWinRTRenderer::Start()
{
	HRESULT hr = SetupSchemeHandler();
	if (FAILED(hr)) {
		SendErrorEvent(hr);
		return false;
	}
	hr = SetupDirectX();
	if (FAILED(hr)) {
		SendErrorEvent(hr);
		return false;
	}
	mMediaStreamSource = MediaStreamSource::CreateMediaSource();
	boolean replaced;
	auto streamInspect = reinterpret_cast<IInspectable*>(mMediaStreamSource->Source);
	std::wstring url(L"mswinrtvid://");
	GUID result;
	hr = CoCreateGuid(&result);
	if (FAILED(hr)) {
		SendErrorEvent(hr);
		return false;
	}
	Platform::Guid gd(result);
	url += gd.ToString()->Data();
	hr = mExtensionManagerProperties->Insert(HStringReference(url.c_str()).Get(), streamInspect, &replaced);
	if (FAILED(hr)) {
		SendErrorEvent(hr);
		return false;
	}
	BSTR sourceBSTR;
	sourceBSTR = SysAllocString(url.c_str());
	hr = mMediaEngine->SetSource(sourceBSTR);
	SysFreeString(sourceBSTR);
	if (FAILED(hr)) {
		ms_error("SetSource failed");
		SendErrorEvent(hr);
		return false;
	}
	hr = mMediaEngine->Load();
	if (FAILED(hr)) {
		ms_error("Load failed");
		SendErrorEvent(hr);
		return false;
	}
	return true;
}

void MSWinRTRenderer::Feed(Windows::Storage::Streams::IBuffer^ pBuffer, int width, int height)
{
	if (mMediaStreamSource != nullptr) {
		mMediaStreamSource->Feed(pBuffer, width, height);
	}
}

HRESULT MSWinRTRenderer::SetupSchemeHandler()
{
	using Windows::Foundation::ActivateInstance;
	HRESULT hr = ActivateInstance(HStringReference(RuntimeClass_Windows_Media_MediaExtensionManager).Get(), mMediaExtensionManager.ReleaseAndGetAddressOf());
	if (FAILED(hr)) {
		ms_error("Failed to create media extension manager");
		return hr;
	}
	ComPtr<IMap<HSTRING, IInspectable*>> props;
	hr = ActivateInstance(HStringReference(RuntimeClass_Windows_Foundation_Collections_PropertySet).Get(), props.ReleaseAndGetAddressOf());
	ComPtr<IPropertySet> propSet;
	props.As(&propSet);
	HStringReference clsid(L"libmswinrtvid.SchemeHandler");
	HStringReference scheme(L"mswinrtvid:");
	hr = mMediaExtensionManager->RegisterSchemeHandlerWithSettings(clsid.Get(), scheme.Get(), propSet.Get());
	if (FAILED(hr)) {
		ms_error("RegisterSchemeHandlerWithSettings failed");
		return hr;
	}
	mExtensionManagerProperties = props;
	return S_OK;
}

HRESULT MSWinRTRenderer::SetupDirectX()
{
	mUseHardware = true;
	HRESULT hr = MFStartup(MF_VERSION);
	if (FAILED(hr)) {
		ms_error("MFStartup failed");
		return hr;
	}
	hr = CreateDX11Device();
	if (FAILED(hr)) {
		return hr;
	}
	UINT resetToken;
	hr = MFCreateDXGIDeviceManager(&resetToken, &mDxGIManager);
	if (FAILED(hr)) {
		ms_error("MFCreateDXGIDeviceManager failed");
		return hr;
	}
	hr = mDxGIManager->ResetDevice(mDevice.Get(), resetToken);
	if (FAILED(hr)) {
		ms_error("ResetDevice failed");
		return hr;
	}
	ComPtr<IMFMediaEngineClassFactory> factory;
	hr = CoCreateInstance(CLSID_MFMediaEngineClassFactory, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&factory));
	if (FAILED(hr)) {
		ms_error("CoCreateInstance failed");
		return hr;
	}
	ComPtr<IMFAttributes> attributes;
	hr = MFCreateAttributes(&attributes, 3);
	if (FAILED(hr)) {
		ms_error("MFCreateAttributes failed");
		return hr;
	}
	hr = attributes->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER, (IUnknown*)mDxGIManager.Get());
	if (FAILED(hr)) {
		ms_error("attributes->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER, (IUnknown*)mDxGIManager.Get()) failed");
		return hr;
	}
	ComPtr<MediaEngineNotify> notify;
	notify = Make<MediaEngineNotify>();
	notify->SetCallback(this);
	hr = attributes->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, DXGI_FORMAT_NV12);
	if (FAILED(hr)) {
		ms_error("attributes->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, DXGI_FORMAT_NV12) failed");
		return hr;
	}
	hr = attributes->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, (IUnknown*)notify.Get());
	if (FAILED(hr)) {
		ms_error("attributes->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, (IUnknown*)notify.Get()) failed");
		return hr;
	}
	hr = factory->CreateInstance(MF_MEDIA_ENGINE_REAL_TIME_MODE | MF_MEDIA_ENGINE_WAITFORSTABLE_STATE, attributes.Get(), &mMediaEngine);
	if (FAILED(hr)) {
		ms_error("CreateInstance failed");
		return hr;
	}
	hr = mMediaEngine.Get()->QueryInterface(__uuidof(IMFMediaEngineEx), (void**)&mMediaEngineEx);
	if (FAILED(hr)) {
		ms_error("mMediaEngine.Get()->QueryInterface(__uuidof(IMFMediaEngineEx), (void**)&mMediaEngineEx) failed");
		return hr;
	}
	hr = mMediaEngineEx->EnableWindowlessSwapchainMode(TRUE);
	if (FAILED(hr)) {
		ms_error("mMediaEngineEx->EnableWindowlessSwapchainMode(TRUE) failed");
		return hr;
	}
	mMediaEngineEx->SetRealTimeMode(TRUE);
	return S_OK;
}

HRESULT MSWinRTRenderer::CreateDX11Device()
{
	static const D3D_FEATURE_LEVEL levels[] = {
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0
	};
	D3D_FEATURE_LEVEL FeatureLevel;
	HRESULT hr = S_OK;

	if (mUseHardware) {
		hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_VIDEO_SUPPORT, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &mDevice, &FeatureLevel, &mDx11DeviceContext);
	}

	if (FAILED(hr)) {
		ms_warning("Failed to create hardware device, falling back to software");
		mUseHardware = false;
	}

	if (!mUseHardware) {
		hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_VIDEO_SUPPORT, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &mDevice, &FeatureLevel, &mDx11DeviceContext);
		if (FAILED(hr)) {
			ms_error("Failed to create device");
			return hr;
		}
	}

	if (mUseHardware) {
		ComPtr<ID3D10Multithread> multithread;
		hr = mDevice.Get()->QueryInterface(IID_PPV_ARGS(&multithread));
		if (FAILED(hr)) {
			ms_error("Failed to set hardware to multithreaded");
			return hr;
		}
		multithread->SetMultithreadProtected(TRUE);
	}
	return hr;
}

void MSWinRTRenderer::SendSwapChainHandle(HANDLE swapChain, bool forceNewHandle)
{
	if ((swapChain == nullptr) || (swapChain == INVALID_HANDLE_VALUE)) return;

	bool notify = false;
	ScopeLock lock(mLock);
	if (forceNewHandle) {
		if (mSharedData->backgroundSwapChainHandle) {
			CloseHandle(mSharedData->backgroundSwapChainHandle);
		}
		mSharedData->backgroundSwapChainHandle = swapChain;
		notify = true;
	}
	else if (mSharedData->backgroundSwapChainHandle == nullptr) {
		mSharedData->backgroundSwapChainHandle = swapChain;
		notify = true;
	}
	if (notify) {
		HANDLE foregroundProcess = OpenProcess(PROCESS_DUP_HANDLE, TRUE, mSharedData->foregroundProcessId);
		DuplicateHandle(GetCurrentProcess(), swapChain, foregroundProcess, &mSharedData->foregroundSwapChainHandle, 0, TRUE, DUPLICATE_SAME_ACCESS);
		CloseHandle(foregroundProcess);
		SetEvent(mEventAvailableEvent);
	}
}

void MSWinRTRenderer::SendErrorEvent(HRESULT hr)
{
	ScopeLock lock(mLock);
	mSharedData->error = hr;
	SetEvent(mSharedData->foregroundEventAvailableEvent);
}

void MSWinRTRenderer::OnMediaEngineEvent(uint32 meEvent, uintptr_t param1, uint32 param2)
{
	HRESULT hr;
	HANDLE swapChainHandle;
	switch ((DWORD)meEvent) {
	case MF_MEDIA_ENGINE_EVENT_ERROR:
		SendErrorEvent((HRESULT)param2);
		break;
	case MF_MEDIA_ENGINE_EVENT_PLAYING:
	case MF_MEDIA_ENGINE_EVENT_FIRSTFRAMEREADY:
		mMediaEngineEx->GetVideoSwapchainHandle(&swapChainHandle);
		SendSwapChainHandle(swapChainHandle, false);
		break;
	case MF_MEDIA_ENGINE_EVENT_FORMATCHANGE:
		mMediaEngineEx->GetVideoSwapchainHandle(&swapChainHandle);
		SendSwapChainHandle(swapChainHandle, true);
		break;
	case MF_MEDIA_ENGINE_EVENT_CANPLAY:
		hr = mMediaEngine->Play();
		if (FAILED(hr)) {
			SendErrorEvent(hr);
		}
		break;
	}
}
