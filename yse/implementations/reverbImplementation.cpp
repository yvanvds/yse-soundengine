/*
  ==============================================================================

    reverbImplementation.cpp
    Created: 10 Mar 2014 8:02:05pm
    Author:  yvan

  ==============================================================================
*/

#include "reverbImplementation.h"

YSE::INTERNAL::reverbImplementation::reverbImplementation(YSE::reverb * head) : implementation<reverb>(head) {
  for (Int i = 0; i < 4; i++) {
    earlyPtr[i] = 0;
    earlyGain[i] = 0;
  }
}

void YSE::INTERNAL::reverbImplementation::parseMessage(const reverb::message & message) {
  switch (message.message) {
    case YSE::reverb::POSITION: {
      position.x = message.vecValue[0];
      position.y = message.vecValue[1];
      position.z = message.vecValue[2];
      break;
    }
    
    case YSE::reverb::SIZE: {
      size = message.floatValue;
      break;
    }
    
    case YSE::reverb::ROLLOFF: {
      rolloff = message.floatValue;
      break;
    }
    
    case YSE::reverb::ACTIVE: {
      active = message.boolValue;
      break;
    }
    
    case YSE::reverb::ROOMSIZE: {
      roomsize = message.floatValue;
      break;
    }
    
    case YSE::reverb::DAMP: {
      damp = message.floatValue;
      break;
    }
    
    case YSE::reverb::DRY_WET: {
      dry = message.vecValue[0];
      wet = message.vecValue[1];
      break;
    }
    
    case YSE::reverb::MODULATION: {
      modFrequency = message.vecValue[0];
      modWidth = message.vecValue[1];
      break;
    }
    
    case YSE::reverb::REFLECTION: {
      Int reflection = static_cast<int>(message.vecValue[0]);
      earlyPtr[reflection] = static_cast<int>(message.vecValue[1]);
      earlyGain[reflection] = message.vecValue[2];
      break;
    }
  }
}