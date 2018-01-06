#if YSE_ANDROID

#include "androidDeviceManager.h"
#include "../internalHeaders.h"
#include "OpenSLImplementation.h"

#define CONV16BIT 32768
UInt YSE::SAMPLERATE = 44100;

YSE::DEVICE::managerObject & YSE::DEVICE::Manager() {
  static managerObject d;
  return d;
}

YSE::DEVICE::managerObject::managerObject() 
  : initDone(false)
  , open(false)
{}

YSE::DEVICE::managerObject::~managerObject() {}

Bool YSE::DEVICE::managerObject::init() {
  YSE::Log().sendMessage("androidDeviceManager: init started");
  if (!initDone) {
    implementation.Setup();
  }
  initDone = true;
  open = true;
  YSE::Log().sendMessage("androidDeviceManager: init done");
  deviceManager::init();
  return true;
}

void YSE::DEVICE::managerObject::updateDeviceList() {
  devices.clear();

  YSE::device d;
  d.setID(0);
  d.setName("Android Audio");
  d.setTypeName("OpenSL ES");
  d.addOutputChannelName("Left");
  d.addOutputChannelName("Right");
  d.setOutputLatency(100);
  d.addAvailableSampleRate(44100);
  devices.push_back(d);
}


void YSE::DEVICE::managerObject::addCallback() {
  //android_RegisterOutputCallback(stream, AudioCallback);
  implementation.Start(YSE::DEVICE::Manager().getMaster().GetBuffers().size());
  YSE::Log().sendMessage("androidDeviceManager: Callback Added");
}

void YSE::DEVICE::managerObject::openDevice(const YSE::deviceSetup & object) {
  // android only has one device for now
  return;
}

void YSE::DEVICE::managerObject::close() {
  YSE::Log().sendMessage("androidDeviceManager: Close started");

  if (!initDone) return;
  if (!open) return;

  implementation.Stop();
  initDone = open = false;
  YSE::Log().sendMessage("androidDeviceManager: Close done");
}


#endif
