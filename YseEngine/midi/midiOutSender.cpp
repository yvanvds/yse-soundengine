/*
  ==============================================================================

    midiOutSender.cpp
    Created for issue #350 — clip transport external MIDI-out sink.

  ==============================================================================
*/

#include "midiOutSender.h"

#if YSE_ENABLE_MIDI_DEVICE

#include <algorithm>
#include <cmath>

#include "RtMidi.h"
#include "midiDeviceManager.h"

namespace {

  unsigned char channelNibble(int channel) {
    // clipEvent channels are 1..16; the status low nibble is 0..15.
    return static_cast<unsigned char>(std::clamp(channel - 1, 0, 15));
  }

  unsigned char data7(int value) {
    return static_cast<unsigned char>(std::clamp(value, 0, 127));
  }

  unsigned char velocity7(float normalized) {
    return data7(static_cast<int>(std::lround(normalized * 127.f)));
  }

} // namespace

YSE::MIDI::outEvent YSE::MIDI::makeNoteOn(RtMidiOut* port, std::int64_t dueNs, int channel,
                                          int pitch, float velocity) {
  outEvent e;
  e.dueNs = dueNs;
  e.port = port;
  e.len = 3;
  e.bytes[0] = static_cast<unsigned char>(0x90 | channelNibble(channel));
  e.bytes[1] = data7(pitch);
  e.bytes[2] = velocity7(velocity);
  return e;
}

YSE::MIDI::outEvent YSE::MIDI::makeNoteOff(RtMidiOut* port, std::int64_t dueNs, int channel,
                                           int pitch, float velocity) {
  outEvent e;
  e.dueNs = dueNs;
  e.port = port;
  e.len = 3;
  e.bytes[0] = static_cast<unsigned char>(0x80 | channelNibble(channel));
  e.bytes[1] = data7(pitch);
  e.bytes[2] = velocity7(velocity);
  return e;
}

YSE::MIDI::outEvent YSE::MIDI::makePitchWheel(RtMidiOut* port, std::int64_t dueNs, int channel,
                                              float bend) {
  // Normalized [-1, 1] -> 14-bit 0..16383, center 8192.
  const int value = std::clamp(8192 + static_cast<int>(std::lround(bend * 8192.f)), 0, 16383);
  outEvent e;
  e.dueNs = dueNs;
  e.port = port;
  e.len = 3;
  e.bytes[0] = static_cast<unsigned char>(0xE0 | channelNibble(channel));
  e.bytes[1] = static_cast<unsigned char>(value & 0x7F);
  e.bytes[2] = static_cast<unsigned char>((value >> 7) & 0x7F);
  return e;
}

YSE::MIDI::outSender& YSE::MIDI::OutSender() {
  static outSender s;
  return s;
}

YSE::MIDI::outSender::~outSender() {
  stop();
}

void YSE::MIDI::outSender::start() {
  if (running.exchange(true, std::memory_order_acq_rel)) return; // already running
  worker = std::thread(&outSender::run, this);
}

void YSE::MIDI::outSender::stop() {
  running.store(false, std::memory_order_release);
  if (worker.joinable()) worker.join();
  // The worker is joined (or was never started), so this thread is now the
  // queue's only consumer. Flush what is still pending immediately — a stopping
  // clip's releaseAll note-offs must reach the hardware even at shutdown.
  outEvent e;
  while (queue.try_pop(e))
    send(e);
}

void YSE::MIDI::outSender::run() {
  // System::init raises the Windows timer resolution to 1 ms (timeBeginPeriod)
  // for the whole process, so the sleeps below wake with ~1 ms granularity.
  while (running.load(std::memory_order_acquire)) {
    outEvent* next = queue.peek();
    if (next == nullptr) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    const std::int64_t now = nowNs();
    if (next->dueNs > now) {
      // Sleep toward the deadline, but wake at least once per millisecond so
      // stop() stays responsive.
      const std::int64_t waitNs = std::min<std::int64_t>(next->dueNs - now, 1000000);
      std::this_thread::sleep_for(std::chrono::nanoseconds(waitNs));
      continue;
    }
    outEvent e;
    if (queue.try_pop(e)) send(e);
  }
}

void YSE::MIDI::outSender::send(const outEvent& e) {
  if (SendHook h = hook.load(std::memory_order_acquire)) {
    h(e, hookUser.load(std::memory_order_acquire));
    return;
  }
  if (e.port == nullptr || e.len == 0) return;
  try {
    e.port->sendMessage(e.bytes, e.len);
  } catch (RtMidiError& error) {
    GenerateMidiError(error);
  }
}

#endif // YSE_ENABLE_MIDI_DEVICE
