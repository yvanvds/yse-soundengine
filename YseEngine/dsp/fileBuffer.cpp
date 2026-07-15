/*
  ==============================================================================

    fileBuffer.cpp
    Created: 7 Aug 2015 1:31:44pm
    Author:  yvan

  ==============================================================================
*/

#include "fileBuffer.hpp"
#include "../internalHeaders.h"
#include "../internal/customFileReader.h"
#include "../io.hpp"

#include <algorithm>
#include <sndfile.hh>
#include <vector>

// Resident full-preload of one channel of an audio file into this buffer
// (issue #174). #173 deferred PCM decode to the sampler as the first consumer,
// because this was a non-functional stub. It is now a real libsndfile-backed
// load: the whole file is decoded into RAM on the calling (setup / slow-pool)
// thread — never the audio thread — so the sampler voice's process() is a pure
// in-RAM interpolated read (spec §9/§10). Honours the custom-IO backend
// (BufferIO / IO()) exactly as INTERNAL::soundFile::loadNonStreaming does.
bool YSE::DSP::fileBuffer::load(const char* fileName, UInt channel) {
  if (fileName == nullptr) return false;

  void* ioHandle = nullptr;
  SndfileHandle handle;
  if (IO().getActive()) {
    long long size = 0;
    if (!INTERNAL::customFileReader::Open(fileName, &size, &ioHandle)) return false;
    handle = SndfileHandle(INTERNAL::customFileReader::GetVIO(), ioHandle);
  } else {
    handle = SndfileHandle(fileName);
  }

  if (!handle) {
    if (ioHandle != nullptr) INTERNAL::customFileReader::Close(ioHandle);
    return false;
  }

  const int fileChannels = handle.channels();
  const long frames = static_cast<long>(handle.frames());
  if (static_cast<int>(channel) >= fileChannels || frames <= 0) {
    if (ioHandle != nullptr) INTERNAL::customFileReader::Close(ioHandle);
    return false;
  }

  setSampleRateAdjustment(static_cast<Flt>(handle.samplerate()) / static_cast<Flt>(SAMPLERATE));
  resize(static_cast<UInt>(frames));
  Flt* out = getPtr();

  // Read interleaved in bounded chunks and pull out the requested channel. A
  // fixed scratch keeps peak memory low for many-channel files.
  const long kChunkFrames = 8192;
  std::vector<float> scratch(static_cast<size_t>(kChunkFrames) * fileChannels);
  long done = 0;
  while (done < frames) {
    sf_count_t want = std::min<long>(kChunkFrames, frames - done);
    sf_count_t got = handle.readf(scratch.data(), want);
    if (got <= 0) break;
    for (sf_count_t i = 0; i < got; ++i) {
      out[done + i] = scratch[static_cast<size_t>(i) * fileChannels + channel];
    }
    done += got;
  }

  if (ioHandle != nullptr) INTERNAL::customFileReader::Close(ioHandle);
  copyOverflow();
  return true;
}

bool YSE::DSP::fileBuffer::save(const char* fileName) {
  std::string fn = fileName;
  fn += ".wav";

  if (IO().getActive()) {
    return false; // not implemented yet
  } else {
    // check if file exists
    /*File file;
    file = File::getCurrentWorkingDirectory().getChildFile(fn.c_str());
    file.deleteFile();
    ScopedPointer<FileOutputStream> fileStream(file.createOutputStream());

    if (fileStream != nullptr) {
      WavAudioFormat wavFormat;
      AudioFormatWriter * writer = wavFormat.createWriterFor(fileStream, SAMPLERATE, 1, 16,
    StringPairArray(), 0);

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