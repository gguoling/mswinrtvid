#pragma once

#include <windows.h>
#include <mfidl.h>
#include <mfapi.h>
#include <mferror.h>

#include <mutex>
#include <collection.h>
#include <windows.foundation.h>
#include <windows.foundation.collections.h>
#include <windows.media.h>
#include <windows.media.capture.h>
#include <windows.media.mediaproperties.h>
#include <wrl\implements.h>
#include <wrl\ftm.h>
#include <ppltasks.h>

#include "LinkList.h"

using namespace Platform;
using namespace Microsoft::WRL;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Storage::Streams;
using namespace Windows::Networking;
using namespace Windows::Networking::Sockets;
using namespace Windows::Media;
using namespace Windows::Media::Capture;
using namespace Windows::Media::MediaProperties;


namespace libmswinrtvid {
	//interface class ISinkCallback;
	class MSWinRTMediaSink;


	class MSWinRTStreamSink : public IMFStreamSink, public IMFMediaTypeHandler
	{
	public:
		// State enum: Defines the current state of the stream.
		enum State
		{
			State_TypeNotSet = 0,    // No media type is set
			State_Ready,             // Media type is set, Start has never been called.
			State_Started,
			State_Stopped,
			State_Paused,
			State_Count              // Number of states
		};

		// StreamOperation: Defines various operations that can be performed on the stream.
		enum StreamOperation
		{
			OpSetMediaType = 0,
			OpStart,
			OpRestart,
			OpPause,
			OpStop,
			OpProcessSample,
			OpPlaceMarker,

			Op_Count                // Number of operations
		};

		// CAsyncOperation:
		// Used to queue asynchronous operations. When we call MFPutWorkItem, we use this
		// object for the callback state (pState). Then, when the callback is invoked,
		// we can use the object to determine which asynchronous operation to perform.

		class CAsyncOperation : public IUnknown
		{
		public:
			CAsyncOperation(StreamOperation op);

			StreamOperation m_op;   // The operation to perform.

									// IUnknown methods.
			STDMETHODIMP QueryInterface(REFIID iid, void **ppv);
			STDMETHODIMP_(ULONG) AddRef();
			STDMETHODIMP_(ULONG) Release();

		private:
			long _cRef;
			virtual ~CAsyncOperation();
		};

	public:
		// IUnknown
		IFACEMETHOD(QueryInterface) (REFIID riid, void **ppv);
		IFACEMETHOD_(ULONG, AddRef) ();
		IFACEMETHOD_(ULONG, Release) ();

		// IMFMediaEventGenerator
		IFACEMETHOD(BeginGetEvent) (IMFAsyncCallback *pCallback, IUnknown *punkState);
		IFACEMETHOD(EndGetEvent) (IMFAsyncResult *pResult, IMFMediaEvent **ppEvent);
		IFACEMETHOD(GetEvent) (DWORD dwFlags, IMFMediaEvent **ppEvent);
		IFACEMETHOD(QueueEvent) (MediaEventType met, REFGUID guidExtendedType, HRESULT hrStatus, PROPVARIANT const *pvValue);

		// IMFStreamSink
		IFACEMETHOD(GetMediaSink) (IMFMediaSink **ppMediaSink);
		IFACEMETHOD(GetIdentifier) (DWORD *pdwIdentifier);
		IFACEMETHOD(GetMediaTypeHandler) (IMFMediaTypeHandler **ppHandler);
		IFACEMETHOD(ProcessSample) (IMFSample *pSample);
		IFACEMETHOD(PlaceMarker) (/* [in] */ MFSTREAMSINK_MARKER_TYPE eMarkerType, /* [in] */ PROPVARIANT const *pvarMarkerValue, /* [in] */ PROPVARIANT const *pvarContextValue);
		IFACEMETHOD(Flush)();

		// IMFMediaTypeHandler
		IFACEMETHOD(IsMediaTypeSupported) (IMFMediaType *pMediaType, IMFMediaType **ppMediaType);
		IFACEMETHOD(GetMediaTypeCount) (DWORD *pdwTypeCount);
		IFACEMETHOD(GetMediaTypeByIndex) (DWORD dwIndex, IMFMediaType **ppType);
		IFACEMETHOD(SetCurrentMediaType) (IMFMediaType *pMediaType);
		IFACEMETHOD(GetCurrentMediaType) (IMFMediaType **ppMediaType);
		IFACEMETHOD(GetMajorType) (GUID *pguidMajorType);

		// ValidStateMatrix: Defines a look-up table that says which operations
		// are valid from which states.
		static BOOL ValidStateMatrix[State_Count][Op_Count];


		MSWinRTStreamSink(DWORD dwIdentifier);
		virtual ~MSWinRTStreamSink();

		HRESULT Initialize(MSWinRTMediaSink *pParent);

		HRESULT CheckShutdown() const
		{
			if (_IsShutdown)
				return MF_E_SHUTDOWN;
			return S_OK;
		}

		HRESULT     Start(MFTIME start);
		HRESULT     Restart();
		HRESULT     Stop();
		HRESULT     Shutdown();

	private:
		HRESULT     ValidateOperation(StreamOperation op);
		HRESULT     QueueAsyncOperation(StreamOperation op);
		HRESULT     OnDispatchWorkItem(IMFAsyncResult *pAsyncResult);
		void        DispatchProcessSample(CAsyncOperation *pOp);
		bool        DropSamplesFromQueue();
		bool        SendSampleFromQueue();
		bool        ProcessSamplesFromQueue(bool fFlush);
		void        ProcessFormatChange(IMFMediaType *pMediaType);
		void        HandleError(HRESULT hr);

	private:
		std::recursive_mutex _mutex;

		long                        _cRef;                      // reference count
		//CritSec                     _critSec;                   // critical section for thread safety

		DWORD                       _dwIdentifier;
		State                       _state;
		bool                        _IsShutdown;                // Flag to indicate if Shutdown() method was called.
		bool                        _Connected;
		bool                        _fGetStartTimeFromSample;
		bool                        _fWaitingForFirstSample;
		bool                        _fFirstSampleAfterConnect;
		GUID                        _guiCurrentSubtype;

		DWORD                       _WorkQueueId;               // ID of the work queue for asynchronous operations.
		MFTIME                      _StartTime;                 // Presentation time when the clock started.

		ComPtr<IMFMediaSink>        _spSink;                    // Parent media sink
		MSWinRTMediaSink                  *_pParent;

		ComPtr<IMFMediaEventQueue>  _spEventQueue;              // Event queue
		ComPtr<IMFByteStream>       _spByteStream;              // Bytestream where we write the data.
		ComPtr<IMFMediaType>        _spCurrentType;
		ComPtr<IMFSample>           _spFirstVideoSample;

		ComPtrList<IUnknown>        _SampleQueue;               // Queue to hold samples and markers.
																// Applies to: ProcessSample, PlaceMarker

		//Network::INetworkChannel^    _networkSender;

		//AsyncCallback<CStreamSink>  _WorkQueueCB;              // Callback for the work queue.

		ComPtr<IUnknown>            _spFTM;
	};


	class MSWinRTMediaSink
		: public Microsoft::WRL::RuntimeClass<
			Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::RuntimeClassType::WinRtClassicComMix>,
			ABI::Windows::Media::IMediaExtension,
			FtmBase,
			IMFMediaSink,
			IMFClockStateSink>
	{
		InspectableClass(L"libmswinrtvid::MSWinRTMediaSink", BaseTrust)

	public:
		MSWinRTMediaSink();
		~MSWinRTMediaSink();

		HRESULT RuntimeClassInitialize(/*ISinkCallback ^callback,*/ Windows::Media::MediaProperties::IMediaEncodingProperties ^videoEncodingProperties);

		// IMediaExtension
		IFACEMETHOD(SetProperties) (ABI::Windows::Foundation::Collections::IPropertySet *pConfiguration) { return S_OK; }

		// IMFMediaSink methods
		IFACEMETHOD(GetCharacteristics) (DWORD *pdwCharacteristics);
		IFACEMETHOD(AddStreamSink)(/* [in] */ DWORD dwStreamSinkIdentifier, /* [in] */ IMFMediaType *pMediaType, /* [out] */ IMFStreamSink **ppStreamSink);
		IFACEMETHOD(RemoveStreamSink) (DWORD dwStreamSinkIdentifier);
		IFACEMETHOD(GetStreamSinkCount) (_Out_ DWORD *pcStreamSinkCount);
		IFACEMETHOD(GetStreamSinkByIndex) (DWORD dwIndex, _Outptr_ IMFStreamSink **ppStreamSink);
		IFACEMETHOD(GetStreamSinkById) (DWORD dwIdentifier, IMFStreamSink **ppStreamSink);
		IFACEMETHOD(SetPresentationClock) (IMFPresentationClock *pPresentationClock);
		IFACEMETHOD(GetPresentationClock) (IMFPresentationClock **ppPresentationClock);
		IFACEMETHOD(Shutdown) ();

		// IMFClockStateSink methods
		IFACEMETHOD(OnClockStart) (MFTIME hnsSystemTime, LONGLONG llClockStartOffset);
		IFACEMETHOD(OnClockStop) (MFTIME hnsSystemTime);
		IFACEMETHOD(OnClockPause) (MFTIME hnsSystemTime);
		IFACEMETHOD(OnClockRestart) (MFTIME hnsSystemTime);
		IFACEMETHOD(OnClockSetRate) (MFTIME hnsSystemTime, float flRate);

		LONGLONG GetStartTime() const { return _llStartTime; }

		void ReportEndOfStream();

	private:
		//typedef ComPtrList<IMFStreamSink> StreamContainer;

	private:
		//void StartListening();
		//void StartReceiving(Network::IMediaBufferWrapper *pReceiveBuffer);
		//concurrency::task<void> SendPacket(Network::IBufferPacket *pPacket);
		//String ^PrepareRemoteUrl(StreamSocketInformation ^info);
		//void SendDescription();

		//ComPtr<Network::IMediaBufferWrapper> FillStreamDescription(CStreamSink *pStream, StspStreamDescription *pStreamDescription);

		void HandleError(HRESULT hr);

		void SetVideoStreamProperties(_In_opt_ Windows::Media::MediaProperties::IMediaEncodingProperties ^mediaEncodingProperties);

		HRESULT CheckShutdown() const
		{
			if (_IsShutdown)
				return MF_E_SHUTDOWN;
			return S_OK;
		}

	private:
		std::recursive_mutex _mutex;
		ComPtrList<IMFStreamSink> _streams;

		long                            _cRef;                      // reference count
		//CritSec                         _critSec;                   // critical section for thread safety

		bool                            _IsShutdown;                // Flag to indicate if Shutdown() method was called.
		bool                            _IsConnected;
		LONGLONG                        _llStartTime;

		ComPtr<IMFPresentationClock>    _spClock;                   // Presentation clock.
		//Network::INetworkChannel^       _networkSender;
		//ISinkCallback^                  _callback;
		//ComPtr<Network::IMediaBufferWrapper> _spReceiveBuffer;
		//StreamContainer                 _streams;
		long                            _cStreamsEnded;
		String^                         _remoteUrl;

		DWORD                           _waitingConnectionId;
	};


	public ref class MSWinRTMediaSinkProxy sealed
	{
	public:
		MSWinRTMediaSinkProxy();
		virtual ~MSWinRTMediaSinkProxy();

		Windows::Media::IMediaExtension ^GetMFExtensions();
		Windows::Foundation::IAsyncOperation<Windows::Media::IMediaExtension^>^ InitializeAsync(Windows::Media::MediaProperties::IMediaEncodingProperties ^videoEncodingProperties);

	internal:
		void SetVideoStreamProperties(_In_opt_ Windows::Media::MediaProperties::IMediaEncodingProperties ^mediaEncodingProperties);

	private:
		void OnShutdown();

		void CheckShutdown()
		{
			if (_fShutdown) {
				throw ref new Exception(MF_E_SHUTDOWN);
			}
		}

	private:
		std::mutex _mutex;
		ComPtr<IMFMediaSink> _spMediaSink;
		bool _fShutdown;
	};
}
