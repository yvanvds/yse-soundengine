/*
  ==============================================================================

    underWaterEffect.cpp
    Created: 1 Feb 2014 10:02:28pm
    Author:  yvan

  ==============================================================================
*/

#include "../internalHeaders.h"


YSE::INTERNAL::underWaterEffect & YSE::INTERNAL::UnderWaterEffect() {
  static underWaterEffect u;
  return u;
}

YSE::INTERNAL::underWaterEffect::underWaterEffect() : activeChannel(nullptr) {
  verb.create();
  verb.setPreset(REVERB_UNDERWATER);
  verb.setSize(10);
  verb.setActive(false);
}

YSE::INTERNAL::underWaterEffect & YSE::INTERNAL::underWaterEffect::channel(CHANNEL::implementationObject * ch) {
  activeChannel = ch;
  return *this;
}

YSE::CHANNEL::implementationObject * YSE::INTERNAL::underWaterEffect::channel() {
  return activeChannel;
}

YSE::INTERNAL::underWaterEffect & YSE::INTERNAL::underWaterEffect::setDepth(Flt value) {
  depth = value;
  if (depth > 0) {
    verb.setActive(true);
    verb.setPosition(ListenerImpl().pos);
  }
  else {
    verb.setActive(false);
  }
  return *this;
}

YSE::INTERNAL::underWaterEffect & YSE::INTERNAL::underWaterEffect::apply(MULTICHANNELBUFFER & channelBuffer) {
  if (depth > 1) {
    // sound underwater is more position neutral. Because the speed of sound 
    // is much higher, the ear cannot hear from what direction it comes.

    // first create a buffer that contains all sound with neutral positions
    for (UInt i = 0; i < channelBuffer.size(); ++i) {
      buffer += channelBuffer[i];
    }
    buffer /= static_cast<Flt>(CHANNEL::Manager().getNumberOfOutputs());

    Flt factor = 140 - (depth * 5);
    factor = DSP::MidiToFreq(factor);
    if (factor < 200) factor = 200;
    filter.setFrequency(factor);
    lpBuffer = filter(buffer);

    if (depth > 5.0f) {
      // completely disregard position info
    for (UInt i = 0; i < channelBuffer.size(); ++i) {
      channelBuffer[i] = lpBuffer;
      }
    }
    else {
      // partly replace with position-neutral version
      lpBuffer *= (depth / 5.0f);
      for (UInt i = 0; i < channelBuffer.size(); ++i) {
        channelBuffer[i] *= (5 - depth) / 5.0f;
        channelBuffer[i] += lpBuffer;
      }
    }
  }
  return *this;
}