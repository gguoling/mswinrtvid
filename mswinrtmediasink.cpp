#include "mswinrtmediasink.h"
#include <mediastreamer2/mscommon.h>

using namespace libmswinrtvid;


#define RETURN_HR(hr) { \
	if (FAILED(hr)) \
		ms_error("%s:%d -> 0x%x", __FUNCTION__, __LINE__, hr); \
	return hr; \
}


static void AddAttribute(_In_ GUID guidKey, _In_ IPropertyValue ^value, _In_ IMFAttributes *pAttr)
{
	HRESULT hr = S_OK;
	PropertyType type = value->Type;
	switch (type) {
	case PropertyType::UInt8Array:
		{
			Array<BYTE>^ arr;
			value->GetUInt8Array(&arr);
			hr = pAttr->SetBlob(guidKey, arr->Data, arr->Length);
		}
		break;
	case PropertyType::Double:
		hr = pAttr->SetDouble(guidKey, value->GetDouble());
		break;
	case PropertyType::Guid:
		hr = pAttr->SetGUID(guidKey, value->GetGuid());
		break;
	case PropertyType::String:
		hr = pAttr->SetString(guidKey, value->GetString()->Data());
		break;
	case PropertyType::UInt32:
		hr = pAttr->SetUINT32(guidKey, value->GetUInt32());
		break;
	case PropertyType::UInt64:
		hr = pAttr->SetUINT64(guidKey, value->GetUInt64());
		break;
	// ignore unknown values
	}

	if (FAILED(hr))
		throw ref new Exception(hr);
}

static void ConvertPropertiesToMediaType(_In_ IMediaEncodingProperties ^mep, _Outptr_ IMFMediaType **ppMT)
{
	if (mep == nullptr || ppMT == nullptr)
		throw ref new InvalidArgumentException();

	ComPtr<IMFMediaType> spMT;
	*ppMT = nullptr;
	HRESULT hr = MFCreateMediaType(&spMT);
	if (FAILED(hr))
		throw ref new Exception(hr);

	auto it = mep->Properties->First();
	while (it->HasCurrent) {
		auto currentValue = it->Current;
		AddAttribute(currentValue->Key, safe_cast<IPropertyValue^>(currentValue->Value), spMT.Get());
		it->MoveNext();
	}

	GUID guiMajorType = safe_cast<IPropertyValue^>(mep->Properties->Lookup(MF_MT_MAJOR_TYPE))->GetGuid();
	if (guiMajorType != MFMediaType_Video)
		throw ref new Exception(E_UNEXPECTED);

	*ppMT = spMT.Detach();
}





MSWinRTStreamSink::MSWinRTStreamSink(DWORD dwIdentifier)
	: _cRef(1)
	, _dwIdentifier(dwIdentifier)
	, _state(State_TypeNotSet)
	, _IsShutdown(false)
	, _Connected(false)
	, _fGetStartTimeFromSample(false)
	, _fWaitingForFirstSample(false)
	, _fFirstSampleAfterConnect(false)
	, _StartTime(0)
	, _WorkQueueId(0)
	, _pParent(nullptr)
#pragma warning(push)
#pragma warning(disable:4355)
	//, _WorkQueueCB(this, &MSWinRTStreamSink::OnDispatchWorkItem)
#pragma warning(pop)
{
	ZeroMemory(&_guiCurrentSubtype, sizeof(_guiCurrentSubtype));
	ms_message("MSWinRTStreamSink constructor");
}

MSWinRTStreamSink::~MSWinRTStreamSink()
{
	ms_message("MSWinRTStreamSink destructor");
}


// IUnknown methods

IFACEMETHODIMP MSWinRTStreamSink::QueryInterface(REFIID riid, void **ppv)
{
	ms_message("MSWinRTStreamSink::QueryInterface");
	if (ppv == nullptr)
		RETURN_HR(E_POINTER)
	(*ppv) = nullptr;

	HRESULT hr = S_OK;
	if (riid == IID_IUnknown ||	riid == IID_IMFStreamSink || riid == IID_IMFMediaEventGenerator) {
		(*ppv) = static_cast<IMFStreamSink*>(this);
		AddRef();
	} else if (riid == IID_IMFMediaTypeHandler) {
		(*ppv) = static_cast<IMFMediaTypeHandler*>(this);
		AddRef();
	} else {
		hr = E_NOINTERFACE;
	}

	if (FAILED(hr) && riid == IID_IMarshal) {
		if (_spFTM == nullptr) {
			_mutex.lock();
			if (_spFTM == nullptr)
				hr = CoCreateFreeThreadedMarshaler(static_cast<IMFStreamSink*>(this), &_spFTM);
			_mutex.unlock();
		}
		if (SUCCEEDED(hr)) {
			if (_spFTM == nullptr)
				hr = E_UNEXPECTED;
			else
				hr = _spFTM.Get()->QueryInterface(riid, ppv);
		}
	}

	RETURN_HR(hr)
}

IFACEMETHODIMP_(ULONG) MSWinRTStreamSink::AddRef()
{
	ms_message("MSWinRTStreamSink::AddRef");
	return InterlockedIncrement(&_cRef);
}

IFACEMETHODIMP_(ULONG) MSWinRTStreamSink::Release()
{
	ms_message("MSWinRTStreamSink::Release");
	long cRef = InterlockedDecrement(&_cRef);
	if (cRef == 0)
		delete this;
	return cRef;
}

// IMFMediaEventGenerator methods.
// Note: These methods call through to the event queue helper object.

IFACEMETHODIMP MSWinRTStreamSink::BeginGetEvent(IMFAsyncCallback *pCallback, IUnknown *punkState)
{
	ms_message("MSWinRTStreamSink::BeginGetEvent");
	HRESULT hr = S_OK;
	_mutex.lock();
	hr = CheckShutdown();
	if (SUCCEEDED(hr))
		hr = _spEventQueue->BeginGetEvent(pCallback, punkState);
	_mutex.unlock();
	RETURN_HR(hr)
}

IFACEMETHODIMP MSWinRTStreamSink::EndGetEvent(IMFAsyncResult *pResult, IMFMediaEvent **ppEvent)
{
	ms_message("MSWinRTStreamSink::EndGetEvent");
	HRESULT hr = S_OK;
	_mutex.lock();
	hr = CheckShutdown();
	if (SUCCEEDED(hr))
		hr = _spEventQueue->EndGetEvent(pResult, ppEvent);
	_mutex.unlock();
	RETURN_HR(hr)
}

IFACEMETHODIMP MSWinRTStreamSink::GetEvent(DWORD dwFlags, IMFMediaEvent **ppEvent)
{
	// NOTE:
	// GetEvent can block indefinitely, so we don't hold the lock.
	// This requires some juggling with the event queue pointer.

	ms_message("MSWinRTStreamSink::GetEvent");
	HRESULT hr = S_OK;
	ComPtr<IMFMediaEventQueue> spQueue;

	_mutex.lock();
	hr = CheckShutdown();
	if (SUCCEEDED(hr))
		spQueue = _spEventQueue;
	_mutex.unlock();

	if (SUCCEEDED(hr))
		hr = spQueue->GetEvent(dwFlags, ppEvent);
	RETURN_HR(hr)
}

IFACEMETHODIMP MSWinRTStreamSink::QueueEvent(MediaEventType met, REFGUID guidExtendedType, HRESULT hrStatus, PROPVARIANT const *pvValue)
{
	ms_message("MSWinRTStreamSink::QueueEvent");
	HRESULT hr = S_OK;
	_mutex.lock();
	hr = CheckShutdown();
	if (SUCCEEDED(hr))
		hr = _spEventQueue->QueueEventParamVar(met, guidExtendedType, hrStatus, pvValue);
	_mutex.unlock();
	RETURN_HR(hr)
}

/// IMFStreamSink methods

IFACEMETHODIMP MSWinRTStreamSink::GetMediaSink(IMFMediaSink **ppMediaSink)
{
	ms_message("MSWinRTStreamSink::GetMediaSink");
	if (ppMediaSink == nullptr)
		RETURN_HR(E_INVALIDARG)

	_mutex.lock();
	HRESULT hr = CheckShutdown();
	if (SUCCEEDED(hr))
		_spSink.Get()->QueryInterface(IID_IMFMediaSink, (void**)ppMediaSink);
	_mutex.unlock();
	RETURN_HR(hr)
}

IFACEMETHODIMP MSWinRTStreamSink::GetIdentifier(DWORD *pdwIdentifier)
{
	ms_message("MSWinRTStreamSink::GetIdentifier");
	if (pdwIdentifier == nullptr)
		RETURN_HR(E_INVALIDARG)

	_mutex.lock();
	HRESULT hr = CheckShutdown();
	if (SUCCEEDED(hr)) {
		ms_message("\t-> %d", _dwIdentifier);
		*pdwIdentifier = _dwIdentifier;
	}
	_mutex.unlock();
	RETURN_HR(hr)
}

IFACEMETHODIMP MSWinRTStreamSink::GetMediaTypeHandler(IMFMediaTypeHandler **ppHandler)
{
	ms_message("MSWinRTStreamSink::GetMediaTypeHandler");
	if (ppHandler == nullptr)
		RETURN_HR(E_INVALIDARG)

	_mutex.lock();
	HRESULT hr = CheckShutdown();
	// This stream object acts as its own type handler, so we QI ourselves.
	if (SUCCEEDED(hr))
		hr = QueryInterface(IID_IMFMediaTypeHandler, (void**)ppHandler);
	_mutex.unlock();
	RETURN_HR(hr)
}

// We received a sample from an upstream component
IFACEMETHODIMP MSWinRTStreamSink::ProcessSample(IMFSample *pSample)
{
	ms_message("MSWinRTStreamSink::ProcessSample");
	if (pSample == nullptr)
		RETURN_HR(E_INVALIDARG)

	HRESULT hr = S_OK;
	_mutex.lock();
	hr = CheckShutdown();
	// Validate the operation.
	if (SUCCEEDED(hr))
		hr = ValidateOperation(OpProcessSample);

	if (SUCCEEDED(hr) && _fWaitingForFirstSample && !_Connected) {
		_spFirstVideoSample = pSample;
		_fWaitingForFirstSample = false;
		hr = QueueEvent(MEStreamSinkRequestSample, GUID_NULL, hr, nullptr);
	} else if (SUCCEEDED(hr)) {
		// Add the sample to the sample queue.
		if (SUCCEEDED(hr))
			hr = _SampleQueue.InsertBack(pSample);

		// Unless we are paused, start an async operation to dispatch the next sample.
		if (SUCCEEDED(hr)) {
			if (_state != State_Paused) {
				// Queue the operation.
				hr = QueueAsyncOperation(OpProcessSample);
			}
		}
	}

	_mutex.unlock();
	RETURN_HR(hr)
}

// The client can call PlaceMarker at any time. In response,
// we need to queue an MEStreamSinkMarker event, but not until
// *after *we have processed all samples that we have received
// up to this point.
//
// Also, in general you might need to handle specific marker
// types, although this sink does not.

IFACEMETHODIMP MSWinRTStreamSink::PlaceMarker(MFSTREAMSINK_MARKER_TYPE eMarkerType, const PROPVARIANT *pvarMarkerValue, const PROPVARIANT *pvarContextValue)
{
	ms_message("MSWinRTStreamSink::PlaceMarker");
	_mutex.lock();
	HRESULT hr = S_OK;
#if 0 // TODO
	ComPtr<IMarker> spMarker;

	hr = CheckShutdown();
	if (SUCCEEDED(hr))
		hr = ValidateOperation(OpPlaceMarker);
	if (SUCCEEDED(hr))
		hr = CreateMarker(eMarkerType, pvarMarkerValue, pvarContextValue, &spMarker);
	if (SUCCEEDED(hr))
		hr = _SampleQueue.InsertBack(spMarker.Get());

	// Unless we are paused, start an async operation to dispatch the next sample/marker.
	if (SUCCEEDED(hr)) {
		if (_state != State_Paused) {
			// Queue the operation.
			hr = QueueAsyncOperation(OpPlaceMarker); // Increments ref count on pOp.
		}
	}
#endif
	_mutex.unlock();
	RETURN_HR(hr)
}

// Discards all samples that were not processed yet.
IFACEMETHODIMP MSWinRTStreamSink::Flush()
{
	ms_message("MSWinRTStreamSink::Flush");
	_mutex.lock();
	HRESULT hr = S_OK;
	try {
		hr = CheckShutdown();
		if (FAILED(hr)) {
			_mutex.unlock();
			throw ref new Exception(hr);
		}

		// Note: Even though we are flushing data, we still need to send
		// any marker events that were queued.
		DropSamplesFromQueue();
	} catch (Exception ^exc) {
		hr = exc->HResult;
	}

	_mutex.unlock();
	RETURN_HR(hr)
}


/// IMFMediaTypeHandler methods

// Check if a media type is supported.
IFACEMETHODIMP MSWinRTStreamSink::IsMediaTypeSupported(/* [in] */ IMFMediaType *pMediaType, /* [out] */ IMFMediaType **ppMediaType)
{
	ms_message("MSWinRTStreamSink::IsMediaTypeSupported");
	if (pMediaType == nullptr)
		RETURN_HR(E_INVALIDARG)

	_mutex.lock();
	GUID majorType = GUID_NULL;
	UINT cbSize = 0;
	HRESULT hr = CheckShutdown();
	if (SUCCEEDED(hr))
		hr = pMediaType->GetGUID(MF_MT_MAJOR_TYPE, &majorType);

	// First make sure it's video type.
	if (SUCCEEDED(hr)) {
		if (majorType != MFMediaType_Video)
			hr = MF_E_INVALIDTYPE;
	}

	if (SUCCEEDED(hr) && _spCurrentType != nullptr) {
		GUID guiNewSubtype;
		if (FAILED(pMediaType->GetGUID(MF_MT_SUBTYPE, &guiNewSubtype)) || guiNewSubtype != _guiCurrentSubtype)
			hr = MF_E_INVALIDTYPE;
	}

	// We don't return any "close match" types.
	if (ppMediaType)
		*ppMediaType = nullptr;

	_mutex.unlock();
	RETURN_HR(hr)
}


// Return the number of preferred media types.
IFACEMETHODIMP MSWinRTStreamSink::GetMediaTypeCount(DWORD *pdwTypeCount)
{
	ms_message("MSWinRTStreamSink::GetMediaTypeCount");
	if (pdwTypeCount == nullptr)
		RETURN_HR(E_INVALIDARG)

	_mutex.lock();
	HRESULT hr = CheckShutdown();
	if (SUCCEEDED(hr)) {
		// We've got only one media type
		*pdwTypeCount = 1;
	}

	_mutex.unlock();
	RETURN_HR(hr)
}


// Return a preferred media type by index.
IFACEMETHODIMP MSWinRTStreamSink::GetMediaTypeByIndex(/* [in] */ DWORD dwIndex, /* [out] */ IMFMediaType **ppType)
{
	ms_message("MSWinRTStreamSink::GetMediaTypeByIndex");
	if (ppType == nullptr)
		RETURN_HR(E_INVALIDARG)

	_mutex.lock();
	HRESULT hr = CheckShutdown();
	if (dwIndex > 0)
		hr = MF_E_NO_MORE_TYPES;
	else {
		*ppType = _spCurrentType.Get();
		if (*ppType != nullptr)
			(*ppType)->AddRef();
	}

	_mutex.unlock();
	RETURN_HR(hr)
}


// Set the current media type.
IFACEMETHODIMP MSWinRTStreamSink::SetCurrentMediaType(IMFMediaType *pMediaType)
{
	ms_message("MSWinRTStreamSink::SetCurrentMediaType");
	HRESULT hr = S_OK;
	try {
		if (pMediaType == nullptr)
			throw ref new Exception(E_INVALIDARG);

		_mutex.lock();
		hr = CheckShutdown();
		if (FAILED(hr)) {
			_mutex.unlock();
			throw ref new Exception(hr);
		}

		// We don't allow format changes after streaming starts.
		hr = ValidateOperation(OpSetMediaType);
		if (FAILED(hr)) {
			_mutex.unlock();
			throw ref new Exception(hr);
		}

		// We set media type already
		if (_state >= State_Ready) {
			hr = IsMediaTypeSupported(pMediaType, nullptr);
			if (FAILED(hr)) {
				_mutex.unlock();
				throw ref new Exception(hr);
			}
		}

		hr = MFCreateMediaType(_spCurrentType.ReleaseAndGetAddressOf());
		if (FAILED(hr)) {
			_mutex.unlock();
			throw ref new Exception(hr);
		}
		hr = pMediaType->CopyAllItems(_spCurrentType.Get());
		if (FAILED(hr)) {
			_mutex.unlock();
			throw ref new Exception(hr);
		}
		hr = _spCurrentType->GetGUID(MF_MT_SUBTYPE, &_guiCurrentSubtype);
		if (FAILED(hr)) {
			_mutex.unlock();
			throw ref new Exception(hr);
		}
		if (_state < State_Ready)
			_state = State_Ready;
		else if (_state > State_Ready) {
			ComPtr<IMFMediaType> spType;
			hr = MFCreateMediaType(&spType);
			if (FAILED(hr)) {
				_mutex.unlock();
				throw ref new Exception(hr);
			}
			hr = pMediaType->CopyAllItems(spType.Get());
			if (FAILED(hr)) {
				_mutex.unlock();
				throw ref new Exception(hr);
			}
			ProcessFormatChange(spType.Get());
		}
	} catch (Exception ^exc) {
		hr = exc->HResult;
	}

	_mutex.unlock();
	RETURN_HR(hr)
}

// Return the current media type, if any.
IFACEMETHODIMP MSWinRTStreamSink::GetCurrentMediaType(IMFMediaType **ppMediaType)
{
	ms_message("MSWinRTStreamSink::GetCurrentMediaType");
	if (ppMediaType == nullptr)
		RETURN_HR(E_INVALIDARG)

	_mutex.lock();
	HRESULT hr = CheckShutdown();
	if (SUCCEEDED(hr)) {
		if (_spCurrentType == nullptr)
			hr = MF_E_NOT_INITIALIZED;
	}
	if (SUCCEEDED(hr)) {
		*ppMediaType = _spCurrentType.Get();
		(*ppMediaType)->AddRef();
	}

	_mutex.unlock();
	RETURN_HR(hr)
}


// Return the major type GUID.
IFACEMETHODIMP MSWinRTStreamSink::GetMajorType(GUID *pguidMajorType)
{
	ms_message("MSWinRTStreamSink::GetMajorType");
	if (pguidMajorType == nullptr)
		RETURN_HR(E_INVALIDARG)

	if (!_spCurrentType)
		return MF_E_NOT_INITIALIZED;

	*pguidMajorType = MFMediaType_Video;
	return S_OK;
}


// private methods
HRESULT MSWinRTStreamSink::Initialize(MSWinRTMediaSink *pParent)
{
	ms_message("MSWinRTStreamSink::Initialize");
	// Create the event queue helper.
	HRESULT hr = MFCreateEventQueue(&_spEventQueue);

	// Allocate a new work queue for async operations.
	if (SUCCEEDED(hr))
		hr = MFAllocateSerialWorkQueue(MFASYNC_CALLBACK_QUEUE_STANDARD, &_WorkQueueId);

	if (SUCCEEDED(hr)) {
		_spSink = pParent;
		_pParent = pParent;
	}

	RETURN_HR(hr)
}


// Called when the presentation clock starts.
HRESULT MSWinRTStreamSink::Start(MFTIME start)
{
	ms_message("MSWinRTStreamSink::Start");
	_mutex.lock();
	HRESULT hr = ValidateOperation(OpStart);
	if (SUCCEEDED(hr)) {
		if (start != PRESENTATION_CURRENT_POSITION) {
			_StartTime = start;        // Cache the start time.
			_fGetStartTimeFromSample = false;
		} else {
			_fGetStartTimeFromSample = true;
		}
		_state = State_Started;
		_fWaitingForFirstSample = true;
		hr = QueueAsyncOperation(OpStart);
	}
	_mutex.unlock();
	RETURN_HR(hr)
}

// Called when the presentation clock stops.
HRESULT MSWinRTStreamSink::Stop()
{
	ms_message("MSWinRTStreamSink::Stop");
	_mutex.lock();
	HRESULT hr = ValidateOperation(OpStop);
	if (SUCCEEDED(hr)) {
		_state = State_Stopped;
		hr = QueueAsyncOperation(OpStop);
	}
	_mutex.unlock();
	RETURN_HR(hr)
}

// Called when the presentation clock restarts.
HRESULT MSWinRTStreamSink::Restart()
{
	ms_message("MSWinRTStreamSink::Restart");
	_mutex.lock();
	HRESULT hr = ValidateOperation(OpRestart);
	if (SUCCEEDED(hr)) {
		_state = State_Started;
		hr = QueueAsyncOperation(OpRestart);
	}
	_mutex.unlock();
	RETURN_HR(hr)
}

// Class-static matrix of operations vs states.
// If an entry is TRUE, the operation is valid from that state.
BOOL MSWinRTStreamSink::ValidStateMatrix[MSWinRTStreamSink::State_Count][MSWinRTStreamSink::Op_Count] =
{
	// States:    Operations:
	//            SetType   Start     Restart   Pause     Stop      Sample    Marker   
	/* NotSet */  TRUE,     FALSE,    FALSE,    FALSE,    FALSE,    FALSE,    FALSE,

	/* Ready */   TRUE,     TRUE,     FALSE,    TRUE,     TRUE,     FALSE,    TRUE,

	/* Start */   TRUE,     TRUE,     FALSE,    TRUE,     TRUE,     TRUE,     TRUE,

	/* Pause */   TRUE,     TRUE,     TRUE,     TRUE,     TRUE,     TRUE,     TRUE,

	/* Stop */    TRUE,     TRUE,     FALSE,    FALSE,    TRUE,     FALSE,    TRUE,

};

// Checks if an operation is valid in the current state.
HRESULT MSWinRTStreamSink::ValidateOperation(StreamOperation op)
{
	ms_message("MSWinRTStreamSink::ValidateOperation");
	if (ValidStateMatrix[_state][op])
		return S_OK;
	else if (_state == State_TypeNotSet)
		return MF_E_NOT_INITIALIZED;
	else
		return MF_E_INVALIDREQUEST;
}

// Shuts down the stream sink.
HRESULT MSWinRTStreamSink::Shutdown()
{
	ms_message("MSWinRTStreamSink::Shutdown");
	_mutex.lock();
	if (!_IsShutdown) {
		if (_spEventQueue)
			_spEventQueue->Shutdown();

		MFUnlockWorkQueue(_WorkQueueId);
		_SampleQueue.Clear();
		_spSink.Reset();
		_spEventQueue.Reset();
		_spByteStream.Reset();
		_spCurrentType.Reset();
		_IsShutdown = true;
	}
	_mutex.unlock();
	return S_OK;
}


// Puts an async operation on the work queue.
HRESULT MSWinRTStreamSink::QueueAsyncOperation(StreamOperation op)
{
	ms_message("MSWinRTStreamSink::QueueAsyncOperation");
	HRESULT hr = S_OK;
#if 0 // TODO
	ComPtr<CAsyncOperation> spOp;
	spOp.Attach(new CAsyncOperation(op)); // Created with ref count = 1
	if (!spOp)
		hr = E_OUTOFMEMORY;
	if (SUCCEEDED(hr))
		hr = MFPutWorkItem2(_WorkQueueId, 0, &_WorkQueueCB, spOp.Get());
#endif
	RETURN_HR(hr)
}

HRESULT MSWinRTStreamSink::OnDispatchWorkItem(IMFAsyncResult *pAsyncResult)
{
	ms_message("MSWinRTStreamSink::OnDispatchWorkItem");
	// Called by work queue thread. Need to hold the critical section.
	_mutex.lock();

	try {
		ComPtr<IUnknown> spState;
		HRESULT hr = pAsyncResult->GetState(&spState);
		if (FAILED(hr)) {
			_mutex.unlock();
			throw ref new Exception(hr);
		}

		// The state object is a CAsncOperation object.
		CAsyncOperation *pOp = static_cast<CAsyncOperation *>(spState.Get());
		StreamOperation op = pOp->m_op;

		switch (op) {
		case OpStart:
		case OpRestart:
			// Send MEStreamSinkStarted.
			hr = QueueEvent(MEStreamSinkStarted, GUID_NULL, S_OK, nullptr);
			if (FAILED(hr)) {
				_mutex.unlock();
				throw ref new Exception(hr);
			}

			// There might be samples queue from earlier (ie, while paused).
			bool fRequestMoreSamples;
			if (!_Connected) {
				// Just drop samples if we are not connected
				fRequestMoreSamples = DropSamplesFromQueue();
			} else {
				fRequestMoreSamples = SendSampleFromQueue();
			}
			if (fRequestMoreSamples) {
				// If false there is no samples in the queue now so request one
				hr = QueueEvent(MEStreamSinkRequestSample, GUID_NULL, S_OK, nullptr);
				if (FAILED(hr)) {
					_mutex.unlock();
					throw ref new Exception(hr);
				}
			}
			break;

		case OpStop:
			// Drop samples from queue.
			DropSamplesFromQueue();

			// Send the event even if the previous call failed.
			hr = QueueEvent(MEStreamSinkStopped, GUID_NULL, S_OK, nullptr);
			if (FAILED(hr)) {
				_mutex.unlock();
				throw ref new Exception(hr);
			}
			break;

		case OpPause:
			hr = QueueEvent(MEStreamSinkPaused, GUID_NULL, S_OK, nullptr);
			if (FAILED(hr)) {
				_mutex.unlock();
				throw ref new Exception(hr);
			}
			break;

		case OpProcessSample:
		case OpPlaceMarker:
		case OpSetMediaType:
			DispatchProcessSample(pOp);
			break;
		}
	} catch (Exception ^exc) {
		HandleError(exc->HResult);
	}

	_mutex.unlock();
	return S_OK;
}

// Complete a ProcessSample or PlaceMarker request.
void MSWinRTStreamSink::DispatchProcessSample(CAsyncOperation *pOp)
{
	ms_message("MSWinRTStreamSink::DispatchProcessSample");
	bool fRequestMoreSamples = false;
	if (!_Connected)
		fRequestMoreSamples = DropSamplesFromQueue();
	else
		fRequestMoreSamples = SendSampleFromQueue();

	// Ask for another sample
	if (fRequestMoreSamples) {
		if (pOp->m_op == OpProcessSample) {
			HRESULT hr = QueueEvent(MEStreamSinkRequestSample, GUID_NULL, S_OK, nullptr);
			if (FAILED(hr))
				throw ref new Exception(hr);
		}
	}
}

// Drop samples in the queue
bool MSWinRTStreamSink::DropSamplesFromQueue()
{
	ms_message("MSWinRTStreamSink::DropSamplesFromQueue");
	ProcessSamplesFromQueue(true);
	return true;
}

// Send sample from the queue
bool MSWinRTStreamSink::SendSampleFromQueue()
{
	ms_message("MSWinRTStreamSink::SendSampleFromQueue");
	return ProcessSamplesFromQueue(false);
}

bool MSWinRTStreamSink::ProcessSamplesFromQueue(bool fFlush)
{
	ms_message("MSWinRTStreamSink::ProcessSamplesFromQueue");
	bool fNeedMoreSamples = false;
#if 0 // TODO
	ComPtr<IUnknown> spunkSample;
	bool fSendSamples = true;
	bool fSendEOS = false;

	if (FAILED(_SampleQueue.RemoveFront(&spunkSample))) {
		fNeedMoreSamples = true;
		fSendSamples = false;
	}

	while (fSendSamples) {
		ComPtr<IMFSample> spSample;
		ComPtr<IBufferPacket> spPacket;
		bool fProcessingSample = false;

		// Figure out if this is a marker or a sample.
		// If this is a sample, write it to the file.
		// Now handle the sample/marker appropriately.
		if (SUCCEEDED(spunkSample.As(&spSample))) {
			if (!fFlush) {
				// Prepare sample for sending
				spPacket = PrepareSample(spSample.Get(), false);
				fProcessingSample = true;
			}
		} else {
			ComPtr<IMarker> spMarker;
			// Check if it is a marker
			if (SUCCEEDED(spunkSample.As(&spMarker))) {
				MFSTREAMSINK_MARKER_TYPE markerType;
				PROPVARIANT var;
				PropVariantInit(&var);
				ThrowIfError(spMarker->GetMarkerType(&markerType));
				// Get the context data.
				ThrowIfError(spMarker->GetContext(&var));

				HRESULT hr = QueueEvent(MEStreamSinkMarker, GUID_NULL, S_OK, &var);
				PropVariantClear(&var);
				if (FAILED(hr))
					throw ref new Exception(hr);

				if (markerType == MFSTREAMSINK_MARKER_ENDOFSEGMENT)
					fSendEOS = true;
			} else {
				ComPtr<IMFMediaType> spType;
				ThrowIfError(spunkSample.As(&spType));
				if (!fFlush)
					spPacket = PrepareFormatChange(spType.Get());
			}
		}

		if (spPacket) {
			ComPtr<MSWinRTStreamSink> spThis = this;
			// Send the sample
			concurrency::create_task(_networkSender->SendAsync(spPacket.Get())).then([this, spThis, fProcessingSample](concurrency::task<void>& sendTask)
			{
				AutoLock lock(_critSec);
				try
				{
					sendTask.get();
					ThrowIfError(CheckShutdown());
					if (_state == State_Started && fProcessingSample)
					{
						// If we are still in started state request another sample
						ThrowIfError(QueueEvent(MEStreamSinkRequestSample, GUID_NULL, S_OK, nullptr));
					}
				}
				catch (Exception ^exc)
				{
					HandleError(exc->HResult);
				}
			});
			// We stop if we processed a sample otherwise keep looking
			fSendSamples = !fProcessingSample;
		}

		if (fSendSamples)
		{
			if (FAILED(_SampleQueue.RemoveFront(spunkSample.ReleaseAndGetAddressOf())))
			{
				fNeedMoreSamples = true;
				fSendSamples = false;
			}
		}

	}

	if (fSendEOS)
	{
		ComPtr<CMediaSink> spParent = _pParent;
		concurrency::create_task([spParent]() {
			spParent->ReportEndOfStream();
		});
	}
#endif
	return fNeedMoreSamples;
}

// Processing format change
void MSWinRTStreamSink::ProcessFormatChange(IMFMediaType *pMediaType)
{
	ms_message("MSWinRTStreamSink::ProcessFormatChange");
	// Add the media type to the sample queue.
	HRESULT hr = _SampleQueue.InsertBack(pMediaType);
	if (FAILED(hr))
		throw ref new Exception(hr);

	// Unless we are paused, start an async operation to dispatch the next sample.
	// Queue the operation.
	hr = QueueAsyncOperation(OpSetMediaType);
	if (FAILED(hr))
		throw ref new Exception(hr);
}

void MSWinRTStreamSink::HandleError(HRESULT hr)
{
	ms_message("MSWinRTStreamSink::HandleError");
	if (!_IsShutdown)
		QueueEvent(MEError, GUID_NULL, hr, nullptr);
}






MSWinRTMediaSink::MSWinRTMediaSink()
	: _cRef(1)
	, _IsShutdown(false)
	, _IsConnected(false)
	, _llStartTime(0)
	, _cStreamsEnded(0)
	, _waitingConnectionId(0)
{
	ms_message("MSWinRTMediaSink constructor");
}

MSWinRTMediaSink::~MSWinRTMediaSink()
{
	ms_message("MSWinRTMediaSink destructor");
}

HRESULT MSWinRTMediaSink::RuntimeClassInitialize(/*ISinkCallback ^callback,*/ Windows::Media::MediaProperties::IMediaEncodingProperties ^videoEncodingProperties)
{
	ms_message("MSWinRTMediaSink::RuntimeClassInitialize");
	try
	{
#if 0 // TODO
		_callback = callback;
#endif
		SetVideoStreamProperties(videoEncodingProperties);
	}
	catch (Exception ^exc)
	{
#if 0 // TODO
		_callback = nullptr;
#endif
		return exc->HResult;
	}

	return S_OK;
}

void MSWinRTMediaSink::SetVideoStreamProperties(_In_opt_ Windows::Media::MediaProperties::IMediaEncodingProperties ^mediaEncodingProperties)
{
	ms_message("MSWinRTMediaSink::SetVideoStreamProperties");
	RemoveStreamSink(0);
	if (mediaEncodingProperties != nullptr)
	{
		ComPtr<IMFStreamSink> spStreamSink;
		ComPtr<IMFMediaType> spMediaType;
		ConvertPropertiesToMediaType(mediaEncodingProperties, &spMediaType);
		HRESULT hr = AddStreamSink(0, spMediaType.Get(), spStreamSink.GetAddressOf());
		if (FAILED(hr))
			throw ref new Exception(hr);
	}
}

///  IMFMediaSink
IFACEMETHODIMP MSWinRTMediaSink::GetCharacteristics(DWORD *pdwCharacteristics)
{
	ms_message("MSWinRTMediaSink::GetCharacteristics");
	if (pdwCharacteristics == NULL)
		RETURN_HR(E_INVALIDARG)

	_mutex.lock();
	HRESULT hr = CheckShutdown();
	if (SUCCEEDED(hr)) {
		// Rateless sink.
		*pdwCharacteristics = MEDIASINK_RATELESS;
	}
	_mutex.unlock();
	RETURN_HR(hr)
}

IFACEMETHODIMP MSWinRTMediaSink::AddStreamSink(DWORD dwStreamSinkIdentifier, IMFMediaType *pMediaType, IMFStreamSink **ppStreamSink)
{
	ms_message("MSWinRTMediaSink::AddStreamSink(dwStreamSinkIdentifier=%d)", dwStreamSinkIdentifier);
	MSWinRTStreamSink *pStream = nullptr;
	ComPtr<IMFStreamSink> spMFStream;

	_mutex.lock();
	HRESULT hr = CheckShutdown();
	if (SUCCEEDED(hr)) {
		hr = GetStreamSinkById(dwStreamSinkIdentifier, &spMFStream);
	}
	if (SUCCEEDED(hr))
		hr = MF_E_STREAMSINK_EXISTS;
	else
		hr = S_OK;

	if (SUCCEEDED(hr)) {
		pStream = new MSWinRTStreamSink(dwStreamSinkIdentifier);
		if (pStream == nullptr)
			hr = E_OUTOFMEMORY;
		spMFStream.Attach(pStream);
	}

	// Initialize the stream.
	if (SUCCEEDED(hr))
		hr = pStream->Initialize(this);
	if (SUCCEEDED(hr) && pMediaType != nullptr)
		hr = pStream->SetCurrentMediaType(pMediaType);
	if (SUCCEEDED(hr))
		hr = _streams.InsertFront(pStream);
	if (SUCCEEDED(hr))
		*ppStreamSink = spMFStream.Detach();

	_mutex.unlock();
	RETURN_HR(hr)
}

IFACEMETHODIMP MSWinRTMediaSink::RemoveStreamSink(DWORD dwStreamSinkIdentifier)
{
	ms_message("MSWinRTMediaSink::RemoveStreamSink(dwStreamSinkIdentifier=%d)", dwStreamSinkIdentifier);
	_mutex.lock();
	HRESULT hr = CheckShutdown();
	//StreamContainer::POSITION pos = _streams.FrontPosition();
	//StreamContainer::POSITION endPos = _streams.EndPosition();
	ComPtr<IMFStreamSink> spStream;

	if (SUCCEEDED(hr)) {
		hr = _streams.GetFront(&spStream);
		if (SUCCEEDED(hr)) {
			DWORD dwId;
			hr = spStream->GetIdentifier(&dwId);
		} else {
			hr = MF_E_INVALIDSTREAMNUMBER;
		}
	}

	if (SUCCEEDED(hr)) {
		hr = _streams.RemoveFront(nullptr);
		static_cast<MSWinRTStreamSink *>(spStream.Get())->Shutdown();
	}

	_mutex.unlock();
	RETURN_HR(hr)
}

IFACEMETHODIMP MSWinRTMediaSink::GetStreamSinkCount(_Out_ DWORD *pcStreamSinkCount)
{
	ms_message("MSWinRTMediaSink::GetStreamSinkCount");
	if (pcStreamSinkCount == NULL)
		RETURN_HR(E_INVALIDARG)

	_mutex.lock();
	HRESULT hr = CheckShutdown();
	if (SUCCEEDED(hr))
		*pcStreamSinkCount = 1;

	_mutex.unlock();
	RETURN_HR(hr)
}

IFACEMETHODIMP MSWinRTMediaSink::GetStreamSinkByIndex(DWORD dwIndex, _Outptr_ IMFStreamSink **ppStreamSink)
{
	ms_message("MSWinRTMediaSink::GetStreamSinkByIndex(dwIndex=%d)", dwIndex);
	if (ppStreamSink == NULL)
		RETURN_HR(E_INVALIDARG)

	ComPtr<IMFStreamSink> spStream;
	_mutex.lock();

	if (dwIndex >= 1)
		return MF_E_INVALIDINDEX;

	HRESULT hr = CheckShutdown();
	if (SUCCEEDED(hr))
		hr = _streams.GetFront(&spStream);
	if (SUCCEEDED(hr))
		*ppStreamSink = spStream.Detach();

	_mutex.unlock();
	RETURN_HR(hr)
}

IFACEMETHODIMP MSWinRTMediaSink::GetStreamSinkById(DWORD dwStreamSinkIdentifier, IMFStreamSink **ppStreamSink)
{
	ms_message("MSWinRTMediaSink::GetStreamSinkById(dwStreamSinkIdentifier=%d)", dwStreamSinkIdentifier);
	if (ppStreamSink == NULL)
		RETURN_HR(E_INVALIDARG)

	_mutex.lock();
	HRESULT hr = CheckShutdown();
	ComPtr<IMFStreamSink> spResult;
	if (SUCCEEDED(hr)) {
		DWORD dwId;
		ComPtr<IMFStreamSink> spStream;
		hr = _streams.GetFront(&spStream);
		if (SUCCEEDED(hr)) {
			hr = spStream->GetIdentifier(&dwId);
			if (SUCCEEDED(hr)) {
				if (dwId == dwStreamSinkIdentifier)
					spResult = spStream;
				else
					hr = MF_E_INVALIDSTREAMNUMBER;
			}
		} else {
			hr = MF_E_INVALIDSTREAMNUMBER;
		}
	}
	if (SUCCEEDED(hr)) {
		*ppStreamSink = spResult.Detach();
	}

	_mutex.unlock();
	RETURN_HR(hr)
}

IFACEMETHODIMP MSWinRTMediaSink::SetPresentationClock(IMFPresentationClock *pPresentationClock)
{
	ms_message("MSWinRTMediaSink::SetPresentationClock");
	_mutex.lock();
	HRESULT hr = CheckShutdown();

	// If we already have a clock, remove ourselves from that clock's
	// state notifications.
	if (SUCCEEDED(hr)) {
		if (_spClock)
			hr = _spClock->RemoveClockStateSink(this);
	}

	// Register ourselves to get state notifications from the new clock.
	if (SUCCEEDED(hr)) {
		if (pPresentationClock)
			hr = pPresentationClock->AddClockStateSink(this);
	}

	if (SUCCEEDED(hr)) {
		// Release the pointer to the old clock.
		// Store the pointer to the new clock.
		_spClock = pPresentationClock;
	}

	_mutex.unlock();
	RETURN_HR(hr)
}

IFACEMETHODIMP MSWinRTMediaSink::GetPresentationClock(IMFPresentationClock **ppPresentationClock)
{
	ms_message("MSWinRTMediaSink::GetPresentationClock");
	if (ppPresentationClock == NULL)
		RETURN_HR(E_INVALIDARG)

	_mutex.lock();
	HRESULT hr = CheckShutdown();
	if (SUCCEEDED(hr)) {
		if (_spClock == NULL)
			hr = MF_E_NO_CLOCK; // There is no presentation clock.
		else {
			// Return the pointer to the caller.
			*ppPresentationClock = _spClock.Get();
			(*ppPresentationClock)->AddRef();
		}
	}

	_mutex.unlock();
	RETURN_HR(hr)
}

IFACEMETHODIMP MSWinRTMediaSink::Shutdown()
{
	ms_message("MSWinRTMediaSink::Shutdown");
#if 0 // TODO
	ISinkCallback ^callback;
#endif
	_mutex.lock();
	HRESULT hr = CheckShutdown();
	if (SUCCEEDED(hr)) {
#if 0 // TODO
		ForEach(_streams, ShutdownFunc());
		_streams.Clear();

		if (_networkSender != nullptr)
		{
			_networkSender->Close();
		}

		_networkSender = nullptr;
#endif
		_spClock.Reset();
		_IsShutdown = true;
#if 0 // TODO
		callback = _callback;
#endif
	}
	_mutex.unlock();

#if 0 // TODO
	if (callback != nullptr)
		callback->OnShutdown();
#endif

	return S_OK;
}

// IMFClockStateSink
IFACEMETHODIMP MSWinRTMediaSink::OnClockStart(MFTIME hnsSystemTime, LONGLONG llClockStartOffset)
{
	ms_message("MSWinRTMediaSink::OnClockStart");
	_mutex.lock();
	HRESULT hr = CheckShutdown();
	if (SUCCEEDED(hr)) {
#if 0 // TODO
		TRACE(TRACE_LEVEL_LOW, L"OnClockStart ts=%I64d\n", llClockStartOffset);
		// Start each stream.
		_llStartTime = llClockStartOffset;
		hr = ForEach(_streams, StartFunc(llClockStartOffset));
#endif
	}

	_mutex.unlock();
	RETURN_HR(hr)
}

IFACEMETHODIMP MSWinRTMediaSink::OnClockStop(MFTIME hnsSystemTime)
{
	ms_message("MSWinRTMediaSink::OnClockStop");
	_mutex.lock();
	HRESULT hr = CheckShutdown();
	if (SUCCEEDED(hr)) {
		// Stop each stream
#if 0 // TODO
		hr = ForEach(_streams, StopFunc());
#endif
	}

	_mutex.unlock();
	RETURN_HR(hr)
}


IFACEMETHODIMP MSWinRTMediaSink::OnClockPause(MFTIME hnsSystemTime)
{
	ms_message("MSWinRTMediaSink::OnClockPause");
	return MF_E_INVALID_STATE_TRANSITION;
}

IFACEMETHODIMP MSWinRTMediaSink::OnClockRestart(MFTIME hnsSystemTime)
{
	ms_message("MSWinRTMediaSink::OnClockRestart");
	return MF_E_INVALID_STATE_TRANSITION;
}

IFACEMETHODIMP MSWinRTMediaSink::OnClockSetRate(/* [in] */ MFTIME hnsSystemTime, /* [in] */ float flRate)
{
	ms_message("MSWinRTMediaSink::OnClockSetRate");
	return S_OK;
}

void MSWinRTMediaSink::ReportEndOfStream()
{
	ms_message("MSWinRTMediaSink::ReportEndOfStream");
	_mutex.lock();
	++_cStreamsEnded;
	_mutex.unlock();
}

#if 0
/// Private methods
// Start listening on the network server
void CMediaSink::StartListening()
{
	ComPtr<CMediaSink> spThis = this;
	concurrency::create_task(safe_cast<INetworkServer^>(_networkSender)->AcceptAsync()).then([spThis, this](concurrency::task<StreamSocketInformation^>& acceptTask)
	{
		IncomingConnectionEventArgs ^args;
		ISinkCallback ^callback;
		try
		{
			{
				AutoLock lock(_critSec);
				auto info = acceptTask.get();
				ThrowIfError(CheckShutdown());
				if (_callback == nullptr)
				{
					Throw(E_UNEXPECTED);
				}
				_remoteUrl = PrepareRemoteUrl(info);

				_waitingConnectionId = LODWORD(GetTickCount64());
				if (_waitingConnectionId == 0)
				{
					++_waitingConnectionId;
				}

				args = ref new IncomingConnectionEventArgs(this, _waitingConnectionId, _remoteUrl);
				callback = _callback;
			}

			callback->FireIncomingConnection(args);
		}
		catch (Exception ^exc)
		{
			AutoLock lock(_critSec);
			HandleError(exc->HResult);
		}
	});
}

void CMediaSink::StartReceiving(IMediaBufferWrapper *pReceiveBuffer)
{
	ComPtr<CMediaSink> spThis = this;
	ComPtr<IMediaBufferWrapper> spReceiveBuffer = pReceiveBuffer;

	concurrency::create_task(_networkSender->ReceiveAsync(pReceiveBuffer)).then([spThis, spReceiveBuffer, this](concurrency::task<void>& task)
	{
		AutoLock lock(_critSec);
		try
		{
			StspOperation eOp = StspOperation_Unknown;
			task.get();
			ThrowIfError(CheckShutdown());

			BYTE *pBuf = spReceiveBuffer->GetBuffer();
			DWORD cbCurrentLen;
			spReceiveBuffer->GetMediaBuffer()->GetCurrentLength(&cbCurrentLen);

			// Validate if the data received from the client is sufficient to fit operation header which is the smallest size of message that we can handle.
			if (cbCurrentLen != sizeof(StspOperationHeader))
			{
				Throw(MF_E_INVALID_FORMAT);
			}

			StspOperationHeader *pOpHeader = reinterpret_cast<StspOperationHeader *>(pBuf);
			// We only support client's request for media description
			if (pOpHeader->cbDataSize != 0 ||
				((pOpHeader->eOperation != StspOperation_ClientRequestDescription) &&
					(pOpHeader->eOperation != StspOperation_ClientRequestStart) &&
					(pOpHeader->eOperation != StspOperation_ClientRequestStop)))
			{
				Throw(MF_E_INVALID_FORMAT);
			}
			else
			{
				eOp = pOpHeader->eOperation;
			}

			switch (eOp)
			{
			case StspOperation_ClientRequestDescription:
				// Send description to the client
				SendDescription();
				break;
			case StspOperation_ClientRequestStart:
			{
				_IsConnected = true;
				if (_spClock)
				{
					ThrowIfError(_spClock->GetTime(&_llStartTime));
				}

				// We are now connected we can start streaming.
				ForEach(_streams, SetConnectedFunc(true, _llStartTime));
			}
			break;
			default:
				Throw(MF_E_INVALID_FORMAT);
				break;
			}
		}
		catch (Exception ^exc)
		{
			HandleError(exc->HResult);
		}
	});
}

// Send packet
concurrency::task<void> CMediaSink::SendPacket(Network::IBufferPacket *pPacket)
{
	return concurrency::create_task(_networkSender->SendAsync(pPacket));
}

// Send media description to the client
void CMediaSink::SendDescription()
{
	// Size of the description buffer
	const DWORD c_cStreams = _streams.GetCount();
	const DWORD c_cbDescriptionSize = sizeof(StspDescription) + (c_cStreams - 1) * sizeof(StspStreamDescription);
	const DWORD c_cbPacketSize = sizeof(StspOperationHeader) + c_cbDescriptionSize;

	ComPtr<IMediaBufferWrapper> spBuffer;
	ComPtr<IMediaBufferWrapper> *arrspAttributes = new ComPtr<IMediaBufferWrapper>[c_cStreams];
	if (arrspAttributes == nullptr)
	{
		Throw(E_OUTOFMEMORY);
	}

	try
	{
		// Create send buffer
		ThrowIfError(CreateMediaBufferWrapper(c_cbPacketSize, &spBuffer));

		// Prepare operation header
		BYTE *pBuf = spBuffer->GetBuffer();
		StspOperationHeader *pOpHeader = reinterpret_cast<StspOperationHeader *>(pBuf);
		pOpHeader->cbDataSize = c_cbDescriptionSize;
		pOpHeader->eOperation = StspOperation_ServerDescription;

		// Prepare description
		StspDescription *pDescription = reinterpret_cast<StspDescription *>(pBuf + sizeof(StspOperationHeader));
		pDescription->cNumStreams = c_cStreams;

		StreamContainer::POSITION pos = _streams.FrontPosition();
		StreamContainer::POSITION endPos = _streams.EndPosition();
		DWORD nStream = 0;
		for (;pos != endPos; pos = _streams.Next(pos), ++nStream)
		{
			ComPtr<IMFStreamSink> spStream;
			ThrowIfError(_streams.GetItemPos(pos, &spStream));

			// Fill out stream description
			arrspAttributes[nStream] = FillStreamDescription(static_cast<CStreamSink *>(spStream.Get()), &pDescription->aStreams[nStream]);

			// Add size of variable size attribute blob to size of the package.
			pOpHeader->cbDataSize += pDescription->aStreams[nStream].cbAttributesSize;
		}

		// Set length of the packet
		ThrowIfError(spBuffer->SetCurrentLength(c_cbPacketSize));

		// Prepare packet to send
		ComPtr<IBufferPacket> spPacket;

		ThrowIfError(CreateBufferPacket(&spPacket));
		// Add fixed size header and description to the packet
		ThrowIfError(spPacket->AddBuffer(spBuffer.Get()));

		for (DWORD nStream = 0; nStream < c_cStreams; ++nStream)
		{
			// Add variable size attributes.
			ThrowIfError(spPacket->AddBuffer(arrspAttributes[nStream].Get()));
		}

		ComPtr<CMediaSink> spThis = this;
		// Send the data.
		SendPacket(spPacket.Get()).then([this, spThis](concurrency::task<void>& task)
		{
			try
			{
				task.get();
			}
			catch (Exception ^exc)
			{
				AutoLock lock(_critSec);
				HandleError(exc->HResult);
			}
		});

		// Keep receiving
		StartReceiving(_spReceiveBuffer.Get());

		delete[] arrspAttributes;
	}
	catch (Exception ^exc)
	{
		delete[] arrspAttributes;
		throw;
	}
}

// Fill stream description and prepare attributes blob.
ComPtr<Network::IMediaBufferWrapper> CMediaSink::FillStreamDescription(CStreamSink *pStream, StspStreamDescription *pStreamDescription)
{
	assert(pStream != nullptr);
	assert(pStreamDescription != nullptr);

	ComPtr<IMFMediaType> spMediaType;

	// Get current media type
	ThrowIfError(pStream->GetCurrentMediaType(&spMediaType));

	return pStream->FillStreamDescription(spMediaType.Get(), pStreamDescription);
}

void CMediaSink::HandleError(HRESULT hr)
{
	Shutdown();
}

String ^CMediaSink::PrepareRemoteUrl(StreamSocketInformation ^info)
{
	WCHAR szBuffer[MAX_PATH];

	if (info->RemoteHostName->Type == HostNameType::Ipv4 || info->RemoteHostName->Type == HostNameType::DomainName)
	{
		ThrowIfError(StringCchPrintf(szBuffer, _countof(szBuffer), L"%s://%s", c_szStspScheme, info->RemoteHostName->RawName->Data()));
	}
	else if (info->RemoteHostName->Type == HostNameType::Ipv6)
	{
		ThrowIfError(StringCchPrintf(szBuffer, _countof(szBuffer), L"%s://[%s]", c_szStspScheme, info->RemoteHostName->RawName->Data()));
	}
	else
	{
		throw ref new InvalidArgumentException();
	}

	return ref new String(szBuffer);
}
#endif




MSWinRTMediaSinkProxy::MSWinRTMediaSinkProxy()
{
}

MSWinRTMediaSinkProxy::~MSWinRTMediaSinkProxy()
{
	_mutex.lock();
	if (_spMediaSink != nullptr) {
		_spMediaSink->Shutdown();
		_spMediaSink = nullptr;
	}
	_mutex.unlock();
}

Windows::Media::IMediaExtension ^MSWinRTMediaSinkProxy::GetMFExtensions() {
	_mutex.lock();
	if (_spMediaSink == nullptr) {
		_mutex.unlock();
		throw ref new Exception(MF_E_NOT_INITIALIZED);
	}
	ComPtr<IInspectable> spInspectable;
	HRESULT hr = _spMediaSink.As(&spInspectable);
	if (FAILED(hr)) {
		_mutex.unlock();
		throw ref new Exception(hr);
	}
	_mutex.unlock();
	return safe_cast<IMediaExtension^>(reinterpret_cast<Object^>(spInspectable.Get()));
}


Windows::Foundation::IAsyncOperation<IMediaExtension^>^ MSWinRTMediaSinkProxy::InitializeAsync(Windows::Media::MediaProperties::IMediaEncodingProperties ^videoEncodingProperties)
{
	return concurrency::create_async([this, videoEncodingProperties]()
	{
		_mutex.lock();
		CheckShutdown();
		if (_spMediaSink != nullptr) {
			_mutex.unlock();
			throw ref new Exception(MF_E_ALREADY_INITIALIZED);
		}
		// Prepare the MF extension
		HRESULT hr = MakeAndInitialize<MSWinRTMediaSink>(&_spMediaSink, /*ref new StspSinkCallback(this), */ videoEncodingProperties);
		if (FAILED(hr)) {
			_mutex.unlock();
			throw ref new Exception(hr);
		}
		ComPtr<IInspectable> spInspectable;
		hr = _spMediaSink.As(&spInspectable);
		if (FAILED(hr)) {
			_mutex.unlock();
			throw ref new Exception(hr);
		}
		_mutex.unlock();
		return safe_cast<IMediaExtension^>(reinterpret_cast<Object^>(spInspectable.Get()));
	});
}

void MSWinRTMediaSinkProxy::OnShutdown()
{
	_mutex.lock();
	if (_fShutdown)	{
		_mutex.unlock();
		return;
	}
	_fShutdown = true;
	_spMediaSink = nullptr;
	_mutex.unlock();
}
