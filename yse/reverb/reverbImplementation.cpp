/*
  ==============================================================================

    reverbImplementation.cpp
    Created: 10 Mar 2014 8:02:05pm
    Author:  yvan

  ==============================================================================
*/


#include "reverbImplementation.h"


YSE::REVERB::implementationObject::implementationObject(interfaceObject * head) : super(head) {
  for (Int i = 0; i < 4; i++) {
    earlyPtr[i] = 0;
    earlyGain[i] = 0;
  }
}

void YSE::REVERB::implementationObject::parseMessage(const messageObject & message) {
  switch (message.ID) {
    case MESSAGE::POSITION: {
      position.x = message.vecValue[0];
      position.y = message.vecValue[1];
      position.z = message.vecValue[2];
      break;
    }
    
    case MESSAGE::SIZE: {
      size = message.floatValue;
      break;
    }
    
    case MESSAGE::ROLLOFF: {
      rolloff = message.floatValue;
      break;
    }
    
    case MESSAGE::ACTIVE: {
      active = message.boolValue;
      break;
    }
    
    case MESSAGE::ROOMSIZE: {
      roomsize = message.floatValue;
      break;
    }
    
    case MESSAGE::DAMP: {
      damp = message.floatValue;
      break;
    }
    
    case MESSAGE::DRY_WET: {
      dry = message.vecValue[0];
      wet = message.vecValue[1];
      break;
    }
    
    case MESSAGE::MODULATION: {
      modFrequency = message.vecValue[0];
      modWidth = message.vecValue[1];
      break;
    }
    
    case MESSAGE::REFLECTION: {
      Int reflection = static_cast<int>(message.vecValue[0]);
      earlyPtr[reflection] = static_cast<int>(message.vecValue[1]);
      earlyGain[reflection] = message.vecValue[2];
      break;
    }
  }
}