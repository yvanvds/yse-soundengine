/*
  ==============================================================================

    directsoundDeviceType.cpp
    Created: 16 Jun 2014 7:46:39pm
    Author:  yvan

  ==============================================================================
*/

#include "directsoundDeviceType.h"

//#include <windows.h>
//#include <mmsystem.h>
#include <dsound.h>
#include <stdio.h>
#include <assert.h>
#include <vector>

/////////////
// LINKING //
/////////////
#pragma comment(lib, "dsound.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "winmm.lib")

YSE::IO::directSoundDeviceType::directSoundDeviceType() : ioDeviceType(L"wasapi") {

}

// internal enumeration callback for directsound
bool CALLBACK enumCallback(LPGUID lpGUID, LPCTSTR description, LPCTSTR name, LPVOID data) {
  std::vector< std::shared_ptr<YSE::IO::directSoundDevice> > * devices = (std::vector< std::shared_ptr<YSE::IO::directSoundDevice> > *) data;

  // for now, it seems to make more sense to swap name and description
  std::shared_ptr<YSE::IO::directSoundDevice> d = std::shared_ptr<YSE::IO::directSoundDevice>(new YSE::IO::directSoundDevice(YSE::StringToWString(description), lpGUID));
  devices->push_back(d);
  devices->back()->setDescription(YSE::StringToWString(name));
  return true;
}

void YSE::IO::directSoundDeviceType::scanDevices() {
  if (devices.empty()) {
    if (FAILED(DirectSoundEnumerate((LPDSENUMCALLBACK)enumCallback, (VOID*)& devices))) {
      // why didn't this work? -> debug time!
      assert(false);
    }
  }
}

int YSE::IO::directSoundDeviceType::getNumDevices() {
  return devices.size();
}

YSE::IO::ioDevice * YSE::IO::directSoundDeviceType::getDevice(int num) {
  if (num < 0 || num >= devices.size()) {
    assert(false);
    return nullptr;
  }
  return devices[num].get();
}
