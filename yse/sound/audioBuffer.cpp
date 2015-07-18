/*
  ==============================================================================

    audioBuffer.cpp
    Created: 30 Mar 2015 7:22:34pm
    Author:  yvan

  ==============================================================================
*/

#include "audioBuffer.hpp"
#include "../internalHeaders.h"


YSE::audioBuffer::audioBuffer() : valid(false) {}

Bool YSE::audioBuffer::create(const char * filename) {
  ScopedPointer<AudioFormatReader> reader;
  if (IO().getActive()) {
    // check if file exists
    if (!INTERNAL::CALLBACK::fileExists(filename)) {
      INTERNAL::LogImpl().emit(E_FILE_ERROR, "file not found for " + std::string(filename));
      return false;
    }
    
    // will be deleted by AudioFormatReader
    INTERNAL::customFileReader * cfr = new INTERNAL::customFileReader;
    cfr->create(filename);
    reader = SOUND::Manager().getReader(cfr);
  }
  else {
    // check if file exists
    File file;
    file = File::getCurrentWorkingDirectory().getChildFile(filename);
    if (!file.existsAsFile()) {
      INTERNAL::LogImpl().emit(E_FILE_ERROR, "file not found for " + file.getFullPathName().toStdString());
      return false;
    }

    reader = SOUND::Manager().getReader(file);
  }

  if (reader != nullptr) {
    juce::AudioSampleBuffer tBuf;
    tBuf.setSize(reader->numChannels, (Int)reader->lengthInSamples);
    reader->read(&tBuf, 0, (Int)reader->lengthInSamples, 0, true, true);

    sampleRateAdjustment = static_cast<Flt>(reader->sampleRate) / static_cast<Flt>(SAMPLERATE);
    length = tBuf.getNumSamples();

    // copy juce buffer to our own
    buffer.resize(tBuf.getNumChannels());
    for (int i = 0; i < buffer.size(); i++) {
      buffer[i].resize(tBuf.getNumSamples());
      const float * in = tBuf.getReadPointer(i);
      Flt * out = buffer[i].getBuffer();
      for (int j = 0; j < tBuf.getNumSamples(); j++) {
        *out++ = *in++;
      }
    }
    valid = true;
    return true;
  }
  else {
    INTERNAL::LogImpl().emit(E_FILEREADER, "Unable to read file " + String(filename));
    return false;
  }
}

int YSE::audioBuffer::getNumChannels() const {
  return buffer.size();
}

std::vector<YSE::DSP::sample> & YSE::audioBuffer::getChannels() {
  return buffer;
}

YSE::DSP::sample & YSE::audioBuffer::getChannel(Int nr) {
  jassert(nr >= 0);
  jassert(nr < buffer.size());
  return buffer[nr];
}

Bool YSE::audioBuffer::isValid() const {
  return valid;
}