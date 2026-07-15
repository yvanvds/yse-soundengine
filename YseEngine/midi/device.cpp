#include "headers/defines.hpp"
#if YSE_ENABLE_MIDI_DEVICE
#include "device.hpp"
#include "RtMidi.h"
#include "midiDeviceManager.h"
#include "synth/synthInterface.hpp"
#include "midi/midiSynthRouting.hpp"

YSE::midiOut::midiOut() : device(nullptr) {}

void YSE::midiOut::create(unsigned int port) {
  device = MIDI::DeviceManager().getMidiOutPort(port);
}

void YSE::midiOut::NoteOn(MIDI::M_CHANNEL channel, MIDI::M_PITCH pitch, unsigned char velocity) {
  if (isPrepared()) {
    unsigned char message[3];
    message[0] = 0x90 + channel;
    message[1] = pitch;
    message[2] = velocity;
    device->sendMessage(message, 3);
  }
}

void YSE::midiOut::NoteOn(MIDI::M_CHANNEL channel, unsigned char pitch, unsigned char velocity) {
  if (isPrepared()) {
    unsigned char message[3];
    message[0] = 0x90 + channel;
    message[1] = pitch;
    message[2] = velocity;
    device->sendMessage(message, 3);
  }
}

void YSE::midiOut::NoteOff(MIDI::M_CHANNEL channel, MIDI::M_PITCH pitch, unsigned char velocity) {
  if (isPrepared()) {
    unsigned char message[3];
    message[0] = 0x80 + channel;
    message[1] = pitch;
    message[2] = velocity;
    device->sendMessage(message, 3);
  }
}

void YSE::midiOut::NoteOff(MIDI::M_CHANNEL channel, unsigned char pitch, unsigned char velocity) {
  if (isPrepared()) {
    unsigned char message[3];
    message[0] = 0x80 + channel;
    message[1] = pitch;
    message[2] = velocity;
    device->sendMessage(message, 3);
  }
}

void YSE::midiOut::PolyPressure(MIDI::M_CHANNEL channel, MIDI::M_PITCH pitch,
                                unsigned char velocity) {
  if (isPrepared()) {
    unsigned char message[3];
    message[0] = 0xA0 + channel;
    message[1] = pitch;
    message[2] = velocity;
    device->sendMessage(message, 3);
  }
}

void YSE::midiOut::PolyPressure(MIDI::M_CHANNEL channel, unsigned char pitch,
                                unsigned char velocity) {
  if (isPrepared()) {
    unsigned char message[3];
    message[0] = 0xA0 + channel;
    message[1] = pitch;
    message[2] = velocity;
    device->sendMessage(message, 3);
  }
}

void YSE::midiOut::ChannelPressure(MIDI::M_CHANNEL channel, unsigned char value) {
  if (isPrepared()) {
    unsigned char message[3];
    message[0] = 0xD0 + channel;
    message[1] = value;
    message[2] = 0;
    device->sendMessage(message, 3);
  }
}

void YSE::midiOut::ProgramChange(MIDI::M_CHANNEL channel, unsigned char value) {
  if (isPrepared()) {
    unsigned char message[3];
    message[0] = 0xC0 + channel;
    message[1] = value;
    message[2] = 0;
    device->sendMessage(message, 3);
  }
}

void YSE::midiOut::ControlChange(MIDI::M_CHANNEL channel, unsigned char controller,
                                 unsigned char value) {
  if (isPrepared()) {
    unsigned char message[3];
    message[0] = 0xB0 + channel;
    message[1] = controller;
    message[2] = value;
    device->sendMessage(message, 3);
  }
}

void YSE::midiOut::AllNotesOff(MIDI::M_CHANNEL channel) {
  if (isPrepared()) {
    unsigned char message[3];
    message[0] = 0xB0 + channel;
    message[1] = 0x7B;
    message[2] = 0;
    device->sendMessage(message, 3);
  }
}

void YSE::midiOut::AllNotesOff() {
  if (isPrepared()) {
    unsigned char message[3];
    message[1] = 0x7B;
    message[2] = 0;

    for (unsigned char i = 0; i < 16; i++) {
      message[0] = 0xB0 + i;
      device->sendMessage(message, 3);
    }
  }
}

void YSE::midiOut::Reset(MIDI::M_CHANNEL channel) {
  if (isPrepared()) {
    unsigned char message[3];
    message[0] = 0xB0 + channel;
    message[1] = 0x79;
    message[2] = 0;
    device->sendMessage(message, 3);
  }
}

void YSE::midiOut::Reset() {
  if (isPrepared()) {
    unsigned char message[3];
    message[1] = 0x79;
    message[2] = 0;

    for (unsigned char i = 0; i < 16; i++) {
      message[0] = 0xB0 + i;
      device->sendMessage(message, 3);
    }
  }
}

void YSE::midiOut::LocalControl(bool on) {
  if (isPrepared()) {
    unsigned char message[3];
    message[0] = 0xB0;
    message[1] = 0x7A;
    message[2] = on ? 127 : 0;
    device->sendMessage(message, 3);
  }
}

void YSE::midiOut::Omni(bool on) {
  if (isPrepared()) {
    unsigned char message[3];
    message[0] = 0xB0;
    message[1] = on ? 0x7D : 0x7C;
    message[2] = 0;
    device->sendMessage(message, 3);
  }
}

void YSE::midiOut::Poly(bool on) {
  if (isPrepared()) {
    unsigned char message[3];
    message[0] = 0xB0;
    message[1] = on ? 0x7F : 0x7E;
    message[2] = 0;
    device->sendMessage(message, 3);
  }
}

void YSE::midiOut::Raw(unsigned char a, unsigned char b, unsigned char c) {
  if (isPrepared()) {
    unsigned char message[3];
    message[0] = a;
    message[1] = b;
    message[2] = c;
    device->sendMessage(message, 3);
  }
}

void YSE::midiOut::Raw(const std::string& value) {
  if (isPrepared()) {
    unsigned char message[3];
    message[0] = value.length() > 0 ? value[0] : 0;
    message[1] = value.length() > 1 ? value[1] : 0;
    message[2] = value.length() > 2 ? value[2] : 0;
    device->sendMessage(message, 3);
  }
}

bool YSE::midiOut::isPrepared() {
  return device != nullptr;
}

// ─── midiIn ──────────────────────────────────────────────────────────────────

YSE::midiIn::midiIn() : device(nullptr) {}

YSE::midiIn::~midiIn() {
  close();
}

void YSE::midiIn::create(unsigned int port) {
  // One RtMidiIn per midiIn instance: RtMidi keeps a single callback per
  // instance, and each midiIn carries its own subscribers — sharing a
  // single RtMidiIn across multiple midiIn objects would lose callbacks.
  close();

  try {
    auto* in = new RtMidiIn();
    in->openPort(port);
    in->setCallback(&rtMidiTrampoline, this);
    // Ignore active-sensing / system-realtime traffic by default — most
    // hosts don't want a torrent of 0xFE messages flooding their queue.
    // Note 4th arg is "ignoreActiveSense", 3rd is "ignoreSysEx",
    // 2nd is "ignoreTime" (timing clock). Match the historical
    // dispatch behaviour of pure host-side MIDI parsers.
    in->ignoreTypes(false, false, true);
    device = in;
  } catch (RtMidiError& error) {
    MIDI::GenerateMidiError(error);
    device = nullptr;
  }
}

void YSE::midiIn::close() {
  if (device == nullptr) return;
  try {
    device->cancelCallback();
    if (device->isPortOpen()) {
      device->closePort();
    }
  } catch (RtMidiError& error) {
    MIDI::GenerateMidiError(error);
  }
  delete device;
  device = nullptr;
}

bool YSE::midiIn::isOpen() const {
  return device != nullptr && device->isPortOpen();
}

void YSE::midiIn::setRawCallback(RawCallback cb, void* user_data) {
  rawUser.store(user_data, std::memory_order_release);
  rawCb.store(cb, std::memory_order_release);
}

void YSE::midiIn::setParsedCallback(ParsedCallback cb, void* user_data) {
  parsedUser.store(user_data, std::memory_order_release);
  parsedCb.store(cb, std::memory_order_release);
}

void YSE::midiIn::connect(YSE::synth& synth, int channelFilter) {
  // Already connected? Just refresh its channel filter.
  for (auto& sub : synthSubs) {
    if (sub.synth.load(std::memory_order_acquire) == &synth) {
      sub.channel.store(channelFilter, std::memory_order_relaxed);
      return;
    }
  }
  // Claim a free slot. The CAS publishes the pointer with release ordering so
  // the RtMidi thread's acquire-load in dispatch() sees a valid synth.
  for (auto& sub : synthSubs) {
    SYNTH::interfaceObject* expected = nullptr;
    if (sub.synth.compare_exchange_strong(expected, &synth, std::memory_order_acq_rel)) {
      sub.channel.store(channelFilter, std::memory_order_relaxed);
      return;
    }
  }
  // Table full (>kMaxSynthSubs synths on one port) — a rare misconfiguration;
  // ignore rather than grow a table the RtMidi thread reads lock-free.
}

void YSE::midiIn::disconnect(YSE::synth& synth) {
  for (auto& sub : synthSubs) {
    if (sub.synth.load(std::memory_order_acquire) == &synth) {
      sub.channel.store(0, std::memory_order_relaxed); // reset filter for reuse
      sub.synth.store(nullptr, std::memory_order_release);
      return;
    }
  }
}

void YSE::midiIn::rtMidiTrampoline(double ts, std::vector<unsigned char>* msg, void* userData) {
  if (msg == nullptr || msg->empty() || userData == nullptr) return;
  auto* self = static_cast<midiIn*>(userData);
  self->dispatch(ts, msg->data(), msg->size());
}

void YSE::midiIn::dispatch(double timestampSec, const unsigned char* bytes, std::size_t len) {
  // Zero-length payloads can't be interpreted at any level; the trampoline
  // already short-circuits these but guard defensively in case dispatch()
  // is reached via another path.
  if (bytes == nullptr || len == 0) return;

  // Raw subscriber sees the byte pointer as-is; ownership stays with the
  // RtMidi vector that survives the call. Consumers that need to forward
  // across threads must copy (the C API bridge already does).
  if (auto rcb = rawCb.load(std::memory_order_acquire)) {
    rcb(timestampSec, bytes, len, rawUser.load(std::memory_order_acquire));
  }

  // Parsed subscriber wants the typed nibbles. For 1- and 2-byte messages
  // (real-time clock, channel pressure, program change) the missing data
  // bytes are reported as zero.
  if (auto pcb = parsedCb.load(std::memory_order_acquire)) {
    const unsigned char status = static_cast<unsigned char>(bytes[0] & 0xF0);
    const unsigned char channel = static_cast<unsigned char>(bytes[0] & 0x0F);
    const unsigned char data1 = len > 1 ? bytes[1] : 0;
    const unsigned char data2 = len > 2 ? bytes[2] : 0;
    pcb(timestampSec, status, channel, data1, data2, parsedUser.load(std::memory_order_acquire));
  }

  // Internal synth subscribers (issue #155). This is the additive fan-out the
  // port hub was left open for: route every channel-voice message to each
  // connected synth whose channel filter matches. Each routed call maps raw
  // MIDI onto the synth's normalized note API and lock-free try_pushes onto its
  // inbox — safe to run here on RtMidi's input thread.
  const unsigned char statusNibble = static_cast<unsigned char>(bytes[0] & 0xF0);
  if (MIDI::isChannelVoiceStatus(statusNibble)) {
    const unsigned char channelNibble = static_cast<unsigned char>(bytes[0] & 0x0F);
    const int synthChannel = static_cast<int>(channelNibble) + 1;
    const unsigned char d1 = len > 1 ? bytes[1] : 0;
    const unsigned char d2 = len > 2 ? bytes[2] : 0;
    for (auto& sub : synthSubs) {
      SYNTH::interfaceObject* s = sub.synth.load(std::memory_order_acquire);
      if (s == nullptr) continue;
      const int filter = sub.channel.load(std::memory_order_relaxed);
      if (filter == 0 || filter == synthChannel)
        MIDI::routeChannelVoiceMessage(*s, statusNibble, channelNibble, d1, d2);
    }
  }
}

#endif