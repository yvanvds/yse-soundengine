// Unit tests for the raw-MIDI -> YSE::synth mapping (issue #155).
//
// routeChannelVoiceMessage() is the single place both MIDI input paths (device
// input and file playback) convert a raw channel-voice MIDI message into the
// synth's normalized note API. These tests pin the mapping precisely — channel
// offset, 7-bit / 14-bit normalization, velocity-0 note-off — against a
// recording sink, with no engine or audio device needed.

#include <doctest/doctest.h>
#include <vector>
#include "midi/midiSynthRouting.hpp"

namespace {

  // Records every synth call the router makes, with its arguments, so a test
  // can assert the exact mapping. Mirrors the public YSE::synth note API the
  // router targets.
  struct RecordingSynth {
    enum Kind { NoteOn, NoteOff, Controller, PitchWheel, Aftertouch };
    struct Call {
      Kind kind;
      int channel;
      int arg; // note number / controller number (unused for pitch wheel)
      float value; // velocity / cc value / bend / pressure
    };
    std::vector<Call> calls;

    void noteOn(int ch, int note, float vel) {
      calls.push_back({NoteOn, ch, note, vel});
    }
    void noteOff(int ch, int note, float vel) {
      calls.push_back({NoteOff, ch, note, vel});
    }
    void controller(int ch, int num, float val) {
      calls.push_back({Controller, ch, num, val});
    }
    void pitchWheel(int ch, float val) {
      calls.push_back({PitchWheel, ch, 0, val});
    }
    void aftertouch(int ch, int note, float val) {
      calls.push_back({Aftertouch, ch, note, val});
    }
  };

  using YSE::MIDI::routeChannelVoiceMessage;

} // namespace

TEST_SUITE("midi") {

  // ─── note on / off ────────────────────────────────────────────────────────

  TEST_CASE("routing: note-on maps channel(+1), note and normalized velocity") {
    RecordingSynth s;
    routeChannelVoiceMessage(s, 0x90, 0x00, 60, 127); // ch nibble 0, C4, vel 127
    REQUIRE(s.calls.size() == 1);
    CHECK(s.calls[0].kind == RecordingSynth::NoteOn);
    CHECK(s.calls[0].channel == 1); // MIDI channel 0 -> synth channel 1
    CHECK(s.calls[0].arg == 60);
    CHECK(s.calls[0].value == doctest::Approx(1.0f)); // 127/127
  }

  TEST_CASE("routing: note-on with velocity 0 is routed as a note-off") {
    RecordingSynth s;
    routeChannelVoiceMessage(s, 0x90, 0x03, 64, 0);
    REQUIRE(s.calls.size() == 1);
    CHECK(s.calls[0].kind == RecordingSynth::NoteOff);
    CHECK(s.calls[0].channel == 4);
    CHECK(s.calls[0].arg == 64);
  }

  TEST_CASE("routing: note-off maps channel, note and normalized release velocity") {
    RecordingSynth s;
    routeChannelVoiceMessage(s, 0x80, 0x05, 48, 64); // ch nibble 5
    REQUIRE(s.calls.size() == 1);
    CHECK(s.calls[0].kind == RecordingSynth::NoteOff);
    CHECK(s.calls[0].channel == 6);
    CHECK(s.calls[0].arg == 48);
    CHECK(s.calls[0].value == doctest::Approx(64.f / 127.f));
  }

  TEST_CASE("routing: channel nibble 15 maps to synth channel 16") {
    RecordingSynth s;
    routeChannelVoiceMessage(s, 0x90, 0x0F, 60, 100);
    REQUIRE(s.calls.size() == 1);
    CHECK(s.calls[0].channel == 16);
  }

  // ─── control change ───────────────────────────────────────────────────────

  TEST_CASE("routing: control change maps controller number and normalized value") {
    RecordingSynth s;
    routeChannelVoiceMessage(s, 0xB0, 0x00, 7, 64); // CC 7 (volume) = 64
    REQUIRE(s.calls.size() == 1);
    CHECK(s.calls[0].kind == RecordingSynth::Controller);
    CHECK(s.calls[0].channel == 1);
    CHECK(s.calls[0].arg == 7);
    CHECK(s.calls[0].value == doctest::Approx(64.f / 127.f));
  }

  TEST_CASE("routing: sustain pedal (CC 64) is forwarded as a controller") {
    // The synth itself intercepts CC 64/66/67 as the pedals, so the router
    // just forwards every CC unchanged.
    RecordingSynth s;
    routeChannelVoiceMessage(s, 0xB0, 0x00, 64, 127);
    REQUIRE(s.calls.size() == 1);
    CHECK(s.calls[0].kind == RecordingSynth::Controller);
    CHECK(s.calls[0].arg == 64);
    CHECK(s.calls[0].value == doctest::Approx(1.0f));
  }

  // ─── pitch bend (14-bit) ──────────────────────────────────────────────────

  TEST_CASE("routing: pitch bend centre (8192) maps to 0") {
    RecordingSynth s;
    routeChannelVoiceMessage(s, 0xE0, 0x00, 0x00, 0x40); // LSB 0, MSB 64 -> 8192
    REQUIRE(s.calls.size() == 1);
    CHECK(s.calls[0].kind == RecordingSynth::PitchWheel);
    CHECK(s.calls[0].value == doctest::Approx(0.0f));
  }

  TEST_CASE("routing: pitch bend minimum maps to -1") {
    RecordingSynth s;
    routeChannelVoiceMessage(s, 0xE0, 0x02, 0x00, 0x00); // raw 0
    REQUIRE(s.calls.size() == 1);
    CHECK(s.calls[0].channel == 3);
    CHECK(s.calls[0].value == doctest::Approx(-1.0f));
  }

  TEST_CASE("routing: pitch bend maximum maps to nearly +1") {
    RecordingSynth s;
    routeChannelVoiceMessage(s, 0xE0, 0x00, 0x7F, 0x7F); // raw 16383
    REQUIRE(s.calls.size() == 1);
    CHECK(s.calls[0].value == doctest::Approx(8191.f / 8192.f));
  }

  // ─── aftertouch ───────────────────────────────────────────────────────────

  TEST_CASE("routing: polyphonic aftertouch maps to the per-note pressure") {
    RecordingSynth s;
    routeChannelVoiceMessage(s, 0xA0, 0x02, 60, 100); // note 60, pressure 100
    REQUIRE(s.calls.size() == 1);
    CHECK(s.calls[0].kind == RecordingSynth::Aftertouch);
    CHECK(s.calls[0].channel == 3);
    CHECK(s.calls[0].arg == 60);
    CHECK(s.calls[0].value == doctest::Approx(100.f / 127.f));
  }

  TEST_CASE("routing: channel aftertouch broadcasts (note == -1)") {
    RecordingSynth s;
    routeChannelVoiceMessage(s, 0xD0, 0x02, 100, 0); // pressure in data1
    REQUIRE(s.calls.size() == 1);
    CHECK(s.calls[0].kind == RecordingSynth::Aftertouch);
    CHECK(s.calls[0].channel == 3);
    CHECK(s.calls[0].arg == -1);
    CHECK(s.calls[0].value == doctest::Approx(100.f / 127.f));
  }

  // ─── unrouted messages ────────────────────────────────────────────────────

  TEST_CASE("routing: program change is not routed to the synth core") {
    RecordingSynth s;
    routeChannelVoiceMessage(s, 0xC0, 0x00, 5, 0);
    CHECK(s.calls.empty());
  }

  TEST_CASE("routing: isChannelVoiceStatus accepts 0x80..0xE0 only") {
    CHECK(YSE::MIDI::isChannelVoiceStatus(0x80));
    CHECK(YSE::MIDI::isChannelVoiceStatus(0x90));
    CHECK(YSE::MIDI::isChannelVoiceStatus(0xE0));
    CHECK_FALSE(YSE::MIDI::isChannelVoiceStatus(0x70));
    CHECK_FALSE(YSE::MIDI::isChannelVoiceStatus(0xF0)); // system / meta
  }

} // TEST_SUITE("midi")
