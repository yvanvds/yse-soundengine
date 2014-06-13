/*
  ==============================================================================

    windowsWASAPI.cpp
    Created: 13 Jun 2014 4:44:14pm
    Author:  yvan

  ==============================================================================
*/


#include <minwindef.h>
#include <wtypes.h>
#include <winnt.h>
#include <string>
#include <cmath>
#include <mmreg.h>
#include <mmeapi.h>
#include <tchar.h>
#include <vector>
#include <array>
#include "../../utils/misc.hpp"

namespace YSE {
  namespace IO {

#define YSE_COMCLASS(name, guid) struct __declspec (uuid(guid)) name
#define YSE_IUNKNOWNCLASS(name, guid)   YSE_COMCLASS(name, guid) : public IUnknown
#define YSE_COMCALL                     virtual HRESULT STDMETHODCALLTYPE

    enum EDataFlow {
      eRender,
      eCapture,
      eAll,
    };

    enum { DEVICE_STATE_ACTIVE = 1 };

    YSE_IUNKNOWNCLASS(IPropertyStore, "886d8eeb-8cf2-4446-8d02-cdba1dbdcf99")
    {
      YSE_COMCALL GetCount(DWORD*) = 0;
      YSE_COMCALL GetAt(DWORD, PROPERTYKEY*) = 0;
      YSE_COMCALL GetValue(const PROPERTYKEY&, PROPVARIANT*) = 0;
      YSE_COMCALL SetValue(const PROPERTYKEY&, const PROPVARIANT&) = 0;
      YSE_COMCALL Commit() = 0;
    };

    YSE_IUNKNOWNCLASS(IMMDevice, "D666063F-1587-4E43-81F1-B948E807363F")
    {
      YSE_COMCALL Activate(REFIID, DWORD, PROPVARIANT*, void**) = 0;
      YSE_COMCALL OpenPropertyStore(DWORD, IPropertyStore**) = 0;
      YSE_COMCALL GetId(LPWSTR*) = 0;
      YSE_COMCALL GetState(DWORD*) = 0;
    };

    YSE_IUNKNOWNCLASS(IMMEndpoint, "1BE09788-6894-4089-8586-9A2A6C265AC5")
    {
      YSE_COMCALL GetDataFlow(EDataFlow*) = 0;
    };

    struct IMMDeviceCollection : public IUnknown
    {
      YSE_COMCALL GetCount(UINT*) = 0;
      YSE_COMCALL Item(UINT, IMMDevice**) = 0;
    };

    enum ERole {
      eConsole,
      eMultimedia,
      eCommunications,
    };

    YSE_IUNKNOWNCLASS(IMMNotificationClient, "7991EEC9-7E89-4D85-8390-6C703CEC60C0")
    {
      YSE_COMCALL OnDeviceStateChanged(LPCWSTR, DWORD) = 0;
      YSE_COMCALL OnDeviceAdded(LPCWSTR) = 0;
      YSE_COMCALL OnDeviceRemoved(LPCWSTR) = 0;
      YSE_COMCALL OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) = 0;
      YSE_COMCALL OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) = 0;
    };

    YSE_IUNKNOWNCLASS(IMMNotificationClient, "7991EEC9-7E89-4D85-8390-6C703CEC60C0")
    {
      YSE_COMCALL OnDeviceStateChanged(LPCWSTR, DWORD) = 0;
      YSE_COMCALL OnDeviceAdded(LPCWSTR) = 0;
      YSE_COMCALL OnDeviceRemoved(LPCWSTR) = 0;
      YSE_COMCALL OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) = 0;
      YSE_COMCALL OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) = 0;
    };

    YSE_IUNKNOWNCLASS(IMMDeviceEnumerator, "A95664D2-9614-4F35-A746-DE8DB63617E6")
    {
      YSE_COMCALL EnumAudioEndpoints(EDataFlow, DWORD, IMMDeviceCollection**) = 0;
      YSE_COMCALL GetDefaultAudioEndpoint(EDataFlow, ERole, IMMDevice**) = 0;
      YSE_COMCALL GetDevice(LPCWSTR, IMMDevice**) = 0;
      YSE_COMCALL RegisterEndpointNotificationCallback(IMMNotificationClient*) = 0;
      YSE_COMCALL UnregisterEndpointNotificationCallback(IMMNotificationClient*) = 0;
    };

    YSE_COMCLASS(MMDeviceEnumerator, "BCDE0395-E52F-467C-8E3D-C4579291692E");

    typedef LONGLONG REFERENCE_TIME;

    enum AVRT_PRIORITY
    {
      AVRT_PRIORITY_LOW = -1,
      AVRT_PRIORITY_NORMAL,
      AVRT_PRIORITY_HIGH,
      AVRT_PRIORITY_CRITICAL
    };

    enum AUDCLNT_SHAREMODE
    {
      AUDCLNT_SHAREMODE_SHARED,
      AUDCLNT_SHAREMODE_EXCLUSIVE
    };

    YSE_IUNKNOWNCLASS(IAudioClient, "1CB9AD4C-DBFA-4c32-B178-C2F568A703B2")
    {
      YSE_COMCALL Initialize(AUDCLNT_SHAREMODE, DWORD, REFERENCE_TIME, REFERENCE_TIME, const WAVEFORMATEX*, LPCGUID) = 0;
      YSE_COMCALL GetBufferSize(UINT32*) = 0;
      YSE_COMCALL GetStreamLatency(REFERENCE_TIME*) = 0;
      YSE_COMCALL GetCurrentPadding(UINT32*) = 0;
      YSE_COMCALL IsFormatSupported(AUDCLNT_SHAREMODE, const WAVEFORMATEX*, WAVEFORMATEX**) = 0;
      YSE_COMCALL GetMixFormat(WAVEFORMATEX**) = 0;
      YSE_COMCALL GetDevicePeriod(REFERENCE_TIME*, REFERENCE_TIME*) = 0;
      YSE_COMCALL Start() = 0;
      YSE_COMCALL Stop() = 0;
      YSE_COMCALL Reset() = 0;
      YSE_COMCALL SetEventHandle(HANDLE) = 0;
      YSE_COMCALL GetService(REFIID, void**) = 0;
    };

    YSE_IUNKNOWNCLASS(IAudioCaptureClient, "C8ADBD64-E71E-48a0-A4DE-185C395CD317")
    {
      YSE_COMCALL GetBuffer(BYTE**, UINT32*, DWORD*, UINT64*, UINT64*) = 0;
      YSE_COMCALL ReleaseBuffer(UINT32) = 0;
      YSE_COMCALL GetNextPacketSize(UINT32*) = 0;
    };

    YSE_IUNKNOWNCLASS(IAudioRenderClient, "F294ACFC-3146-4483-A7BF-ADDCA7C260E2")
    {
      YSE_COMCALL GetBuffer(UINT32, BYTE**) = 0;
      YSE_COMCALL ReleaseBuffer(UINT32, DWORD) = 0;
    };

    YSE_IUNKNOWNCLASS(IAudioEndpointVolume, "5CDF2C82-841E-4546-9722-0CF74078229A")
    {
      YSE_COMCALL RegisterControlChangeNotify(void*) = 0;
      YSE_COMCALL UnregisterControlChangeNotify(void*) = 0;
      YSE_COMCALL GetChannelCount(UINT*) = 0;
      YSE_COMCALL SetMasterVolumeLevel(float, LPCGUID) = 0;
      YSE_COMCALL SetMasterVolumeLevelScalar(float, LPCGUID) = 0;
      YSE_COMCALL GetMasterVolumeLevel(float*) = 0;
      YSE_COMCALL GetMasterVolumeLevelScalar(float*) = 0;
      YSE_COMCALL SetChannelVolumeLevel(UINT, float, LPCGUID) = 0;
      YSE_COMCALL SetChannelVolumeLevelScalar(UINT, float, LPCGUID) = 0;
      YSE_COMCALL GetChannelVolumeLevel(UINT, float*) = 0;
      YSE_COMCALL GetChannelVolumeLevelScalar(UINT, float*) = 0;
      YSE_COMCALL SetMute(BOOL, LPCGUID) = 0;
      YSE_COMCALL GetMute(BOOL*) = 0;
      YSE_COMCALL GetVolumeStepInfo(UINT*, UINT*) = 0;
      YSE_COMCALL VolumeStepUp(LPCGUID) = 0;
      YSE_COMCALL VolumeStepDown(LPCGUID) = 0;
      YSE_COMCALL QueryHardwareSupport(DWORD*) = 0;
      YSE_COMCALL GetVolumeRange(float*, float*, float*) = 0;
    };

    enum AudioSessionDisconnectReason
    {
      DisconnectReasonDeviceRemoval = 0,
      DisconnectReasonServerShutdown = 1,
      DisconnectReasonFormatChanged = 2,
      DisconnectReasonSessionLogoff = 3,
      DisconnectReasonSessionDisconnected = 4,
      DisconnectReasonExclusiveModeOverride = 5
    };

    enum AudioSessionState
    {
      AudioSessionStateInactive = 0,
      AudioSessionStateActive = 1,
      AudioSessionStateExpired = 2
    };

    YSE_IUNKNOWNCLASS(IAudioSessionEvents, "24918ACC-64B3-37C1-8CA9-74A66E9957A8")
    {
      YSE_COMCALL OnDisplayNameChanged(LPCWSTR, LPCGUID) = 0;
      YSE_COMCALL OnIconPathChanged(LPCWSTR, LPCGUID) = 0;
      YSE_COMCALL OnSimpleVolumeChanged(float, BOOL, LPCGUID) = 0;
      YSE_COMCALL OnChannelVolumeChanged(DWORD, float*, DWORD, LPCGUID) = 0;
      YSE_COMCALL OnGroupingParamChanged(LPCGUID, LPCGUID) = 0;
      YSE_COMCALL OnStateChanged(AudioSessionState) = 0;
      YSE_COMCALL OnSessionDisconnected(AudioSessionDisconnectReason) = 0;
    };

    YSE_IUNKNOWNCLASS(IAudioSessionControl, "F4B1A599-7266-4319-A8CA-E70ACB11E8CD")
    {
      YSE_COMCALL GetState(AudioSessionState*) = 0;
      YSE_COMCALL GetDisplayName(LPWSTR*) = 0;
      YSE_COMCALL SetDisplayName(LPCWSTR, LPCGUID) = 0;
      YSE_COMCALL GetIconPath(LPWSTR*) = 0;
      YSE_COMCALL SetIconPath(LPCWSTR, LPCGUID) = 0;
      YSE_COMCALL GetGroupingParam(GUID*) = 0;
      YSE_COMCALL SetGroupingParam(LPCGUID, LPCGUID) = 0;
      YSE_COMCALL RegisterAudioSessionNotification(IAudioSessionEvents*) = 0;
      YSE_COMCALL UnregisterAudioSessionNotification(IAudioSessionEvents*) = 0;
    };

#undef YSE_COMCALL
#undef YSE_COMCLASS
#undef YSE_IUNKNOWNCLASS

    //==============================================================================

    namespace WASAPI{

      std::wstring getDeviceID(IMMDevice * const device) {
        std::wstring s;
        WCHAR* deviceId = nullptr;

        if (SUCCEEDED(device->GetId(&deviceId))) {
          s = std::wstring(deviceId);
          CoTaskMemFree(deviceId);
        }
        return s;
      }

      EDataFlow getDataFlow(const ComSmartPtr<IMMDevice>& device) {
        EDataFlow flow = eRender;
        ComSmartPtr<IMMEndpoint> endpoint;
        if (SUCCEEDED(device.QueryInterface(endpoint))) {
          (void)SUCCEEDED(endpoint->GetDataFlow(&flow));
        }
        return flow;
      }

      int refTimeToSamples(const REFERENCE_TIME& t, const double sampleRate) {
        return std::round(sampleRate * ((double)t) * 0.0000001);
      }

      void copyWavFormat(WAVEFORMATEXTENSIBLE& dest, const WAVEFORMATEX* const src) {
        memcpy(&dest, src, src->wFormatTag == WAVE_FORMAT_EXTENSIBLE ? sizeof (WAVEFORMATEXTENSIBLE)
        : sizeof (WAVEFORMATEX));
      }

      //==============================================================================
      
      class WASAPIDeviceBase
      {
      public:
        WASAPIDeviceBase(const ComSmartPtr<IMMDevice>& d, const bool exclusiveMode)
          : device(d),
          sampleRate(0),
          defaultSampleRate(0),
          numChannels(0),
          actualNumChannels(0),
          minBufferSize(0),
          defaultBufferSize(0),
          latencySamples(0),
          useExclusiveMode(exclusiveMode),
          sampleRateHasChanged(false)
        {
          clientEvent = CreateEvent(0, false, false, _T("JuceWASAPI"));

          ComSmartPtr<IAudioClient> tempClient(createClient());
          if (tempClient == nullptr)
            return;

          REFERENCE_TIME defaultPeriod, minPeriod;
          if (!SUCCEEDED(tempClient->GetDevicePeriod(&defaultPeriod, &minPeriod)))
            return;

          WAVEFORMATEX* mixFormat = nullptr;
          if (!SUCCEEDED(tempClient->GetMixFormat(&mixFormat)))
            return;

          WAVEFORMATEXTENSIBLE format;
          copyWavFormat(format, mixFormat);
          CoTaskMemFree(mixFormat);

          actualNumChannels = numChannels = format.Format.nChannels;
          defaultSampleRate = format.Format.nSamplesPerSec;
          minBufferSize = refTimeToSamples(minPeriod, defaultSampleRate);
          defaultBufferSize = refTimeToSamples(defaultPeriod, defaultSampleRate);
          mixFormatChannelMask = format.dwChannelMask;

          rates.push_back(defaultSampleRate);

          std::array<int, 6> ratesToTest = { 44100, 48000, 88200, 96000, 176400, 192000 };

          for (int i = 0; i < ratesToTest.size(); ++i)
          {
            if (ratesToTest[i] == defaultSampleRate)
              continue;

            format.Format.nSamplesPerSec = (DWORD)ratesToTest[i];

            if (SUCCEEDED(tempClient->IsFormatSupported(useExclusiveMode ? AUDCLNT_SHAREMODE_EXCLUSIVE : AUDCLNT_SHAREMODE_SHARED,
              (WAVEFORMATEX*)&format, 0))) {

              Bool found = false;
              FOREACH_D(j, rates) {
                if (rates[j] == ratesToTest[i]) {
                  found = true;
                  break;
                }
              }
              if (!found) rates.push_back(ratesToTest[i]);
            }

          }
          std::sort(rates.begin(), rates.end());
        }

        virtual ~WASAPIDeviceBase()
        {
          device = nullptr;
          CloseHandle(clientEvent);
        }

        bool isOk() const noexcept{ return defaultBufferSize > 0 && defaultSampleRate > 0; }

        bool openClient(const double newSampleRate, const U64& newChannels)
        {
          sampleRate = newSampleRate;
          channels = newChannels;
          channels.setRange(actualNumChannels, channels.getHighestBit() + 1 - actualNumChannels, false);
          numChannels = channels.getHighestBit() + 1;

          if (numChannels == 0)
            return true;

          client = createClient();

          if (client != nullptr
            && (tryInitialisingWithFormat(true, 4) || tryInitialisingWithFormat(false, 4)
            || tryInitialisingWithFormat(false, 3) || tryInitialisingWithFormat(false, 2)))
          {
            sampleRateHasChanged = false;

            channelMaps.clear();
            for (int i = 0; i <= channels.getHighestBit(); ++i)
            if (channels[i])
              channelMaps.add(i);

            REFERENCE_TIME latency;
            if (SUCCEEDED(client->GetStreamLatency(&latency)))
              latencySamples = refTimeToSamples(latency, sampleRate);

            (void)SUCCEEDED(client->GetBufferSize(&actualBufferSize));

            createSessionEventCallback();

            return SUCCEEDED(client->SetEventHandle(clientEvent));
          }

          return false;
        }

        void closeClient()
        {
          if (client != nullptr)
            client->Stop();

          deleteSessionEventCallback();
          client = nullptr;
          ResetEvent(clientEvent);
        }

        void deviceSampleRateChanged()
        {
          sampleRateHasChanged = true;
        }

        //==============================================================================
        ComSmartPtr<IMMDevice> device;
        ComSmartPtr<IAudioClient> client;
        Dbl sampleRate, defaultSampleRate;
        Int numChannels, actualNumChannels;
        Int minBufferSize, defaultBufferSize, latencySamples;
        DWORD mixFormatChannelMask;
        const Bool useExclusiveMode;
        std::vector<Dbl> rates;
        HANDLE clientEvent;
        U64 channels;
        std::vector<Int> channelMaps;
        UINT32 actualBufferSize;
        Int bytesPerSample;
        Bool sampleRateHasChanged;

        virtual void updateFormat(Bool isFloat) = 0;

      private:
        //==============================================================================
        class SessionEventCallback : public ComBaseClassHelper<IAudioSessionEvents>
        {
        public:
          SessionEventCallback(WASAPIDeviceBase& d) : owner(d) {}

          YSE_COMRESULT OnDisplayNameChanged(LPCWSTR, LPCGUID)                 { return S_OK; }
          YSE_COMRESULT OnIconPathChanged(LPCWSTR, LPCGUID)                    { return S_OK; }
          YSE_COMRESULT OnSimpleVolumeChanged(float, BOOL, LPCGUID)            { return S_OK; }
          YSE_COMRESULT OnChannelVolumeChanged(DWORD, float*, DWORD, LPCGUID)  { return S_OK; }
          YSE_COMRESULT OnGroupingParamChanged(LPCGUID, LPCGUID)               { return S_OK; }
          YSE_COMRESULT OnStateChanged(AudioSessionState)                      { return S_OK; }

          YSE_COMRESULT OnSessionDisconnected(AudioSessionDisconnectReason reason)
          {
            if (reason == DisconnectReasonFormatChanged)
              owner.deviceSampleRateChanged();

            return S_OK;
          }

        private:
          WASAPIDeviceBase& owner;
        };

        ComSmartPtr<IAudioSessionControl> audioSessionControl;
        ComSmartPtr<SessionEventCallback> sessionEventCallback;

        void createSessionEventCallback()
        {
          deleteSessionEventCallback();
          client->GetService(__uuidof (IAudioSessionControl),
            (void**)audioSessionControl.resetAndGetPointerAddress());

          if (audioSessionControl != nullptr)
          {
            sessionEventCallback = new SessionEventCallback(*this);
            audioSessionControl->RegisterAudioSessionNotification(sessionEventCallback);
            sessionEventCallback->Release(); // (required because ComBaseClassHelper objects are constructed with a ref count of 1)
          }
        }

        void deleteSessionEventCallback()
        {
          if (audioSessionControl != nullptr && sessionEventCallback != nullptr)
            audioSessionControl->UnregisterAudioSessionNotification(sessionEventCallback);

          audioSessionControl = nullptr;
          sessionEventCallback = nullptr;
        }

        //==============================================================================
        ComSmartPtr<IAudioClient> createClient()
        {
          ComSmartPtr<IAudioClient> client;

          if (device != nullptr)
            device->Activate(__uuidof (IAudioClient), CLSCTX_INPROC_SERVER,
            nullptr, (void**)client.resetAndGetPointerAddress());

          return client;
        }

        bool tryInitialisingWithFormat(const bool useFloat, const int bytesPerSampleToTry)
        {
          WAVEFORMATEXTENSIBLE format;
          memset(&format, 0, sizeof(format));

          if (numChannels <= 2 && bytesPerSampleToTry <= 2)
          {
            format.Format.wFormatTag = WAVE_FORMAT_PCM;
          }
          else
          {
            format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
            format.Format.cbSize = sizeof (WAVEFORMATEXTENSIBLE)-sizeof (WAVEFORMATEX);
          }

          format.Format.nSamplesPerSec = (DWORD)sampleRate;
          format.Format.nChannels = (WORD)numChannels;
          format.Format.wBitsPerSample = (WORD)(8 * bytesPerSampleToTry);
          format.Format.nAvgBytesPerSec = (DWORD)(format.Format.nSamplesPerSec * numChannels * bytesPerSampleToTry);
          format.Format.nBlockAlign = (WORD)(numChannels * bytesPerSampleToTry);
          format.SubFormat = useFloat ? KSDATAFORMAT_SUBTYPE_IEEE_FLOAT : KSDATAFORMAT_SUBTYPE_PCM;
          format.Samples.wValidBitsPerSample = format.Format.wBitsPerSample;
          format.dwChannelMask = mixFormatChannelMask;

          WAVEFORMATEXTENSIBLE* nearestFormat = nullptr;

          HRESULT hr = client->IsFormatSupported(useExclusiveMode ? AUDCLNT_SHAREMODE_EXCLUSIVE
            : AUDCLNT_SHAREMODE_SHARED,
            (WAVEFORMATEX*)&format,
            useExclusiveMode ? nullptr : (WAVEFORMATEX**)&nearestFormat);
          //logFailure(hr);

          if (hr == S_FALSE && format.Format.nSamplesPerSec == nearestFormat->Format.nSamplesPerSec)
          {
            copyWavFormat(format, (WAVEFORMATEX*)nearestFormat);
            hr = S_OK;
          }

          CoTaskMemFree(nearestFormat);

          REFERENCE_TIME defaultPeriod = 0, minPeriod = 0;
          if (useExclusiveMode)
            SUCCEEDED(client->GetDevicePeriod(&defaultPeriod, &minPeriod));

          GUID session;
          if (hr == S_OK
            && SUCCEEDED(client->Initialize(useExclusiveMode ? AUDCLNT_SHAREMODE_EXCLUSIVE : AUDCLNT_SHAREMODE_SHARED,
            0x40000 /*AUDCLNT_STREAMFLAGS_EVENTCALLBACK*/,
            defaultPeriod, defaultPeriod, (WAVEFORMATEX*)&format, &session)))
          {
            actualNumChannels = format.Format.nChannels;
            const bool isFloat = format.Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE && format.SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
            bytesPerSample = format.Format.wBitsPerSample / 8;

            updateFormat(isFloat);
            return true;
          }

          return false;
        }

      };

      //==============================================================================
      class WASAPIInputDevice : public WASAPIDeviceBase
      {
      public:
        WASAPIInputDevice(const ComSmartPtr<IMMDevice>& d, const bool exclusiveMode)
          : WASAPIDeviceBase(d, exclusiveMode),
          reservoir(1, 1)
        {
        }

        ~WASAPIInputDevice()
        {
          close();
        }

        bool open(const double newSampleRate, const U64& newChannels)
        {
          reservoirSize = 0;
          reservoirCapacity = 16384;
          reservoir.setSize(actualNumChannels * reservoirCapacity * sizeof (float));
          return openClient(newSampleRate, newChannels)
            && (numChannels == 0 || SUCCEEDED(client->GetService(__uuidof (IAudioCaptureClient),
            (void**)captureClient.resetAndGetPointerAddress())));
        }

        void close()
        {
          closeClient();
          captureClient = nullptr;
          reservoir.reset();
        }

        template<class SourceType>
        void updateFormatWithType(SourceType*)
        {
          typedef AudioData::Pointer<AudioData::Float32, AudioData::NativeEndian, AudioData::NonInterleaved, AudioData::NonConst> NativeType;
          converter = new AudioData::ConverterInstance<AudioData::Pointer<SourceType, AudioData::LittleEndian, AudioData::Interleaved, AudioData::Const>, NativeType>(actualNumChannels, 1);
        }

        void updateFormat(bool isFloat)
        {
          if (isFloat)                    updateFormatWithType((AudioData::Float32*) 0);
          else if (bytesPerSample == 4)   updateFormatWithType((AudioData::Int32*) 0);
          else if (bytesPerSample == 3)   updateFormatWithType((AudioData::Int24*) 0);
          else                            updateFormatWithType((AudioData::Int16*) 0);
        }

        void copyBuffers(float** destBuffers, int numDestBuffers, int bufferSize, Thread& thread)
        {
          if (numChannels <= 0)
            return;

          int offset = 0;

          while (bufferSize > 0)
          {
            if (reservoirSize > 0)  // There's stuff in the reservoir, so use that...
            {
              const int samplesToDo = jmin(bufferSize, (int)reservoirSize);

              for (int i = 0; i < numDestBuffers; ++i)
                converter->convertSamples(destBuffers[i] + offset, 0, reservoir.getData(), channelMaps.getUnchecked(i), samplesToDo);

              bufferSize -= samplesToDo;
              offset += samplesToDo;
              reservoirSize = 0;
            }
            else
            {
              UINT32 packetLength = 0;
              if (!check(captureClient->GetNextPacketSize(&packetLength)))
                break;

              if (packetLength == 0)
              {
                if (thread.threadShouldExit()
                  || WaitForSingleObject(clientEvent, 1000) == WAIT_TIMEOUT)
                  break;

                continue;
              }

              uint8* inputData;
              UINT32 numSamplesAvailable;
              DWORD flags;

              if (check(captureClient->GetBuffer(&inputData, &numSamplesAvailable, &flags, 0, 0)))
              {
                const int samplesToDo = jmin(bufferSize, (int)numSamplesAvailable);

                for (int i = 0; i < numDestBuffers; ++i)
                  converter->convertSamples(destBuffers[i] + offset, 0, inputData, channelMaps.getUnchecked(i), samplesToDo);

                bufferSize -= samplesToDo;
                offset += samplesToDo;

                if (samplesToDo < (int)numSamplesAvailable)
                {
                  reservoirSize = jmin((int)(numSamplesAvailable - samplesToDo), reservoirCapacity);
                  memcpy((uint8*)reservoir.getData(), inputData + bytesPerSample * actualNumChannels * samplesToDo,
                    (size_t)(bytesPerSample * actualNumChannels * reservoirSize));
                }

                captureClient->ReleaseBuffer(numSamplesAvailable);
              }
            }
          }
        }

        ComSmartPtr<IAudioCaptureClient> captureClient;
        MemoryBlock reservoir;
        int reservoirSize, reservoirCapacity;
        ScopedPointer<AudioData::Converter> converter;

      private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WASAPIInputDevice)
      };

      //==============================================================================
      class WASAPIOutputDevice : public WASAPIDeviceBase
      {
      public:
        WASAPIOutputDevice(const ComSmartPtr<IMMDevice>& d, const bool exclusiveMode)
          : WASAPIDeviceBase(d, exclusiveMode)
        {
        }

        ~WASAPIOutputDevice()
        {
          close();
        }

        bool open(const double newSampleRate, const BigInteger& newChannels)
        {
          return openClient(newSampleRate, newChannels)
            && (numChannels == 0 || check(client->GetService(__uuidof (IAudioRenderClient), (void**)renderClient.resetAndGetPointerAddress())));
        }

        void close()
        {
          closeClient();
          renderClient = nullptr;
        }

        template<class DestType>
        void updateFormatWithType(DestType*)
        {
          typedef AudioData::Pointer<AudioData::Float32, AudioData::NativeEndian, AudioData::NonInterleaved, AudioData::Const> NativeType;
          converter = new AudioData::ConverterInstance<NativeType, AudioData::Pointer<DestType, AudioData::LittleEndian, AudioData::Interleaved, AudioData::NonConst> >(1, actualNumChannels);
        }

        void updateFormat(bool isFloat)
        {
          if (isFloat)                    updateFormatWithType((AudioData::Float32*) 0);
          else if (bytesPerSample == 4)   updateFormatWithType((AudioData::Int32*) 0);
          else if (bytesPerSample == 3)   updateFormatWithType((AudioData::Int24*) 0);
          else                            updateFormatWithType((AudioData::Int16*) 0);
        }

        void copyBuffers(const float** const srcBuffers, const int numSrcBuffers, int bufferSize, Thread& thread)
        {
          if (numChannels <= 0)
            return;

          int offset = 0;

          while (bufferSize > 0)
          {
            UINT32 padding = 0;
            if (!check(client->GetCurrentPadding(&padding)))
              return;

            int samplesToDo = useExclusiveMode ? bufferSize
              : jmin((int)(actualBufferSize - padding), bufferSize);

            if (samplesToDo <= 0)
            {
              if (thread.threadShouldExit()
                || WaitForSingleObject(clientEvent, 1000) == WAIT_TIMEOUT)
                break;

              continue;
            }

            uint8* outputData = nullptr;
            if (check(renderClient->GetBuffer((UINT32)samplesToDo, &outputData)))
            {
              for (int i = 0; i < numSrcBuffers; ++i)
                converter->convertSamples(outputData, channelMaps.getUnchecked(i), srcBuffers[i] + offset, 0, samplesToDo);

              renderClient->ReleaseBuffer((UINT32)samplesToDo, 0);

              offset += samplesToDo;
              bufferSize -= samplesToDo;
            }
          }
        }

        ComSmartPtr<IAudioRenderClient> renderClient;
        ScopedPointer<AudioData::Converter> converter;

      private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WASAPIOutputDevice)
      };


    }


  }
}
