/*
  ==============================================================================

    juceSoundFile.cpp
    Created: 28 Jul 2016 9:29:26pm
    Author:  yvan

  ==============================================================================
*/

#if JUCE_BACKEND

#include "../internalHeaders.h"

void YSE::INTERNAL::soundFile::loadStreaming() {

  File file;
  if (IO().getActive()) {
    // will be deleted by AudioFormatReader
    customFileReader * cfr = new customFileReader;
    cfr->create(fileName.c_str());
    streamReader = SOUND::Manager().getReader(cfr);
  }
  else {
    file = File::getCurrentWorkingDirectory().getChildFile(juce::String(fileName));
    streamReader = SOUND::Manager().getReader(file);
  }

  if (streamReader != nullptr) {
    _fileBuffer.setSize(streamReader->numChannels, STREAM_BUFFERSIZE);
    // sample rate adjustment
    _sampleRateAdjustment = static_cast<Flt>(streamReader->sampleRate) / static_cast<Flt>(SAMPLERATE);
    _length = (Int)streamReader->lengthInSamples;
    _buffer = _fileBuffer.getArrayOfReadPointers();
    _channels = _fileBuffer.getNumChannels();
    _streamPos = 0;
    fillStream(false);
    // file is ready for use now
    state = READY;
  }
  else {
    LogImpl().emit(E_FILEREADER, "Unable to read " + file.getFullPathName().toStdString());
    state = INVALID;
  }
}


void YSE::INTERNAL::soundFile::loadNonStreaming() {
  // load non streaming sounds in one go
  ScopedPointer<AudioFormatReader> reader;
  File file;

  if (IO().getActive()) {
    // will be deleted by AudioFormatReader
    customFileReader * cfr = new customFileReader;
    cfr->create(fileName.c_str());
    reader = SOUND::Manager().getReader(cfr);
  }
  else {
    file = File::getCurrentWorkingDirectory().getChildFile(juce::String(fileName));
    reader = SOUND::Manager().getReader(file);
  }

  if (reader != nullptr) {
    _fileBuffer.setSize(reader->numChannels, (Int)reader->lengthInSamples);
    reader->read(&_fileBuffer, 0, (Int)reader->lengthInSamples, 0, true, true);
    // sample rate adjustment
    _sampleRateAdjustment = static_cast<Flt>(reader->sampleRate) / static_cast<Flt>(SAMPLERATE);
    _length = _fileBuffer.getNumSamples();
    _buffer = _fileBuffer.getArrayOfReadPointers();
    _channels = _fileBuffer.getNumChannels();

    // file is ready for use now
    state = READY;
  }
  else {
    LogImpl().emit(E_FILEREADER, "Unable to read " + file.getFullPathName().toStdString());
    state = INVALID;
  }
}

Bool YSE::INTERNAL::soundFile::fillStream(Bool loop) {
  if (!loop) {
    streamReader->read(&_fileBuffer, 0, (Int)_fileBuffer.getNumSamples(), _streamPos, true, true);
    _streamPos += (Int)_fileBuffer.getNumSamples();
    if (_streamPos >= (Int)streamReader->lengthInSamples) {
      // end of file reached
      return false;
    }
    else {
      // file is not done yet
      return true;
    }
  }
  else {
    // looping sound, so we have to keep refilling the buffer
    Int bufferPos = 0;
    while (bufferPos < STREAM_BUFFERSIZE) {
      // determine how many samples we can get before _streamPos needs to reset
      Int samplesToGet;
      if (_streamPos + (STREAM_BUFFERSIZE - bufferPos) >(Int)streamReader->lengthInSamples) {
        samplesToGet = (Int)streamReader->lengthInSamples - _streamPos;
      }
      else {
        samplesToGet = STREAM_BUFFERSIZE - bufferPos;
      }

      streamReader->read(&_fileBuffer, bufferPos, samplesToGet, _streamPos, true, true);
      bufferPos += samplesToGet;
      _streamPos += samplesToGet;
      if (_streamPos >= (Int)streamReader->lengthInSamples) {
        // restart file
        _streamPos -= (Int)streamReader->lengthInSamples;
      }
    }
  }

  return true;
}




#endif