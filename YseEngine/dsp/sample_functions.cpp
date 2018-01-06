/*
  ==============================================================================

    sample_functions.cpp
    Created: 19 Jul 2015 8:36:47pm
    Author:  yvan

  ==============================================================================
*/

#include "sample_functions.hpp"
#include "../internalHeaders.h"
#include "../io.hpp"
/*
namespace YSE {
  namespace DSP {

    // internal function to retrieve a reader for an audio file
    juce::AudioFormatReader * getReader(const char * fileName) {
      if (IO().getActive()) {
        // check if file exists
        if (!INTERNAL::CALLBACK::fileExists(fileName)) {
          INTERNAL::LogImpl().emit(E_FILE_ERROR, "file not found for " + std::string(fileName));
          return nullptr;
        }

        // will be deleted by AudioFormatReader
        INTERNAL::customFileReader * cfr = new INTERNAL::customFileReader;
        cfr->create(fileName);
        return SOUND::Manager().getReader(cfr);
      }
      else {
        // check if file exists
        File file;
        file = File::getCurrentWorkingDirectory().getChildFile(fileName);
        if (!file.existsAsFile()) {
          INTERNAL::LogImpl().emit(E_FILE_ERROR, "file not found for " + file.getFullPathName().toStdString());
          return nullptr;
        }

        return SOUND::Manager().getReader(file);
      }
    }



  }
}*/

bool YSE::DSP::LoadFromFile(const char * fileName, YSE::DSP::buffer & buffer, UInt channel) {
  /*ScopedPointer<AudioFormatReader> reader = getReader(fileName);
  if (reader == nullptr) return false;
  if (channel >= reader->numChannels) return false;

  juce::AudioSampleBuffer tBuf;
  tBuf.setSize(reader->numChannels, (Int)reader->lengthInSamples);
  reader->read(&tBuf, 0, (Int)reader->lengthInSamples, 0, true, true);

  buffer.setSampleRateAdjustment(static_cast<Flt>(reader->sampleRate) / static_cast<Flt>(SAMPLERATE));

  buffer.resize(tBuf.getNumSamples());
  const float * in = tBuf.getReadPointer(channel);
  Flt * out = buffer.getPtr();
  for (int i = 0; i < tBuf.getNumSamples(); i++) {
    *out++ = *in++;
  }

  buffer.copyOverflow();*/
  return true;
  
}

bool YSE::DSP::LoadFromFile(const char * fileName, MULTICHANNELBUFFER & buffer) {
  /*ScopedPointer<AudioFormatReader> reader = getReader(fileName);
  if (reader == nullptr) return false;

  juce::AudioSampleBuffer tBuf;
  tBuf.setSize(reader->numChannels, (Int)reader->lengthInSamples);
  reader->read(&tBuf, 0, (Int)reader->lengthInSamples, 0, true, true);

  buffer.resize(reader->numChannels);
  for (unsigned int i = 0; i < buffer.size(); i++) {
    buffer[i].setSampleRateAdjustment(static_cast<Flt>(reader->sampleRate) / static_cast<Flt>(SAMPLERATE));
    buffer[i].resize(tBuf.getNumSamples());

    const float * in = tBuf.getReadPointer(i);
    Flt * out = buffer[i].getPtr();
    for (int j = 0; j < tBuf.getNumSamples(); j++) {
      *out++ = *in++;
    }

    buffer[i].copyOverflow();
  }*/
  return true;
}
  
bool YSE::DSP::SaveToFile(const char * fileName, YSE::DSP::buffer & buffer) {
  /*std::string fn = fileName;
  fn += ".wav";

  if (IO().getActive()) {
    return false; // not implemented yet
  }
  else {
    // check if file exists
    File file;
    file = File::getCurrentWorkingDirectory().getChildFile(fn.c_str());
    file.deleteFile();
    ScopedPointer<FileOutputStream> fileStream(file.createOutputStream());

    if (fileStream != nullptr) {
      WavAudioFormat wavFormat;
      AudioFormatWriter * writer = wavFormat.createWriterFor(fileStream, SAMPLERATE, 1, 16, StringPairArray(), 0);

      if (writer != nullptr) {
        fileStream.release();

        float ** array = new float*[1];
        array[0] = buffer.getPtr();

        writer->writeFromFloatArrays(array, 1, buffer.getLength());
        writer->flush();
        delete[] array;
      }
    }

  }*/

  return true;
}

bool YSE::DSP::SaveToFile(const char * fileName, MULTICHANNELBUFFER & buffer) {
  /*std::string fn = fileName;
  fn += ".wav";

  if (IO().getActive()) {
    return false; // not implemented yet
  }
  else {
    // check if file exists
    File file;
    file = File::getCurrentWorkingDirectory().getChildFile(fn.c_str());
    file.deleteFile();
    ScopedPointer<FileOutputStream> fileStream(file.createOutputStream());

    if (fileStream != nullptr) {
      WavAudioFormat wavFormat;
      AudioFormatWriter * writer = wavFormat.createWriterFor(fileStream, SAMPLERATE, buffer.size(), 16, StringPairArray(), 0);

      if (writer != nullptr) {
        fileStream.release();

        float ** array = new float*[buffer.size()];
        for (unsigned int i = 0; i < buffer.size(); i++) {
          array[i] = buffer[i].getPtr();
        }

        writer->writeFromFloatArrays(array, buffer.size(), buffer[0].getLength());
        writer->flush();
        delete[] array;
      }
    }

  }*/

  return true;
}

void YSE::DSP::Normalize(buffer & buffer) {
  Flt max = getMaxAmplitude(buffer);

  if (max != 0.f) {
    Flt multiplier = 1 / max;
    buffer *= multiplier;
  }
}