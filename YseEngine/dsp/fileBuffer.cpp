/*
  ==============================================================================

    fileBuffer.cpp
    Created: 7 Aug 2015 1:31:44pm
    Author:  yvan

  ==============================================================================
*/

#include "fileBuffer.hpp"
#include "../internalHeaders.h"
#include "../io.hpp"

/*namespace YSE {
  namespace DSP {

    // exists in sampleFunctions.cpp
    juce::AudioFormatReader * getReader(const char * fileName);
  }
}*/

bool YSE::DSP::fileBuffer::load(const char * fileName, UInt channel) {
  /*ScopedPointer<AudioFormatReader> reader = getReader(fileName);
  if (reader == nullptr) return false;
  if (channel >= reader->numChannels) return false;

  juce::AudioSampleBuffer tBuf;
  tBuf.setSize(reader->numChannels, (Int)reader->lengthInSamples);
  reader->read(&tBuf, 0, (Int)reader->lengthInSamples, 0, true, true);

  setSampleRateAdjustment(static_cast<Flt>(reader->sampleRate) / static_cast<Flt>(SAMPLERATE));

  resize(tBuf.getNumSamples());
  const float * in = tBuf.getReadPointer(channel);
  Flt * out = getPtr();
  for (int i = 0; i < tBuf.getNumSamples(); i++) {
    *out++ = *in++;
  }*/

  copyOverflow();
  return true;
}

bool YSE::DSP::fileBuffer::save(const char * fileName) {
  std::string fn = fileName;
  fn += ".wav";

  if (IO().getActive()) {
    return false; // not implemented yet
  }
  else {
    // check if file exists
    /*File file;
    file = File::getCurrentWorkingDirectory().getChildFile(fn.c_str());
    file.deleteFile();
    ScopedPointer<FileOutputStream> fileStream(file.createOutputStream());

    if (fileStream != nullptr) {
      WavAudioFormat wavFormat;
      AudioFormatWriter * writer = wavFormat.createWriterFor(fileStream, SAMPLERATE, 1, 16, StringPairArray(), 0);

      if (writer != nullptr) {
        fileStream.release();

        float ** array = new float*[1];
        array[0] = getPtr();

        writer->writeFromFloatArrays(array, 1, getLength());
        writer->flush();
        delete[] array;
      }
    }
	*/
  }

  return true;
}