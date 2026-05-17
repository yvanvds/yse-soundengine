#include "oboeImplementation.h"

#if YSE_ANDROID

#include "../implementations/logImplementation.h"
#include "../internalHeaders.h"

OboeImplementation::OboeImplementation()
  : bufferPos(YSE::STANDARD_BUFFERSIZE)
{}

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
  YSE::SAMPLERATE = (UInt)negotiatedSampleRate;

  delete[] sourceChannels;
  sourceChannels = new float * [numChannels];
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

oboe::DataCallbackResult OboeImplementation::onAudioReady(oboe::AudioStream * /*stream*/,
                                                          void * audioData,
                                                          int32_t numFrames) {
  ++callbacksSinceLastUpdate;

  float * dest = static_cast<float *>(audioData);

  if (!YSE::DEVICE::Manager().doOnCallback(numFrames)) {
    std::memset(dest, 0, sizeof(float) * numFrames * numChannels);
    return oboe::DataCallbackResult::Continue;
  }

  UInt pos = 0;
  const UInt totalFrames = (UInt)numFrames;
  auto & master = YSE::DEVICE::Manager().getMaster();
  auto & out = master.GetBuffers();

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

  return oboe::DataCallbackResult::Continue;
}

void OboeImplementation::onErrorAfterClose(oboe::AudioStream * /*stream*/, oboe::Result error) {
  // Oboe has already closed the stream by the time this fires (vs. onErrorBeforeClose).
  // Headphone unplug / USB device removal trips Disconnected; transparently rebuild.
  YSE::INTERNAL::LogImpl().emit(YSE::E_DEBUG, "Oboe: stream disconnected, restarting");
  if (error == oboe::Result::ErrorDisconnected) {
    mStream.reset();
    openStream(numChannels);
  }
}

#endif
