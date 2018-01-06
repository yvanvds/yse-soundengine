#include "stdafx.h"
#include "Demo05_Reverb.h"


DemoReverb::DemoReverb()
{
  snare.create("..\\TestResources\\snare.ogg", nullptr, true).play();
  
  // set global reverb
  YSE::System().getGlobalReverb().setActive(true);
  YSE::System().getGlobalReverb().setPreset(YSE::REVERB_GENERIC);
  YSE::ChannelMaster().attachReverb();

  // 'world' reverbs can be added at specific positions
  // size is the maximum distance from the reverb at which it its influence is at maximum level
  // rolloff indicates how far outside its size it will drop to zero influence (linear curve)

  // add reverb at 5 meter
  bathroom.create();
  bathroom.setPosition(YSE::Pos(0, 0, 5)).setSize(1).setRollOff(1);
  bathroom.setPreset(YSE::REVERB_BATHROOM);

  // add reverb at 10 meter
  hall.create();
  hall.setPosition(YSE::Pos(0, 0, 10)).setSize(1).setRollOff(1);
  hall.setPreset(YSE::REVERB_HALL);

  // add reverb at 15 meter
  sewer.create();
  sewer.setPosition(YSE::Pos(0, 0, 15)).setSize(1).setRollOff(1);
  sewer.setPreset(YSE::REVERB_SEWERPIPE);

  // add reverb at 20 meter
  custom.create();
  custom.setPosition(YSE::Pos(0, 0, 20)).setSize(1).setRollOff(1);
  // for this reverb we use custom parameters instead of a preset
  // (these are meant to sound awkward)
  custom.setRoomSize(1.0f).setDamping(0.1f).setDryWetBalance(0.0f, 1.0f).setModulation(6.5, 0.7);
  custom.setReflection(0, 1000, 0.5f).setReflection(1, 1500, 0.6f);
  custom.setReflection(2, 2100, 0.8f).setReflection(3, 2999, 0.9f);

  SetTitle("Reverb Demo");
  AddAction('q', "Move sound and listener forward.", std::bind(&DemoReverb::MoveForward, this));
  AddAction('a', "Move sound and listener backward.", std::bind(&DemoReverb::MoveBack, this));
  AddAction('1', "Turn global reverb on.", std::bind(&DemoReverb::GlobalReverbOn, this));
  AddAction('2', "Turn global reverb off.", std::bind(&DemoReverb::GlobalReverbOff, this));
}


DemoReverb::~DemoReverb()
{
}

void DemoReverb::ExplainDemo()
{
  std::cout << "This example as one global reverb. On top of that, there are several localized reverbs which will alter the listener's experience when moving around." << std::endl;
}

void DemoReverb::MoveForward()
{
  YSE::Pos pos = YSE::Listener().pos();
  pos.z += 0.1;
  YSE::Listener().pos(pos);
  snare.pos(pos);
}

void DemoReverb::MoveBack()
{
  YSE::Pos pos = YSE::Listener().pos();
  pos.z -= 0.1;
  YSE::Listener().pos(pos);
  snare.pos(pos);
}

void DemoReverb::GlobalReverbOn()
{
  YSE::System().getGlobalReverb().setActive(true);
}

void DemoReverb::GlobalReverbOff()
{
  YSE::System().getGlobalReverb().setActive(false);
}
