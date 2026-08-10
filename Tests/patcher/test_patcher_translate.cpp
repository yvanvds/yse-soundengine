// Tests for `.translate` — the patcher's converter between time formats
// (issue #516).
//
// The object exists because the patcher speaks two kinds of time and cannot mix
// them: milliseconds (`.delay 500`) and a named domain clock's beats
// (`.timepoint main 4`). So the cases below are organised around the two
// families and the one thing that bridges them:
//
//   - conversions *within* a family need no clock at all, and must not go quiet
//     for the want of one;
//   - conversions *across* the families read the tempo from the bound clock **at
//     the moment they convert**, which is #516's own requirement — a cached
//     tempo would be silently wrong the instant a `.setclock` ramped, and the
//     `tracks a tempo ramp` case below is written so that a cached one fails it;
//   - with no exchange rate — no clock, or a clock at tempo 0 — nothing at all
//     is emitted, `.when`'s and `.setclock`'s rule.
//
// Registered in the `clock` suite, not `patcher`: the cases drive
// CLOCK::Manager().update() directly on the test thread to advance beats
// deterministically, which would race a live audio thread from another suite in
// the shared monolithic process. No audio device required.

#include <doctest/doctest.h>
#include <cstdint>
#include <string>
#include <vector>

#include "clock/clockManager.h"
#include "headers/constants.hpp"
#include "patcher/inlet.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/time/clockBridge.h"
#include "patcher/time/gTranslate.h"
#include "patcher/time/messageScheduler.h"
#include "support/alloc_probe.hpp"

namespace {

  using YSE::PATCHER::gTranslate;
  using YSE::PATCHER::patcherImplementation;
  using Format = YSE::PATCHER::gTranslate::Format;

  // `.transport`'s, `.when`'s and `.setclock`'s constants, so the four files'
  // numbers line up side by side. 0.25 s per tick at 120 BPM is exactly half a
  // beat, and every number in that sentence is exact in binary.
  constexpr float kTickSeconds = 0.25f;
  constexpr float kTempo = 120.f;
  constexpr float kFastTempo = 240.f;

  // One beat at 120 BPM in milliseconds, and Max's ticks per beat. Both exact.
  constexpr float kBeatMs = 500.f;
  constexpr float kTicksPerBeat = 480.f;

  // One block of the whole world, in the engine's own order: advance every
  // domain clock, then let the patcher render.
  void Tick(patcherImplementation& p) {
    YSE::CLOCK::Manager().update(kTickSeconds);
    p.Calculate(YSE::T_DSP);
  }

  // Records every float it is sent, in order.
  struct FloatRecorder : YSE::PATCHER::pObject {
    std::vector<float> seen;

    FloatRecorder() : pObject(false) {
      // Reserved up front so the allocation-probe case measures the *object's*
      // handlers rather than this vector growing under them.
      seen.reserve(64);
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterFloat([this](float v, int, YSE::THREAD) { seen.push_back(v); });
    }
    const char* Type() const override {
      return "translate_recorder";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // Counts bangs. The end-to-end case wires a `.delay` into one of these.
  struct BangCounter : YSE::PATCHER::pObject {
    int bangs = 0;

    BangCounter() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) { bangs++; });
    }
    const char* Type() const override {
      return "translate_bangs";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // A `.translate` living in a real patcher, with a recorder on its outlet.
  struct TranslateRig {
    patcherImplementation patcher{1, nullptr};
    FloatRecorder out;
    YSE::pHandle outHandle{&out};
    YSE::pHandle* obj = nullptr;

    explicit TranslateRig(const std::string& args) {
      obj = patcher.CreateObject(YSE::OBJ::G_TRANSLATE, args);
      REQUIRE(obj != nullptr);
      patcher.Connect(obj, 0, &outHandle, 0);
      // Makes the resolve attempt deterministic.
      patcher.Clocks()->WaitIdle();
    }

    void Bang() {
      obj->SetBang(0);
    }
    void Int(int value) {
      obj->SetIntData(0, value);
    }
    void Float(float value) {
      obj->SetFloatData(0, value);
    }
    void Send(const std::string& message) {
      obj->SetListData(0, message);
    }
    void Tick() {
      ::Tick(patcher);
    }

    // The one float the last input produced, taken and cleared. Fails the case
    // when the object emitted nothing or emitted more than once — which is what
    // most of the assertions below are about.
    float Only() {
      REQUIRE(out.seen.size() == 1);
      const float value = out.seen[0];
      out.seen.clear();
      return value;
    }
    bool Silent() const {
      return out.seen.empty();
    }
    void Clear() {
      out.seen.clear();
    }
  };

  // Drop a clock and let the manager retire it, so a case's name is free again
  // for the next run of the process.
  void DropClock(const std::string& name) {
    YSE::CLOCK::Manager().destroyClock(name);
    YSE::CLOCK::Manager().update(0.01f);
  }

} // namespace

TEST_SUITE("clock") {

  // ─── the object exists ──────────────────────────────────────────────────────

  TEST_CASE("translate: the object is registered and creatable (#516)") {
    bool found = false;
    for (const std::string& name : YSE::PATCHER::Register().AllNames()) {
      if (name == YSE::OBJ::G_TRANSLATE) found = true;
    }
    CHECK(found);

    patcherImplementation patcher(1, nullptr);
    YSE::pHandle* obj = patcher.CreateObject(YSE::OBJ::G_TRANSLATE, "");
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == ".translate");
    // Max's shape: one inlet that takes everything, one outlet.
    CHECK(obj->GetInputs() == 1);
    CHECK(obj->GetOutputs() == 1);

    gTranslate standalone;
    CHECK(standalone.GetCategory() == YSE::PATCHER::pCategory::TIME);
    // The defaults. Max's input default is bars.beats.units, which needs a
    // meter a domain clock does not have, so milliseconds takes its place —
    // what a bare number already means everywhere else in the patcher. The
    // output default is Max's own.
    CHECK(standalone.InFormat() == Format::MS);
    CHECK(standalone.OutFormat() == Format::TICKS);
    CHECK(std::string(standalone.ClockName()).empty());
    CHECK_FALSE(standalone.Bound());
    CHECK_FALSE(standalone.HasValue());

    // The vocabulary, pinned. Five words and only those, matched whole so that
    // a clock called `ms.driver` is not read as a format.
    Format parsed = Format::HZ;
    CHECK(gTranslate::ReadFormat("ms", 2, parsed));
    CHECK(parsed == Format::MS);
    CHECK(gTranslate::ReadFormat("beats", 5, parsed));
    CHECK(parsed == Format::BEATS);
    CHECK(gTranslate::ReadFormat("ticks", 5, parsed));
    CHECK(parsed == Format::TICKS);
    CHECK(gTranslate::ReadFormat("hz", 2, parsed));
    CHECK(parsed == Format::HZ);
    CHECK(gTranslate::ReadFormat("samples", 7, parsed));
    CHECK(parsed == Format::SAMPLES);
    CHECK_FALSE(gTranslate::ReadFormat("ms.driver", 9, parsed));
    // Max's three this engine cannot honour.
    CHECK_FALSE(gTranslate::ReadFormat("bars.beats.units", 16, parsed));
    CHECK_FALSE(gTranslate::ReadFormat("hh:mm:ss", 8, parsed));
    CHECK_FALSE(gTranslate::ReadFormat("notevalues", 10, parsed));

    // The family split the tempo bridges.
    CHECK_FALSE(gTranslate::IsRelative(Format::MS));
    CHECK_FALSE(gTranslate::IsRelative(Format::HZ));
    CHECK_FALSE(gTranslate::IsRelative(Format::SAMPLES));
    CHECK(gTranslate::IsRelative(Format::BEATS));
    CHECK(gTranslate::IsRelative(Format::TICKS));
  }

  // ─── inside one family: no clock needed ─────────────────────────────────────

  TEST_CASE("translate: conversions inside one family need no clock at all (#516)") {
    // The half of the object that has nothing to do with tempo. A patch with no
    // clock anywhere still gets milliseconds to samples and beats to ticks, and
    // the object must not go quiet for the want of a clock it does not need.
    const float rate = (float)YSE::SAMPLERATE;
    REQUIRE(rate > 0.f);

    {
      TranslateRig rig("ms samples");
      CHECK(rig.patcher.Clocks()->BoundCount() == 0);
      rig.Float(1000.f);
      CHECK(rig.Only() == doctest::Approx(rate));
      rig.Int(500);
      CHECK(rig.Only() == doctest::Approx(rate / 2.f));
    }
    {
      TranslateRig rig("samples ms");
      rig.Float(rate);
      CHECK(rig.Only() == doctest::Approx(1000.f));
    }
    {
      // Max's `hz` time value is a repetition rate: 2 Hz is 500 ms.
      TranslateRig rig("ms hz");
      rig.Float(kBeatMs);
      CHECK(rig.Only() == doctest::Approx(2.f));
      // 0 ms repeats at no rate at all, so nothing is emitted rather than an
      // infinity coming out of the outlet.
      rig.Float(0.f);
      CHECK(rig.Silent());
    }
    {
      TranslateRig rig("hz ms");
      rig.Float(2.f);
      CHECK(rig.Only() == doctest::Approx(kBeatMs));
      rig.Float(0.f);
      CHECK(rig.Silent());
    }
    {
      // The relative family on its own: 480 ticks is one beat, and no tempo is
      // needed to say so.
      TranslateRig rig("beats ticks");
      CHECK(rig.patcher.Clocks()->BoundCount() == 0);
      rig.Float(1.f);
      CHECK(rig.Only() == doctest::Approx(kTicksPerBeat));
      rig.Float(0.25f);
      CHECK(rig.Only() == doctest::Approx(120.f));
    }
    {
      TranslateRig rig("ticks beats");
      rig.Float(240.f);
      CHECK(rig.Only() == doctest::Approx(0.5f));
    }
  }

  // ─── across the families: the clock's tempo ─────────────────────────────────

  TEST_CASE("translate: crossing the families converts at the bound clock's tempo (#516)") {
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("tl.cross", kTempo));

    {
      // Half a second is one beat at 120 BPM, and one beat is 480 ticks.
      TranslateRig rig("ms beats tl.cross");
      CHECK(rig.patcher.Clocks()->BoundCount() == 1);
      CHECK(std::string(rig.patcher.Clocks()->NameOf(1)) == "tl.cross");
      rig.Float(kBeatMs);
      CHECK(rig.Only() == doctest::Approx(1.0));
      rig.Float(2000.f);
      CHECK(rig.Only() == doctest::Approx(4.0));
    }
    {
      TranslateRig rig("ms ticks tl.cross");
      rig.Float(kBeatMs);
      CHECK(rig.Only() == doctest::Approx(kTicksPerBeat));
    }
    {
      // And back the other way, which is the conversion a patch actually wants:
      // how long is a beat, in the milliseconds every `.delay` and `.metro`
      // already speaks.
      TranslateRig rig("beats ms tl.cross");
      rig.Float(1.f);
      CHECK(rig.Only() == doctest::Approx(kBeatMs));
      rig.Float(0.25f);
      CHECK(rig.Only() == doctest::Approx(125.f));
    }
    {
      // Two crossings at once: a beat at 120 BPM repeats at 2 Hz.
      TranslateRig rig("beats hz tl.cross");
      rig.Float(1.f);
      CHECK(rig.Only() == doctest::Approx(2.f));
    }

    DropClock("tl.cross");
  }

  TEST_CASE("translate: the tempo is read at conversion time, so a bang tracks a ramp (#516)") {
    // **The object.** Issue #516: "conversions that depend on tempo must read it
    // from the bound domain clock, not from a stored constant — otherwise a
    // tempo ramp silently invalidates the result." Every number below would be
    // the first one again if the tempo had been cached when the object was
    // built, so a cached implementation fails this case three times over.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("tl.live", kTempo));

    TranslateRig rig("ms beats tl.live");
    rig.Float(kBeatMs);
    CHECK(rig.Only() == doctest::Approx(1.0));

    // A step change: the same half-second is two beats at twice the tempo, and
    // a bang re-converts the value that is already in the object rather than
    // needing it sent again — Max's "bang converts the last input value".
    mgr.setTempo("tl.live", kFastTempo, 0.f);
    rig.Tick();
    rig.Bang();
    CHECK(rig.Only() == doctest::Approx(2.0));

    // A ramp, which is the case the issue names. 240 back to 120 over one
    // second is 120 BPM per second, so one quarter-second block lands on 210
    // exactly — a tempo that exists only *during* the glide, and the answer
    // follows it.
    mgr.setTempo("tl.live", kTempo, 1.f);
    rig.Tick();
    REQUIRE(mgr.currentTempo("tl.live") == doctest::Approx(210.f));
    rig.Bang();
    CHECK(rig.Only() == doctest::Approx(kBeatMs * 210.f / 60000.f));

    // And at the end of the glide it is back to one beat.
    for (int i = 0; i < 4; i++)
      rig.Tick();
    REQUIRE(mgr.currentTempo("tl.live") == doctest::Approx(kTempo));
    rig.Bang();
    CHECK(rig.Only() == doctest::Approx(1.0));

    DropClock("tl.live");
  }

  TEST_CASE("translate: with no exchange rate nothing at all is emitted (#516)") {
    auto& mgr = YSE::CLOCK::Manager();

    {
      // No clock named: the crossing cannot be made, so nothing comes out — not
      // a zero, which would be indistinguishable from a real answer.
      TranslateRig rig("ms beats");
      rig.Float(kBeatMs);
      CHECK(rig.Silent());
      rig.Bang();
      CHECK(rig.Silent());
    }
    {
      // A name nothing has claimed *yet*. The object binds it and stays quiet,
      // then starts answering the moment the clock comes into being — the
      // reader contract: it never creates the clock itself.
      TranslateRig rig("ms beats tl.later");
      REQUIRE_FALSE(mgr.clockExists("tl.later"));
      rig.Float(kBeatMs);
      CHECK(rig.Silent());
      CHECK(rig.patcher.Clocks()->BoundCount() == 1);
      CHECK_FALSE(mgr.clockExists("tl.later"));

      REQUIRE(mgr.createClock("tl.later", kTempo));
      rig.Bang();
      CHECK(rig.Only() == doctest::Approx(1.0));
      DropClock("tl.later");
    }
    {
      // A stopped clock has no exchange rate in either direction: no number of
      // milliseconds is any beats, and no number of beats ever arrives. Tempo 0
      // is how the engine spells "stopped".
      REQUIRE(mgr.createClock("tl.stopped", 0.f));
      TranslateRig rig("ms beats tl.stopped");
      rig.Float(kBeatMs);
      CHECK(rig.Silent());

      mgr.setTempo("tl.stopped", kTempo, 0.f);
      rig.Tick();
      rig.Bang();
      CHECK(rig.Only() == doctest::Approx(1.0));
      DropClock("tl.stopped");
    }
    {
      // A bang before anything has been sent has no last value to convert.
      TranslateRig rig("ms ms");
      rig.Bang();
      CHECK(rig.Silent());
      rig.Float(7.f);
      CHECK(rig.Only() == doctest::Approx(7.f));
      rig.Bang();
      CHECK(rig.Only() == doctest::Approx(7.f));
    }
  }

  // ─── note values ────────────────────────────────────────────────────────────

  TEST_CASE("translate: note values and tick counts are read wherever a value is (#516)") {
    // Max makes `notevalues` a format you select. Here a note value says what
    // it is, so it is read whatever the input format is — through
    // `timeValue.h`'s shared reader, which is what makes `4nd` mean the same
    // thing to this object as it does to a `.delay` and a `.metro`.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("tl.note", kTempo));

    TranslateRig rig("ms ms tl.note");
    rig.Send("4n"); // a quarter note is one beat is 500 ms at 120 BPM
    CHECK(rig.Only() == doctest::Approx(kBeatMs));
    rig.Send("8n");
    CHECK(rig.Only() == doctest::Approx(250.f));
    rig.Send("4nd"); // dotted: Max's 720 ticks
    CHECK(rig.Only() == doctest::Approx(750.f));
    rig.Send("8nt"); // triplet: Max's 160 ticks, a third of a beat
    CHECK(rig.Only() == doctest::Approx(kBeatMs / 3.f));
    rig.Send("1440 ticks"); // Max's other spelling: three beats
    CHECK(rig.Only() == doctest::Approx(1500.f));

    // A bare number is still read in the *input* format, which is the one thing
    // that setting decides.
    rig.Float(250.f);
    CHECK(rig.Only() == doctest::Approx(250.f));

    // And a note value into a relative output needs no clock at all, because
    // both ends are already in beats.
    TranslateRig unbound("ms ticks");
    unbound.Send("4nd");
    CHECK(unbound.Only() == doctest::Approx(720.f));
    unbound.Send("8nt");
    CHECK(unbound.Only() == doctest::Approx(160.f));

    // Nonsense that merely looks like one is not a time value, and neither is a
    // number with something after it.
    rig.Send("4nx");
    CHECK(rig.Silent());
    rig.Send("3n"); // Max's table is the powers of two, and only those
    CHECK(rig.Silent());
    rig.Send("500 wobble");
    CHECK(rig.Silent());
    rig.Send("");
    CHECK(rig.Silent());

    DropClock("tl.note");
  }

  // ─── the settings ───────────────────────────────────────────────────────────

  TEST_CASE("translate: 'in' and 'out' re-point the object, and unknown words do not (#516)") {
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("tl.set", kTempo));

    TranslateRig rig("ms ms tl.set");
    rig.Float(kBeatMs);
    CHECK(rig.Only() == doctest::Approx(kBeatMs));

    rig.Send("out beats");
    rig.Float(kBeatMs);
    CHECK(rig.Only() == doctest::Approx(1.0));

    rig.Send("in beats");
    rig.Float(2.f);
    CHECK(rig.Only() == doctest::Approx(2.0));
    rig.Send("out ms");
    rig.Float(2.f);
    CHECK(rig.Only() == doctest::Approx(1000.f));

    // A word that names no format leaves the setting exactly where it was,
    // rather than reconfiguring the object into something nobody asked for.
    // That includes the two Max formats a domain clock cannot express and the
    // one that is an input spelling here rather than a format.
    rig.Send("out bars.beats.units");
    rig.Send("out hh:mm:ss");
    rig.Send("out notevalues");
    rig.Send("in wobble");
    rig.Send("out");
    rig.Send("in");
    rig.Float(2.f);
    CHECK(rig.Only() == doctest::Approx(1000.f));

    // The input format is applied when the value *arrives*, so changing it
    // afterwards re-points the object for what comes next rather than
    // re-reading what already came.
    rig.Send("in ms");
    rig.Bang();
    CHECK(rig.Only() == doctest::Approx(1000.f));
    rig.Float(2.f);
    CHECK(rig.Only() == doctest::Approx(2.f));

    DropClock("tl.set");
  }

  TEST_CASE("translate: 'clock <name>' re-points the tempo, and a bare 'clock' unbinds (#516)") {
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("tl.a", kTempo));
    REQUIRE(mgr.createClock("tl.b", kFastTempo));

    TranslateRig rig("ms beats tl.a");
    rig.Float(kBeatMs);
    CHECK(rig.Only() == doctest::Approx(1.0));

    rig.Send("clock tl.b");
    CHECK(rig.patcher.Clocks()->BoundCount() == 2);
    rig.Bang();
    CHECK(rig.Only() == doctest::Approx(2.0));

    // Max's bare `clock` gives the clock back. Here that means the crossing
    // goes quiet — there is no millisecond fallback for a tempo.
    rig.Send("clock");
    rig.Bang();
    CHECK(rig.Silent());

    // The conversions that never needed a tempo are untouched by it.
    rig.Send("out hz");
    rig.Float(kBeatMs);
    CHECK(rig.Only() == doctest::Approx(2.f));

    DropClock("tl.b");
    DropClock("tl.a");
  }

  TEST_CASE("translate: the creation arguments are scanned, not counted off (#516)") {
    // The clock name is any token that is not a format, so it may sit on either
    // side of the two formats — `.change`'s reading of the same problem.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("tl.order", kTempo));

    {
      TranslateRig rig("tl.order ms beats");
      CHECK(std::string(rig.patcher.Clocks()->NameOf(1)) == "tl.order");
      rig.Float(kBeatMs);
      CHECK(rig.Only() == doctest::Approx(1.0));
    }
    {
      TranslateRig rig("ms tl.order beats");
      rig.Float(kBeatMs);
      CHECK(rig.Only() == doctest::Approx(1.0));
    }
    {
      // A format word this object refuses is recognised as such rather than
      // taken for the clock name — which is what it would otherwise become, so
      // this patch would have bound a domain clock called "notevalues".
      TranslateRig rig("notevalues ms hz");
      CHECK(rig.patcher.Clocks()->BoundCount() == 0);
      rig.Float(kBeatMs);
      CHECK(rig.Only() == doctest::Approx(2.f));
    }
    {
      // No patcher: SetParent is where binding happens, so a standalone object
      // binds nothing however good its argument looks. It parsed its arguments
      // all the same.
      gTranslate orphan;
      orphan.SetParams("beats hz tl.order");
      CHECK(orphan.InFormat() == Format::BEATS);
      CHECK(orphan.OutFormat() == Format::HZ);
      CHECK_FALSE(orphan.Bound());
      CHECK(std::string(orphan.ClockName()).empty());
    }

    DropClock("tl.order");
  }

  // ─── the audio-callback route ───────────────────────────────────────────────

  TEST_CASE("translate: a conversion made inside Calculate reads the bridge (#516)") {
    // The property a direct unit call structurally cannot show, and the whole
    // reason `ReadTempo` has two routes. A patcher message handler runs on
    // whichever thread dispatched it, and the deferred drain at the top of
    // `Calculate` dispatches *from the audio callback* — so this chain
    // (`.pipe` -> `.translate`) converts on the audio thread, where
    // `CLOCK::Manager()`'s mutex is out of bounds.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("tl.rt", kTempo));

    TranslateRig rig("ms beats tl.rt");
    YSE::pHandle* pipe = rig.patcher.CreateObject(YSE::OBJ::G_PIPE, "5");
    REQUIRE(pipe != nullptr);
    rig.patcher.Connect(pipe, 0, rig.obj, 0);

    // The wait-free read needs the binding resolved; the clock was created
    // before the bind, so this join is all it takes.
    rig.patcher.Clocks()->WaitIdle();
    REQUIRE(rig.patcher.Clocks()->Resolved(1));

    const std::uint64_t due = YSE::PATCHER::messageScheduler::BlocksForMillis(5);
    pipe->SetFloatData(0, kBeatMs);
    for (std::uint64_t block = 0; block <= due + 1; ++block)
      rig.Tick();
    CHECK(rig.Only() == doctest::Approx(1.0));

    // Which route it took, pinned exactly. Destroy the clock and make a fresh
    // one at another tempo under the same name: the manager now answers with
    // the new clock, while the binding stays attached to the old, frozen one
    // forever — `Poll` only ever retries bindings that never resolved (#707).
    // So an answer at the *new* tempo came through `CLOCK::Manager()`, and one
    // at the old tempo came through the bridge.
    mgr.destroyClock("tl.rt");
    mgr.update(0.01f);
    REQUIRE_FALSE(mgr.clockExists("tl.rt"));
    REQUIRE(mgr.createClock("tl.rt", kFastTempo));

    pipe->SetFloatData(0, kBeatMs);
    for (std::uint64_t block = 0; block <= due + 1; ++block)
      rig.Tick();
    CHECK(rig.Only() == doctest::Approx(1.0));

    // And off the callback it is the other route, on the same object in the
    // same breath — the manager, by name, reaching the clock that now holds it.
    rig.Float(kBeatMs);
    CHECK(rig.Only() == doctest::Approx(2.0));

    DropClock("tl.rt");
  }

  TEST_CASE("translate: its handlers allocate nothing (#516)") {
    // The handler may be the audio callback — there is no predicate an object
    // can ask up front — so nothing may be read out of the message into a
    // string. The probe sees std::string allocations since issue #697, so this
    // assertion is not vacuous over the paths that carry text.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("tl.noalloc", kTempo));
    TranslateRig rig("ms ms tl.noalloc");
    rig.Tick();

    // Built outside the probe: it is the *handler* that must not allocate, not
    // the test's own construction of the message.
    const std::string note = "4nd";
    const std::string ticks = "1440 ticks";
    const std::string setOut = "out beats";
    const std::string setIn = "in ticks";
    const std::string reclock = "clock tl.noalloc";
    const std::string ignored = "listen 1 some other patcher message 1 2 3";
    {
      TestHelpers::ProbeScope probe;
      rig.obj->SetBang(0);
      rig.obj->SetIntData(0, 250);
      rig.obj->SetFloatData(0, 250.5f);
      rig.obj->SetListData(0, note);
      rig.obj->SetListData(0, ticks);
      rig.obj->SetListData(0, setOut);
      rig.obj->SetListData(0, setIn);
      rig.obj->SetListData(0, reclock);
      rig.obj->SetListData(0, ignored);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    // And it really did something — an assertion that only proves nothing
    // happened proves nothing. Four values came out (the bang had nothing to
    // convert yet), and the object is now reading ticks and writing beats.
    CHECK(rig.out.seen.size() == 4);
    rig.Clear();
    rig.Float(kTicksPerBeat);
    CHECK(rig.Only() == doctest::Approx(1.0));

    DropClock("tl.noalloc");
  }

  // ─── the use case, end to end ───────────────────────────────────────────────

  TEST_CASE("translate: a note value becomes a '.delay''s time on a running clock (#516)") {
    // Issue #516's use case, driven through the objects a patch would actually
    // use and nothing else: a `.setclock` runs a tempo domain, a `.translate`
    // turns a note value into the milliseconds a `.delay` speaks, and the delay
    // fires at that time. Then the tempo doubles and the same note value makes
    // the same delay fire twice as soon — the whole point of the object, at the
    // level a user sees it.
    auto& mgr = YSE::CLOCK::Manager();

    patcherImplementation live(1, nullptr);
    BangCounter fired;
    YSE::pHandle firedHandle{&fired};

    YSE::pHandle* clock = live.CreateObject(YSE::OBJ::G_SETCLOCK, "tl.e2e 120");
    REQUIRE(clock != nullptr);
    YSE::pHandle* translate = live.CreateObject(YSE::OBJ::G_TRANSLATE, "beats ms tl.e2e");
    REQUIRE(translate != nullptr);
    YSE::pHandle* delay = live.CreateObject(YSE::OBJ::G_DELAY, "1000");
    REQUIRE(delay != nullptr);
    // The converted milliseconds go into the delay's cold inlet, which is what
    // a patch would wire: the note value sets the time, the bang starts it.
    live.Connect(translate, 0, delay, 1);
    live.Connect(delay, 0, &firedHandle, 0);
    live.Clocks()->WaitIdle();
    REQUIRE(mgr.clockExists("tl.e2e"));

    // A quarter note at 120 BPM is 500 ms.
    translate->SetListData(0, "4n");
    const std::uint64_t slow = YSE::PATCHER::messageScheduler::BlocksForMillis(500);
    delay->SetBang(0);
    std::uint64_t blocks = 0;
    while (fired.bangs == 0 && blocks < slow * 4) {
      Tick(live);
      blocks++;
    }
    REQUIRE(fired.bangs == 1);
    CHECK(blocks >= slow);
    CHECK(blocks <= slow + 2);

    // Twice the tempo, and the same note value is half the wait — with nothing
    // in the patch changed but the clock's speed. A `.translate` holding a
    // tempo it cached at load would still be waiting 500 ms here.
    fired.bangs = 0;
    clock->SetFloatData(0, kFastTempo);
    Tick(live);
    REQUIRE(mgr.currentTempo("tl.e2e") == doctest::Approx(kFastTempo));

    translate->SetListData(0, "4n");
    const std::uint64_t fast = YSE::PATCHER::messageScheduler::BlocksForMillis(250);
    delay->SetBang(0);
    blocks = 0;
    while (fired.bangs == 0 && blocks < slow * 4) {
      Tick(live);
      blocks++;
    }
    REQUIRE(fired.bangs == 1);
    CHECK(blocks >= fast);
    CHECK(blocks <= fast + 2);

    DropClock("tl.e2e");
  }

  // ─── persistence ────────────────────────────────────────────────────────────

  TEST_CASE("translate: parameters survive a DumpJSON / ParseJSON round trip (#516)") {
    // Checked by *driving* the restored object rather than by reading the JSON
    // back: a parameter that survived the file but not the rebuild would pass a
    // string comparison and fail here.
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("tl.save", kTempo));

    YSE::patcher src;
    src.create(2);
    YSE::pHandle* obj = src.CreateObject(YSE::OBJ::G_TRANSLATE, "hz beats tl.save");
    REQUIRE(obj != nullptr);

    const std::string json = src.DumpJSON();
    CHECK(json.find("hz beats tl.save") != std::string::npos);

    patcherImplementation restored(1, nullptr);
    restored.ParseJSON(json);
    REQUIRE(restored.Objects() == 1);
    restored.Clocks()->WaitIdle();
    CHECK(restored.Clocks()->BoundCount() == 1);
    CHECK(std::string(restored.Clocks()->NameOf(1)) == "tl.save");

    FloatRecorder out;
    YSE::pHandle outHandle{&out};
    YSE::pHandle* back = restored.GetHandleFromList(0);
    REQUIRE(back != nullptr);
    restored.Connect(back, 0, &outHandle, 0);

    // All three parameters at once: 2 Hz is 500 ms is one beat of the restored
    // clock at 120 BPM. Any one of them lost gives another number, or none.
    back->SetFloatData(0, 2.f);
    REQUIRE(out.seen.size() == 1);
    CHECK(out.seen[0] == doctest::Approx(1.0));

    DropClock("tl.save");
  }

} // TEST_SUITE
