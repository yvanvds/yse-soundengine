/*
  ==============================================================================

    reverbImplementation.cpp
    Created: 10 Mar 2014 8:02:05pm
    Author:  yvan

  ==============================================================================
*/


#include "reverbImplementation.h"


YSE::REVERB::implementationObject::implementationObject(YSE::reverb * head) 
  : head(head),
  objectStatus(OBJECT_CONSTRUCTED),
  position(0),
  size(0),
  rolloff(0),
  roomsize(0.5f),
  damp(0.5f),
  dry(0.5f),
  wet(0.5f),
  modFrequency(0),
  modWidth(0),
  active(true) {
  for (Int i = 0; i < 4; i++) {
    earlyPtr[i] = 0;
    earlyGain[i] = 0;
  }
}

YSE::REVERB::implementationObject::~implementationObject() {
  if (head.load() != nullptr) {
    head.load()->pimpl = nullptr;
  }
}

Bool YSE::REVERB::implementationObject::readyCheck() {
  if (objectStatus == OBJECT_READY) {
    return false;
  }
  if(objectStatus == OBJECT_SETUP) {
    objectStatus = OBJECT_READY;
    return true;
  }
  return false;
}


void YSE::REVERB::implementationObject::removeInterface() {
  head = nullptr;
}

YSE::OBJECT_IMPLEMENTATION_STATE YSE::REVERB::implementationObject::getStatus() {
  return objectStatus.load();
}

void YSE::REVERB::implementationObject::setStatus(OBJECT_IMPLEMENTATION_STATE value) {
  objectStatus.store(value);
}

void YSE::REVERB::implementationObject::sync() {
  if (head.load() == nullptr) {
    objectStatus = OBJECT_RELEASE;
    return;
  }

  messageObject message;
  while (messages.try_pop(message)) {
    parseMessage(message);
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

