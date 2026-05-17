#pragma once

#if YSE_ANDROID

#include <oboe/Oboe.h>
#include <atomic>
#include <memory>
#include "../headers/types.hpp"

// Oboe-backed playback implementation. Replaces the legacy OpenSL ES path.
// Drives YSE::DEVICE::Manager().getMaster() from the realtime callback,
// rendering interleaved float to the stream's buffer.
class OboeImplementation : public oboe::AudioStreamDataCallback,
                           public oboe::AudioStreamErrorCallback {
public:
  OboeImplementation();
  ~OboeImplementation() override;

  bool Setup();
  bool Start(int channels);
  void Stop();
  void Suspend();
  void Resume();
  unsigned int GetCallbacksSinceLastUpdate();

  int32_t getNegotiatedSampleRate() const { return negotiatedSampleRate; }

  // Live values queried from the open Oboe stream; 0 when no stream is open.
  int32_t getNegotiatedBufferSize() const;
  int32_t getNegotiatedOutputLatencyMs() const;

  oboe::DataCallbackResult onAudioReady(oboe::AudioStream * stream,
                                        void * audioData,
                                        int32_t numFrames) override;

  // Stream disconnected (headphone unplug, USB removed, etc.). Oboe has already
  // closed the stream; we rebuild it on a non-realtime thread.
  void onErrorAfterClose(oboe::AudioStream * stream, oboe::Result error) override;

private:
  bool openStream(int channels);

  std::shared_ptr<oboe::AudioStream> mStream;
  int numChannels = 2;
  int32_t negotiatedSampleRate = 44100;
  UInt bufferPos = 0;
  float ** sourceChannels = nullptr;

  std::atomic<unsigned int> callbacksSinceLastUpdate{0};
};

#endif
