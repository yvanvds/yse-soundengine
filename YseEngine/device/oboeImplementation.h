#pragma once

#if YSE_ANDROID

#include <oboe/Oboe.h>
#include <atomic>
#include <chrono>
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

  int32_t getNegotiatedSampleRate() const {
    return negotiatedSampleRate;
  }

  // Live values queried from the open Oboe stream; 0 when no stream is open.
  int32_t getNegotiatedBufferSize() const;
  int32_t getNegotiatedOutputLatencyMs() const;

  // EMA-smoothed callback wall-clock load (issue #82), mirroring the
  // PortAudio path. Read by managerObject::cpuLoad() on the control thread.
  float cpuLoad() const {
    return cpuLoadEma.load(std::memory_order_relaxed);
  }

  oboe::DataCallbackResult onAudioReady(oboe::AudioStream* stream, void* audioData,
                                        int32_t numFrames) override;

  // Stream disconnected (headphone unplug, USB removed, etc.). Oboe has already
  // closed the stream. Runs on Oboe's error thread: it only records that a
  // rebuild is due and returns — the actual reopen happens in serviceReconnect()
  // on the main thread (issue #200).
  void onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) override;

  // Perform a pending stream rebuild requested by onErrorAfterClose. Must be
  // called from the main-thread update path (never the audio or error thread)
  // so the reopen — which allocates and rewrites members the audio callback
  // reads — is serialised with close()/pause()/resume() (issue #200). No-op
  // when no reconnect has been requested.
  void serviceReconnect();

private:
  bool openStream(int channels);

  // Fold one callback's wall-clock elapsed time into cpuLoadEma.
  // Called by onAudioReady on every exit path. Single producer
  // (the Oboe callback thread), so relaxed atomics are sufficient.
  void updateCpuLoadEma(std::chrono::steady_clock::time_point cbStart, int32_t numFrames);

  std::shared_ptr<oboe::AudioStream> mStream;
  int numChannels = 2;
  int32_t negotiatedSampleRate = 44100;
  UInt bufferPos = 0;
  float** sourceChannels = nullptr;

  std::atomic<unsigned int> callbacksSinceLastUpdate{0};
  std::atomic<float> cpuLoadEma{0.f};

  // Set by onErrorAfterClose (Oboe error thread), consumed by serviceReconnect
  // (main thread). Decouples the disconnect notification from the actual stream
  // rebuild so the reopen never runs on the error thread (issue #200).
  std::atomic<bool> reconnectRequested{false};
};

#endif
