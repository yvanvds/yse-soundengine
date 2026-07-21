// Tests for YSE::synth named-bus addressing (issue #388, following the
// sound/channel pattern of #123).
//
// A named synth subscribes to "synth.<name>.note", ".off", ".cc", ".bend",
// ".aftertouch" and ".alloff" on the global named bus; the subscribers reuse
// the RT-safe message inbox the C++ setters already use. Delivery is observed
// through per-note position handlers: a staticHandler pins every sounding
// voice to a known position (so getVoicePosition() != origin <=> the note
// arrived), and a probe handler echoes the live cc / bend / aftertouch values
// into a position for exact-value assertions.
//
// ISOLATION: like `synthhandlers` these drive System::initOffline()/close()
// and so run in their own ctest process (`synthbus`), excluded from the
// combined `yse_unit_tests` run. initOffline() needs no audio hardware, so
// they run in CI; a host where it returns false bails the case out as a pass.

#include <doctest/doctest.h>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "yse.hpp"
#include "internal/namedBus.h"
#include "sound/soundInterface.hpp"
#include "synth/positionHandler.hpp"
#include "synth/positionHandlers.hpp"
#include "synth/sineVoice.hpp"
#include "synth/synthInterface.hpp"

// The C-API mirror case (yse_synth_set_name) lives here rather than in the
// flat-C `synthcapi` suite because verifying it needs the engine-internal
// Bus() publisher, which that suite deliberately never includes.
#include "yse_c/yse_sound.h"
#include "yse_c/yse_synth.h"

namespace {

  // Bring a synth (behind a sound) to READY by pumping the offline engine until
  // its voices are cloned on the slow pool, or a deadline passes.
  bool bringToReady(YSE::synth& syn, int expectVoices) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
      YSE::System().update();
      YSE::System().renderOffline(1);
      if (syn.getNumVoices() == expectVoices) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return syn.getNumVoices() == expectVoices;
  }

  // Advance the offline engine by n blocks.
  void pump(int n) {
    for (int b = 0; b < n; ++b) {
      YSE::System().update();
      YSE::System().renderOffline(1);
    }
  }

  // Publish a list[float] to `address` on T_GUI (the test thread ran
  // initOffline(), so it is the control thread and dispatch is synchronous).
  void publishList(const std::string& address, std::vector<float> values) {
    YSE::INTERNAL::Bus().publish(address, YSE::INTERNAL::BusValue{std::move(values)}, YSE::T_GUI);
  }

  // Give a sine prototype a short envelope (fast attack, short release so
  // note-off tests reach silence quickly). The voice's copy constructor is
  // protected (cloning is the engine's job), so configure in place.
  void configureQuick(YSE::SYNTH::sineVoice& v) {
    v.attack(0.001f).decay(0.001f).sustain(1.f).release(0.01f);
  }

  // Where the staticHandler pins every sounding voice. Off-origin, so
  // getVoicePosition() == kSoundingPos <=> a voice sounds for that
  // (channel, note) and == origin <=> none does.
  const YSE::Pos kSoundingPos(2.f, 0.f, 0.f);

  // True once no voice sounds (channel, note) any more — pumps the engine
  // until the release tail ends or the deadline passes.
  bool becomesSilent(YSE::synth& syn, int channel, int note) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
      pump(1);
      if (syn.getVoicePosition(channel, note) == YSE::Pos(0.f)) return true;
    }
    return false;
  }

  // Echoes the live keyboard state a voice sees into its position, so a test
  // can assert exactly which bus-published value reached the synth:
  // x <- CC1 * 10, y <- pitch wheel * 4, z <- aftertouch * 4.
  class KeyboardProbeHandler : public YSE::SYNTH::positionHandler {
  public:
    YSE::SYNTH::positionHandler* clone() override {
      return new KeyboardProbeHandler(*this);
    }
    YSE::Pos noteOn(const YSE::SYNTH::voiceContext& ctx) override {
      return probe(ctx);
    }
    YSE::Pos update(const YSE::SYNTH::voiceContext& ctx, Flt) override {
      return probe(ctx);
    }

  private:
    static YSE::Pos probe(const YSE::SYNTH::voiceContext& ctx) {
      return YSE::Pos(ctx.controller(1) * 10.f, ctx.pitchWheel * 4.f, ctx.aftertouch * 4.f);
    }
  };

} // namespace

TEST_SUITE("synthbus") {

  TEST_CASE("synth bus: note publish reaches the synth, channel and note parsed") {
    YSE::System().close();
    if (!YSE::System().initOffline()) return;

    YSE::SYNTH::sineVoice proto;
    configureQuick(proto);
    YSE::SYNTH::staticHandler pin;
    pin.position(kSoundingPos);
    {
      YSE::synth syn;
      syn.create().addVoices(proto, 8).positionHandler(pin);
      syn.name("bells");
      YSE::sound snd;
      snd.create(syn);
      REQUIRE(bringToReady(syn, 8));
      snd.play();

      publishList("synth.bells.note", {1.f, 60.f, 0.9f});
      pump(2);
      CHECK(syn.getVoicePosition(1, 60) == kSoundingPos);

      // The channel element is parsed, not assumed: a note published on
      // channel 3 sounds under (3, 64), not (1, 64).
      publishList("synth.bells.note", {3.f, 64.f, 0.9f});
      pump(2);
      CHECK(syn.getVoicePosition(3, 64) == kSoundingPos);
      CHECK(syn.getVoicePosition(1, 64) == YSE::Pos(0.f));

      syn.allNotesOff();
      pump(8);
    }
    pump(4); // let the release/delete jobs run before the next case
  }

  TEST_CASE("synth bus: off releases a note; alloff releases all (int and bang)") {
    YSE::System().close();
    if (!YSE::System().initOffline()) return;

    YSE::SYNTH::sineVoice proto;
    configureQuick(proto);
    YSE::SYNTH::staticHandler pin;
    pin.position(kSoundingPos);
    {
      YSE::synth syn;
      syn.create().addVoices(proto, 8).positionHandler(pin);
      syn.name("cutshort");
      YSE::sound snd;
      snd.create(syn);
      REQUIRE(bringToReady(syn, 8));
      snd.play();

      // note on, then bus note-off (2-element shape, velocity defaults to 0).
      publishList("synth.cutshort.note", {1.f, 60.f, 0.9f});
      pump(2);
      REQUIRE(syn.getVoicePosition(1, 60) == kSoundingPos);
      publishList("synth.cutshort.off", {1.f, 60.f});
      CHECK(becomesSilent(syn, 1, 60));

      // alloff as an int channel (0 = all channels).
      publishList("synth.cutshort.note", {1.f, 62.f, 0.9f});
      publishList("synth.cutshort.note", {2.f, 64.f, 0.9f});
      pump(2);
      REQUIRE(syn.getVoicePosition(1, 62) == kSoundingPos);
      REQUIRE(syn.getVoicePosition(2, 64) == kSoundingPos);
      YSE::INTERNAL::Bus().publish("synth.cutshort.alloff", YSE::INTERNAL::BusValue{0}, YSE::T_GUI);
      CHECK(becomesSilent(syn, 1, 62));
      CHECK(becomesSilent(syn, 2, 64));

      // alloff as a bang (monostate — what a patcher gSend publishes).
      publishList("synth.cutshort.note", {1.f, 65.f, 0.9f});
      pump(2);
      REQUIRE(syn.getVoicePosition(1, 65) == kSoundingPos);
      YSE::INTERNAL::Bus().publish("synth.cutshort.alloff", YSE::INTERNAL::BusValue{}, YSE::T_GUI);
      CHECK(becomesSilent(syn, 1, 65));
    }
    pump(4);
  }

  TEST_CASE("synth bus: cc, bend and aftertouch reach the keyboard state") {
    YSE::System().close();
    if (!YSE::System().initOffline()) return;

    YSE::SYNTH::sineVoice proto;
    configureQuick(proto);
    KeyboardProbeHandler probe;
    {
      YSE::synth syn;
      syn.create().addVoices(proto, 4).positionHandler(probe);
      syn.name("expressive");
      YSE::sound snd;
      snd.create(syn);
      REQUIRE(bringToReady(syn, 4));
      snd.play();

      publishList("synth.expressive.note", {1.f, 60.f, 0.9f});
      pump(2);

      publishList("synth.expressive.cc", {1.f, 1.f, 0.5f});
      publishList("synth.expressive.bend", {1.f, 0.5f});
      publishList("synth.expressive.aftertouch", {1.f, 60.f, 0.5f});
      pump(2);

      YSE::Pos p = syn.getVoicePosition(1, 60);
      CHECK(p.x == doctest::Approx(5.0f)); // CC1 = 0.5 -> x = 5
      CHECK(p.y == doctest::Approx(2.0f)); // bend = 0.5 -> y = 2
      CHECK(p.z == doctest::Approx(2.0f)); // aftertouch = 0.5 -> z = 2

      syn.allNotesOff();
      pump(8);
    }
    pump(4);
  }

  TEST_CASE("synth bus: wrong payload shapes are ignored, anonymous synths unreachable") {
    YSE::System().close();
    if (!YSE::System().initOffline()) return;

    YSE::SYNTH::sineVoice proto;
    configureQuick(proto);
    YSE::SYNTH::staticHandler pin;
    pin.position(kSoundingPos);
    {
      YSE::synth named, anon;
      named.create().addVoices(proto, 4).positionHandler(pin);
      named.name("strict");
      anon.create().addVoices(proto, 4).positionHandler(pin);
      YSE::sound sndNamed, sndAnon;
      sndNamed.create(named);
      sndAnon.create(anon);
      REQUIRE(bringToReady(named, 4));
      REQUIRE(bringToReady(anon, 4));
      sndNamed.play();
      sndAnon.play();

      // Wrong arity / wrong type on every address: all ignored, no crash.
      publishList("synth.strict.note", {60.f, 0.9f}); // missing channel
      YSE::INTERNAL::Bus().publish("synth.strict.note", YSE::INTERNAL::BusValue{60.f}, YSE::T_GUI);
      publishList("synth.strict.off", {60.f});
      publishList("synth.strict.cc", {1.f, 1.f});
      publishList("synth.strict.bend", {0.5f});
      publishList("synth.strict.aftertouch", {1.f, 60.f});
      pump(2);
      CHECK(named.getVoicePosition(1, 60) == YSE::Pos(0.f));

      // The anonymous synth never subscribed; a publish reaches no one.
      publishList("synth..note", {1.f, 60.f, 0.9f});
      pump(2);
      CHECK(anon.getVoicePosition(1, 60) == YSE::Pos(0.f));
    }
    pump(4);
  }

  TEST_CASE("synth bus: duplicate name rejected, rename re-subscribes, \"\" clears") {
    YSE::System().close();
    if (!YSE::System().initOffline()) return;

    YSE::SYNTH::sineVoice proto;
    configureQuick(proto);
    YSE::SYNTH::staticHandler pin;
    pin.position(kSoundingPos);
    {
      YSE::synth a, b;
      a.create().addVoices(proto, 4).positionHandler(pin);
      b.create().addVoices(proto, 4).positionHandler(pin);
      YSE::sound sndA, sndB;
      sndA.create(a);
      sndB.create(b);
      REQUIRE(bringToReady(a, 4));
      REQUIRE(bringToReady(b, 4));
      sndA.play();
      sndB.play();

      // Duplicate: b's registration is rejected and logged; a keeps ownership.
      a.name("dup");
      b.name("dup");
      publishList("synth.dup.note", {1.f, 60.f, 0.9f});
      pump(2);
      CHECK(a.getVoicePosition(1, 60) == kSoundingPos);
      CHECK(b.getVoicePosition(1, 60) == YSE::Pos(0.f));
      a.allNotesOff();
      REQUIRE(becomesSilent(a, 1, 60));

      // Renaming drops the old address and claims the new one.
      a.name("renamed");
      publishList("synth.dup.note", {1.f, 62.f, 0.9f}); // old address is dead
      pump(2);
      CHECK(a.getVoicePosition(1, 62) == YSE::Pos(0.f));
      publishList("synth.renamed.note", {1.f, 64.f, 0.9f});
      pump(2);
      CHECK(a.getVoicePosition(1, 64) == kSoundingPos);
      a.allNotesOff();
      REQUIRE(becomesSilent(a, 1, 64));

      // "" makes the synth anonymous again and frees the name for others.
      a.name("");
      publishList("synth.renamed.note", {1.f, 65.f, 0.9f});
      pump(2);
      CHECK(a.getVoicePosition(1, 65) == YSE::Pos(0.f));
      b.name("renamed"); // now claimable: b was never registered under "dup"
      publishList("synth.renamed.note", {1.f, 67.f, 0.9f});
      pump(2);
      CHECK(b.getVoicePosition(1, 67) == kSoundingPos);
      b.allNotesOff();
      pump(8);
    }
    pump(4);
  }

  TEST_CASE("synth bus: destroying a named synth unsubscribes and frees the name") {
    YSE::System().close();
    if (!YSE::System().initOffline()) return;

    YSE::SYNTH::sineVoice proto;
    configureQuick(proto);
    YSE::SYNTH::staticHandler pin;
    pin.position(kSoundingPos);
    {
      YSE::synth syn;
      syn.create().addVoices(proto, 4).positionHandler(pin);
      syn.name("gone");
      YSE::sound snd;
      snd.create(syn);
      REQUIRE(bringToReady(syn, 4));
      snd.play();
      publishList("synth.gone.note", {1.f, 60.f, 0.9f});
      pump(2);
      CHECK(syn.getVoicePosition(1, 60) == kSoundingPos);
      syn.allNotesOff();
      pump(8);
    } // ~sound then ~synth: unsubscribes and releases the name here
    pump(4);

    // Publishing to the dead address must not crash and must reach no one.
    publishList("synth.gone.note", {1.f, 60.f, 0.9f});
    pump(2);

    // The name is free again: a fresh synth can claim it and receive events.
    {
      YSE::synth again;
      again.create().addVoices(proto, 4).positionHandler(pin);
      again.name("gone");
      YSE::sound snd;
      snd.create(again);
      REQUIRE(bringToReady(again, 4));
      snd.play();
      publishList("synth.gone.note", {1.f, 62.f, 0.9f});
      pump(2);
      CHECK(again.getVoicePosition(1, 62) == kSoundingPos);
      again.allNotesOff();
      pump(8);
    }
    pump(4);
  }

  TEST_CASE("synth bus: the C API naming setter registers the same addresses") {
    YSE::System().close();
    if (!YSE::System().initOffline()) return;

    YseSynth* syn = yse_synth_create();
    REQUIRE(syn != nullptr);
    REQUIRE(yse_synth_add_voices_sine(syn, 4, 0, 0, 127, 0.001f, 0.001f, 1.0f, 0.01f) == YSE_OK);
    YseSynthPositionParams params{};
    params.static_x = kSoundingPos.x;
    params.static_y = kSoundingPos.y;
    params.static_z = kSoundingPos.z;
    REQUIRE(yse_synth_set_position_handler(syn, YSE_POSITION_HANDLER_STATIC, &params) == YSE_OK);

    YseSound* snd = yse_sound_create();
    REQUIRE(snd != nullptr);
    REQUIRE(yse_synth_attach_to_sound(syn, snd, nullptr, 1.0f) == YSE_OK);
    yse_sound_play(snd);

    // Pump until the setup pool has cloned the pool (drainUntilVoices in the
    // synthcapi suite, but driven through the C++ System() like this suite).
    {
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
      while (std::chrono::steady_clock::now() < deadline && yse_synth_get_num_voices(syn) < 4) {
        pump(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
    }
    REQUIRE(yse_synth_get_num_voices(syn) == 4);

    // Naming over the C ABI registers the same synth.<name>.* addresses.
    yse_synth_set_name(syn, "ffi");
    publishList("synth.ffi.note", {1.f, 60.f, 0.9f});
    pump(2);
    float x = 0.f, y = 0.f, z = 0.f;
    yse_synth_get_voice_position(syn, 1, 60, &x, &y, &z);
    CHECK(x == doctest::Approx(kSoundingPos.x));
    yse_synth_all_notes_off(syn, 0);
    pump(8);

    // NULL clears the name like "" does: the address goes dead.
    yse_synth_set_name(syn, nullptr);
    publishList("synth.ffi.note", {1.f, 62.f, 0.9f});
    pump(2);
    yse_synth_get_voice_position(syn, 1, 62, &x, &y, &z);
    CHECK(x == doctest::Approx(0.f));

    // Null-handle no-op (void C-API convention).
    yse_synth_set_name(nullptr, "ffi");

    yse_sound_destroy(snd); // sound must go before the synth it renders
    yse_synth_destroy(syn);
    // Let the delete jobs free the impls before the next case's close().
    {
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
      while (std::chrono::steady_clock::now() < deadline) {
        pump(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
    }
  }

  TEST_CASE("synth bus: naming while the engine is down is a safe no-op") {
    YSE::System().close();
    {
      YSE::synth syn;
      syn.name("earlybird"); // bus is down: silent no-op, no crash
    } // destructor with no live registration: also a no-op
    CHECK(true);
  }

} // TEST_SUITE("synthbus")
