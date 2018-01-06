#include "stdafx.h"
#include "Demo06_Devices.h"
#include <sstream>
#include <conio.h>


DemoDevices::DemoDevices()
{
  SetTitle("Devices Demo");

  const std::vector<YSE::device> & list = YSE::System().getDevices();
  for (unsigned int i = 0; i < list.size(); i++) {
    if (list[i].getOutputChannelNames().size() > 0) {
      std::ostringstream stream;
      stream << i << ": " << list[i].getName()
        << " on host: " << list[i].getTypeName()
        << " has " << list[i].getOutputChannelNames().size()
        << " Outputs.";
      AddAction('a' + i, stream.str(), std::bind(&DemoDevices::PickDevice, this));
    }
  }

  drone.create("..\\TestResources\\drone.ogg", nullptr, true).play();
}

void DemoDevices::ExplainDemo()
{
  std::cout << "This example just lists the available devices on your system." << std::endl;
  std::cout << "If more than one is available, you can switch to another device." << std::endl;
}

void DemoDevices::PickDevice() {
  const std::vector<YSE::device> & list = YSE::System().getDevices();
  int input = lastAction - 'a';
  
  YSE::deviceSetup setup;
  setup.setOutput(list[input]);
  YSE::System().closeCurrentDevice();
  YSE::System().openDevice(setup);
  drone.play();
}