#include "oboeImplementation.h"

#if YSE_ANDROID

#include <chrono>
#include <cmath>
#include "../implementations/logImplementation.h"
#include "../internalHeaders.h"
#include "../internal/denormalGuard.h"

namespace {
  // See portaudioDeviceManager.cpp for the rationale on the time constant.
  constexpr double kCpuLoadTau = 1.0;
} // namespace

OboeImplementation::OboeImplementation() : bufferPos(YSE::STANDARD_BUFFERSIZE) {}

OboeImplementation::~OboeImplementation() {
  Stop();
  delete[] sourceChannels;
}

bool OboeImplementation::Setup() {
  YSE::INTERNAL::LogImpl().emit(YSE::E_DEBUG, "Oboe: Setup");
  return true;
}

bool OboeImplementation::openStream(int channels) {
  numChannels = channels;

  oboe::AudioStreamBuilder builder;
  builder.setDirection(oboe::Direction::Output)
      ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
      ->setSharingMode(oboe::SharingMode::Exclusive)
      ->setFormat(oboe::AudioFormat::Float)
      ->setChannelCount(channels)
      ->setDataCallback(this)
      ->setErrorCallback(this);

  oboe::Result result = builder.openStream(mStream);
  if (result != oboe::Result::OK) {
    YSE::INTERNAL::LogImpl().emit(YSE::E_ERROR, "Oboe: openStream failed");
    mStream.reset();
    return false;
  }

  negotiatedSampleRate = mStream->getSampleRate();
  // Session-locked: once the lock is set at the end of system::initShared(),
  // SAMPLERATE can only be re-written to its current value (e.g. by the
  // pause()/resume() reopen path, or onErrorAfterClose rebuild on the same
  // device). A debug assert catches genuine mid-session rate-change attempts
  // (e.g. headphone-disconnect landing on a different rate); the write is
  // skipped so SAMPLERATE-derived caches stay coherent.
  {
    const UInt newRate = (UInt)negotiatedSampleRate;
    assert(!YSE::INTERNAL::Global().isSampleRateLocked() || newRate == YSE::SAMPLERATE);
    if (!YSE::INTERNAL::Global().isSampleRateLocked()) {
      YSE::SAMPLERATE = newRate;
    }
  }

  delete[] sourceChannels;
  sourceChannels = new float*[numChannels];
  bufferPos = YSE::STANDARD_BUFFERSIZE;

  result = mStream->requestStart();
  if (result != oboe::Result::OK) {
    YSE::INTERNAL::LogImpl().emit(YSE::E_ERROR, "Oboe: requestStart failed");
    mStream->close();
    mStream.reset();
    return false;
  }

  return true;
}

bool OboeImplementation::Start(int channels) {
  YSE::INTERNAL::LogImpl().emit(YSE::E_DEBUG, "Oboe: Start");
  if (mStream) Stop();
  return openStream(channels);
}

void OboeImplementation::Stop() {
  if (!mStream) return;
  YSE::INTERNAL::LogImpl().emit(YSE::E_DEBUG, "Oboe: Stop");
  mStream->stop();
  mStream->close();
  mStream.reset();
  cpuLoadEma.store(0.f, std::memory_order_relaxed);
}

void OboeImplementation::Suspend() {
  if (mStream) mStream->pause();
}

void OboeImplementation::Resume() {
  if (mStream) mStream->start();
}

unsigned int OboeImplementation::GetCallbacksSinceLastUpdate() {
  return callbacksSinceLastUpdate.exchange(0);
}

int32_t OboeImplementation::getNegotiatedBufferSize() const {
  return mStream ? mStream->getFramesPerBurst() : 0;
}

int32_t OboeImplementation::getNegotiatedOutputLatencyMs() const {
  if (!mStream) return 0;
  auto result = mStream->calculateLatencyMillis();
  // result is a ResultWithValue<double>; only return the value when the
  // underlying calculation succeeded (some streams report ErrorUnimplemented
  // when the hardware can't sample timestamps).
  return result ? (int32_t)result.value() : 0;
}

oboe::DataCallbackResult OboeImplementation::onAudioReady(oboe::AudioStream* /*stream*/,
                                                          void* audioData, int32_t numFrames) {
  YSE::INTERNAL::enableFlushToZero();
  // Issue #82: our own callback wall-clock measurement, mirroring the
  // PortAudio path so system::cpuLoad() reports a consistent number
  // across backends.
  const auto cbStart = std::chrono::steady_clock::now();
  ++callbacksSinceLastUpdate;

  float* dest = static_cast<float*>(audioData);

  if (!YSE::DEVICE::Manager().doOnCallback(numFrames)) {
    std::memset(dest, 0, sizeof(float) * numFrames * numChannels);
    updateCpuLoadEma(cbStart, numFrames);
    return oboe::DataCallbackResult::Continue;
  }

  UInt pos = 0;
  const UInt totalFrames = (UInt)numFrames;
  auto& master = YSE::DEVICE::Manager().getMaster();
  auto& out = master.GetBuffers();

  while (pos < totalFrames) {
    if (bufferPos == YSE::STANDARD_BUFFERSIZE) {
      master.dsp();
      master.buffersToParent();
      bufferPos = 0;
    }

    UInt size = (totalFrames - pos) > (YSE::STANDARD_BUFFERSIZE - bufferPos)
                    ? (YSE::STANDARD_BUFFERSIZE - bufferPos)
                    : (totalFrames - pos);

    for (int i = 0; i < numChannels; i++) {
      sourceChannels[i] = out[i].getPtr() + bufferPos;
    }

    UInt l = size;
    while (l--) {
      for (int i = 0; i < numChannels; i++) {
        *dest++ = *sourceChannels[i]++;
      }
    }

    bufferPos += size;
    pos += size;
  }

  updateCpuLoadEma(cbStart, numFrames);
  return oboe::DataCallbackResult::Continue;
}

void OboeImplementation::updateCpuLoadEma(std::chrono::steady_clock::time_point cbStart,
                                          int32_t numFrames) {
  const auto cbEnd = std::chrono::steady_clock::now();
  const double elapsedSec = std::chrono::duration<double>(cbEnd - cbStart).count();
  const double bufferSec = double(numFrames) / double(YSE::SAMPLERATE);
  if (bufferSec <= 0.0) return;

  const float sample = float(elapsedSec / bufferSec);
  const float alpha = float(1.0 - std::exp(-bufferSec / kCpuLoadTau));
  const float prev = cpuLoadEma.load(std::memory_order_relaxed);
  cpuLoadEma.store(prev + alpha * (sample - prev), std::memory_order_relaxed);
}

void OboeImplementation::onErrorAfterClose(oboe::AudioStream* /*stream*/, oboe::Result error) {
  // Oboe has already closed the stream by the time this fires (vs. onErrorBeforeClose).
  // Headphone unplug / USB device removal trips Disconnected; transparently rebuild.
  YSE::INTERNAL::LogImpl().emit(YSE::E_DEBUG, "Oboe: stream disconnected, restarting");
  if (error == oboe::Result::ErrorDisconnected) {
    mStream.reset();
    openStream(numChannels);
  }
}

#endif
