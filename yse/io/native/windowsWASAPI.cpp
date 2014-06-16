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
#include <mutex>
#include "../../utils/misc.hpp"
#include "../ioDevice.h"
#include "../ioDeviceType.h"

namespace YSE {
  namespace IO {

      //==============================================================================
      

      //==============================================================================



      //==============================================================================
      class WASAPIAudioIODeviceType : public ioDeviceType,
        private DeviceChangeDetector
      {
      public:
        WASAPIAudioIODeviceType()
          : AudioIODeviceType("Windows Audio"),
          DeviceChangeDetector(L"Windows Audio"),
          hasScanned(false)
        {
        }

        ~WASAPIAudioIODeviceType()
        {
          if (notifyClient != nullptr)
            enumerator->UnregisterEndpointNotificationCallback(notifyClient);
        }

        //==============================================================================
        void scanForDevices()
        {
          hasScanned = true;

          outputDeviceNames.clear();
          inputDeviceNames.clear();
          outputDeviceIds.clear();
          inputDeviceIds.clear();

          scan(outputDeviceNames, inputDeviceNames,
            outputDeviceIds, inputDeviceIds);
        }

        StringArray getDeviceNames(bool wantInputNames) const
        {
          jassert(hasScanned); // need to call scanForDevices() before doing this

          return wantInputNames ? inputDeviceNames
            : outputDeviceNames;
        }

        int getDefaultDeviceIndex(bool /*forInput*/) const
        {
          jassert(hasScanned); // need to call scanForDevices() before doing this
          return 0;
        }

        int getIndexOfDevice(AudioIODevice* device, bool asInput) const
        {
          jassert(hasScanned); // need to call scanForDevices() before doing this

          if (WASAPIAudioIODevice* const d = dynamic_cast<WASAPIAudioIODevice*> (device))
            return asInput ? inputDeviceIds.indexOf(d->inputDeviceId)
            : outputDeviceIds.indexOf(d->outputDeviceId);

          return -1;
        }

        bool hasSeparateInputsAndOutputs() const    { return true; }

        ioDevice* createDevice(const std::wstring & outputDeviceName,
          const std::wstring & inputDeviceName)
        {
          assert(hasScanned); // need to call scanForDevices() before doing this

          const bool useExclusiveMode = false;
          ScopedPointer<WASAPIAudioIODevice> device;

          const int outputIndex = outputDeviceNames.indexOf(outputDeviceName);
          const int inputIndex = inputDeviceNames.indexOf(inputDeviceName);

          if (outputIndex >= 0 || inputIndex >= 0)
          {
            device = new WASAPIAudioIODevice(!outputDeviceName.empty() ? outputDeviceName
              : inputDeviceName,
              outputDeviceIds[outputIndex],
              inputDeviceIds[inputIndex],
              useExclusiveMode);

            if (!device->initialise())
              device = nullptr;
          }

          return device.release();
        }

        //==============================================================================
        std::vector<std::wstring> outputDeviceNames, outputDeviceIds;
        std::vector<std::wstring> inputDeviceNames, inputDeviceIds;

      private:
        bool hasScanned;
        ComSmartPtr<IMMDeviceEnumerator> enumerator;

        //==============================================================================
        class ChangeNotificationClient : public ComBaseClassHelper<IMMNotificationClient>
        {
        public:
          ChangeNotificationClient(WASAPIAudioIODeviceType& d)
            : ComBaseClassHelper<IMMNotificationClient>(0), device(d) {}

          HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR)                             { return notify(); }
          HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR)                           { return notify(); }
          HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD)               { return notify(); }
          HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR)  { return notify(); }
          HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) { return notify(); }

        private:
          WASAPIAudioIODeviceType& device;

          HRESULT notify()   { device.triggerAsyncDeviceChangeCallback(); return S_OK; }

        };

        ComSmartPtr<ChangeNotificationClient> notifyClient;

        //==============================================================================
        static std::wstring getDefaultEndpoint(IMMDeviceEnumerator* const enumerator, const bool forCapture)
        {
          std::wstring s;
          IMMDevice* dev = nullptr;

          if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(forCapture ? eCapture : eRender,
            eMultimedia, &dev)))
          {
            WCHAR* deviceId = nullptr;

            if (SUCCEEDED(dev->GetId(&deviceId)))
            {
              s = deviceId;
              CoTaskMemFree(deviceId);
            }

            dev->Release();
          }

          return s;
        }

        //==============================================================================
        void scan(std::vector<std::wstring> & outputDeviceNames,
          std::vector<std::wstring> & inputDeviceNames,
          std::vector<std::wstring> & outputDeviceIds,
          std::vector<std::wstring> & inputDeviceIds)
        {
          if (enumerator == nullptr)
          {
            if (!SUCCEEDED(enumerator.CoCreateInstance(__uuidof (MMDeviceEnumerator))))
              return;

            notifyClient = new ChangeNotificationClient(*this);
            enumerator->RegisterEndpointNotificationCallback(notifyClient);
          }

          const std::wstring defaultRenderer(getDefaultEndpoint(enumerator, false));
          const std::wstring defaultCapture(getDefaultEndpoint(enumerator, true));

          ComSmartPtr<IMMDeviceCollection> deviceCollection;
          UINT32 numDevices = 0;

          if (!(SUCCEEDED(enumerator->EnumAudioEndpoints(eAll, DEVICE_STATE_ACTIVE, deviceCollection.resetAndGetPointerAddress()))
            && SUCCEEDED(deviceCollection->GetCount(&numDevices))))
            return;

          for (UINT32 i = 0; i < numDevices; ++i)
          {
            ComSmartPtr<IMMDevice> device;
            if (!SUCCEEDED(deviceCollection->Item(i, device.resetAndGetPointerAddress())))
              continue;

            DWORD state = 0;
            if (!(SUCCEEDED(device->GetState(&state)) && state == DEVICE_STATE_ACTIVE))
              continue;

            const std::wstring deviceId(getDeviceID(device));
            std::wstring name;

            {
              ComSmartPtr<IPropertyStore> properties;
              if (!SUCCEEDED(device->OpenPropertyStore(STGM_READ, properties.resetAndGetPointerAddress())))
                continue;

              PROPVARIANT value;
              zerostruct(value);

              const PROPERTYKEY PKEY_Device_FriendlyName
                = { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14 };

              if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &value)))
                name = value.pwszVal;

              PropVariantClear(&value);
            }

            const EDataFlow flow = getDataFlow(device);

            if (flow == eRender)
            {
              const int index = (deviceId == defaultRenderer) ? 0 : -1;
              outputDeviceIds.insert(index, deviceId);
              outputDeviceNames.insert(index, name);
            }
            else if (flow == eCapture)
            {
              const int index = (deviceId == defaultCapture) ? 0 : -1;
              inputDeviceIds.insert(index, deviceId);
              inputDeviceNames.insert(index, name);
            }
          }

          inputDeviceNames.appendNumbersToDuplicates(false, false);
          outputDeviceNames.appendNumbersToDuplicates(false, false);
        }

        //==============================================================================
        void systemDeviceChanged()
        {
          std::vector<std::wstring> newOutNames, newInNames, newOutIds, newInIds;
          scan(newOutNames, newInNames, newOutIds, newInIds);

          if (newOutNames != outputDeviceNames
            || newInNames != inputDeviceNames
            || newOutIds != outputDeviceIds
            || newInIds != inputDeviceIds)
          {
            hasScanned = true;
            outputDeviceNames = newOutNames;
            inputDeviceNames = newInNames;
            outputDeviceIds = newOutIds;
            inputDeviceIds = newInIds;
          }

          callDeviceChangeListeners();
        }
      };

      //==============================================================================
      struct MMDeviceMasterVolume
      {
        MMDeviceMasterVolume()
        {
          ComSmartPtr<IMMDeviceEnumerator> enumerator;
          if (SUCCEEDED(enumerator.CoCreateInstance(__uuidof (MMDeviceEnumerator))))
          {
            ComSmartPtr<IMMDevice> device;
            if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, device.resetAndGetPointerAddress())))
              SUCCEEDED(device->Activate(__uuidof (IAudioEndpointVolume), CLSCTX_INPROC_SERVER, nullptr,
              (void**)endpointVolume.resetAndGetPointerAddress()));
          }
        }

        float getGain() const
        {
          float vol = 0.0f;
          if (endpointVolume != nullptr)
            SUCCEEDED(endpointVolume->GetMasterVolumeLevelScalar(&vol));

          return vol;
        }

        bool setGain(float newGain) const
        {
          Clamp(newGain, 0.0f, 1.0f);
          return endpointVolume != nullptr
            && SUCCEEDED(endpointVolume->SetMasterVolumeLevelScalar(newGain, nullptr));
        }

        bool isMuted() const
        {
          BOOL mute = 0;
          return endpointVolume != nullptr
            && SUCCEEDED(endpointVolume->GetMute(&mute)) && mute != 0;
        }

        bool setMuted(bool shouldMute) const
        {
          return endpointVolume != nullptr
            && SUCCEEDED(endpointVolume->SetMute(shouldMute, nullptr));
        }

        ComSmartPtr<IAudioEndpointVolume> endpointVolume;
      };

}

//==============================================================================
ioDeviceType* ioDeviceType::createAudioIODeviceType_WASAPI()
{
  if (SystemStats::getOperatingSystemType() >= SystemStats::WinVista)
    return new WasapiClasses::WASAPIAudioIODeviceType();

  return nullptr;
}

    }


  }
}
