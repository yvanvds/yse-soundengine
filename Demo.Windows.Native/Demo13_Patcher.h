#pragma once

#include "basePage.h"
#include "yse.hpp"

class DemoPatcher: public basePage {
public:
  DemoPatcher();

  void FreqUp();
  void FreqDown();

  void LfoUp();
  void LfoDown();

  void VolumeUp();
  void VolumeDown();

  void SaveToFile();

private:
  void Setup();

  YSE::sound sound;
  YSE::patcher patcher;

  YSE::pHandle * sine;
  YSE::pHandle * lfo;
  YSE::pHandle * mtof;
  YSE::pHandle * volume;

	YSE::pHandle * controlPitch;
	YSE::pHandle * controlVolume;
	YSE::pHandle * controlLFO;

  float noteValue;
  float lfoValue;
	float volumeValue;
};