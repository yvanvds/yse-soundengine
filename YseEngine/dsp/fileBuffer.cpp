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

  // Keep the load-time ratio for existing consumers, but also record the
  // rate-independent native file rate so playback speed can be re-derived
  // against the live SAMPLERATE after a close()/init() cycle (issue #637).
  fileRate = static_cast<Flt>(handle.samplerate());
  setSampleRateAdjustment(fileRate / static_cast<Flt>(SAMPLERATE));
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

// Write the buffer out as a mono WAV file (issue #580). The JUCE writer this
// replaces was never ported when load() moved to libsndfile in #174, leaving a
// stub that appended ".wav" to the caller's path, wrote nothing, and returned
// true anyway. It is now a real libsndfile write: the path is used verbatim (no
// silent suffix), samples are stored as 32-bit float so buffers holding values
// outside [-1, 1] survive the round trip, and every failure reports false.
// Runs on the calling thread — this is file I/O, never call it from the audio
// callback.
bool YSE::DSP::fileBuffer::save(const char* fileName) {
  if (fileName == nullptr) return false;

  // The custom-IO backend (BufferIO / IO()) is read-only: customFileReader
  // exposes open/read/seek callbacks but no write, so there is nowhere to put
  // the data while it is active.
  if (IO().getActive()) return false;

  const UInt length = getLength();
  if (length == 0) return false;

  // A buffer filled by load() holds the source file's frames unresampled, so
  // its native rate is the honest one to write; anything generated in-engine
  // is at the engine rate.
  const int rate = fileRate > 0.0f ? static_cast<int>(fileRate) : static_cast<int>(SAMPLERATE);

  SndfileHandle handle(fileName, SFM_WRITE, SF_FORMAT_WAV | SF_FORMAT_FLOAT, 1, rate);
  if (!handle) return false;

  const sf_count_t frames = static_cast<sf_count_t>(length);
  if (handle.writef(getPtr(), frames) != frames) return false;
  handle.writeSync();
  // SndfileHandle closes the file on destruction, at the end of this scope.
  return true;
}