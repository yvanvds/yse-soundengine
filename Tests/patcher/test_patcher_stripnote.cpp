// Tests for `.stripnote` — Max's stripnote, "only pass note-on messages: those
// having any velocity above 0" (issue #539).
//
// The object is trivial and that is exactly the trap: an implementation that
// forwarded every pair would pass a naive "does a note come out" test and leave
// every patch downstream firing twice per key — once when the key goes down and
// once when it comes up. So the cases below are written around the *release*
// rather than around the attack, and every one that checks something passes is
// paired with one that checks something does not.
//
// Three layers, and they are not interchangeable:
//
//   - **standalone cases** pin the grammar and the shape: what each inlet
//     accepts, that a list is Max's inlet distribution, that the right inlet
//     stores without emitting, that the velocity outlet fires before the pitch
//     outlet, and that nothing is clamped on the way through.
//
//   - **the filter itself**, which is the object: a velocity of 0 sends
//     nothing at all, and a non-zero one sends the pair unaltered.
//
//   - **end-to-end cases** run a real `.midiparse` -> `.stripnote` chain inside
//     a real patcher, fed the raw bytes a keyboard actually sends — a note-on
//     followed by a note-off — and ask the question a patch asks: how many
//     times did the downstream object fire? The control case proves the rig can
//     see the doubled trigger before the case that proves `.stripnote` removes
//     it.
//
// No audio device and no MIDI hardware required.

#include <doctest/doctest.h>
#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "patcher/inlet.h"
#include "patcher/midi/mStripNote.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"

using YSE::PATCHER::mStripNote;
using YSE::PATCHER::patcherImplementation;

namespace {

  // Records both outlets into one ordered log, which is the only way to see the
  // half of this object that lives in the *order* the two outlets fire in.
  // Velocity has to reach a downstream object's cold inlet before the pitch
  // reaches its hot one, so a sink watching the pitch outlet alone would be
  // blind to the most consequential way of getting this object wrong.
  //
  // Events are a fixed-size struct in a reserved vector rather than strings, so
  // the allocation probe measures the object under test and not this sink.
  struct Notes : YSE::PATCHER::pObject {
    struct Event {
      char kind = 'p'; // 'p' pitch outlet, 'v' velocity outlet
      int value = 0;
    };

    std::vector<Event> events;

    Notes() : pObject(false) {
      events.reserve(4096);

      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterInt([this](int v, int, YSE::THREAD) {
        Event e;
        e.kind = 'p';
        e.value = v;
        events.push_back(e);
      });

      inputs.emplace_back(this, false, 1);
      inputs.back().RegisterInt([this](int v, int, YSE::THREAD) {
        Event e;
        e.kind = 'v';
        e.value = v;
        events.push_back(e);
      });
    }
    const char* Type() const override {
      return "stripnote_notes";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

    std::size_t n() const {
      return events.size();
    }
    void Clear() {
      events.clear();
    }

    // "v100 p60" — the whole exchange in the order it happened.
    std::string trace() const {
      std::string out;
      for (const Event& e : events) {
        if (!out.empty()) out.push_back(' ');
        out.push_back(e.kind);
        out += std::to_string(e.value);
      }
      return out;
    }
  };

  // Counts note events arriving as `.midiparse` spells them, for the end-to-end
  // chain where the question is how many times a patch downstream would fire.
  struct NoteCounter : YSE::PATCHER::pObject {
    int seen = 0;
    std::vector<std::string> lists;

    NoteCounter() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterInt([this](int, int, YSE::THREAD) { seen++; });
      inputs.back().RegisterList([this](const std::string& v, int, YSE::THREAD) {
        seen++;
        lists.push_back(v);
      });
    }
    const char* Type() const override {
      return "stripnote_counter";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  using TestHelpers::Wire;

  // A standalone `.stripnote` with both outlets watched. The object needs no
  // clock and holds nothing, so standalone is the whole of its behaviour and
  // not merely a convenient subset of it.
  //
  // The sink is declared **before** the object so it is destroyed after it —
  // see Wire on why that matters even for a symmetric edge.
  struct Rig {
    Notes out;
    mStripNote obj;

    Rig() {
      Wire(obj, 0, out, 0);
      Wire(obj, 1, out, 1);
    }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;
    Rig(Rig&&) = delete;
    Rig& operator=(Rig&&) = delete;

    void Int(int value, int inlet = 0) {
      obj.GetInlet(inlet)->SetInt(value, YSE::T_GUI);
    }
    void Float(float value, int inlet = 0) {
      obj.GetInlet(inlet)->SetFloat(value, YSE::T_GUI);
    }
    void List(const std::string& message, int inlet = 0) {
      obj.GetInlet(inlet)->SetList(message, YSE::T_GUI);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── the object exists ──────────────────────────────────────────────────────

  TEST_CASE("stripnote: creatable through the registry (#539)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::M_STRIPNOTE);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".stripnote");
  }

  TEST_CASE("stripnote: listed by pRegistry::AllNames (#539)") {
    auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".stripnote")) != names.end());
  }

  TEST_CASE("stripnote: the shape is two inlets and two int outlets (#539)") {
    mStripNote obj;
    CHECK(obj.NumInputs() == 2);
    CHECK(obj.NumOutputs() == 2);
    CHECK(obj.GetOutputType(0) == YSE::OUT_TYPE::INT);
    CHECK(obj.GetOutputType(1) == YSE::OUT_TYPE::INT);
    CHECK(obj.GetCategory() == YSE::PATCHER::pCategory::MIDI);
    CHECK_FALSE(obj.IsDSPObject());
    CHECK_FALSE(obj.WantsBlockPoll());
  }

  TEST_CASE("stripnote: documents itself (#539)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::M_STRIPNOTE));
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == std::string(YSE::OBJ::M_STRIPNOTE));
    CHECK_FALSE(obj->GetDescription().empty());
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MIDI);
    for (int i = 0; i < obj->NumInputs(); i++)
      CHECK_FALSE(obj->GetInlet(i)->GetDocLabel().empty());
    for (int i = 0; i < obj->NumOutputs(); i++)
      CHECK_FALSE(obj->GetOutlet(i)->GetDocLabel().empty());
    // Max's stripnote takes no creation arguments, so there is nothing to
    // document and nothing a save has to carry.
    CHECK(obj->GetParamDocs().empty());
  }

  TEST_CASE("stripnote: a fresh object holds velocity 0 and passes nothing (#539)") {
    // Max documents no initial velocity, which means 0 — and 0 is the right
    // start: until a velocity has arrived, no note-on has arrived either.
    Rig rig;
    CHECK(rig.obj.Velocity() == 0);
    rig.Int(60);
    CHECK(rig.out.n() == 0);
  }

  // ─── the filter, which is the whole object ──────────────────────────────────

  TEST_CASE("stripnote: a note-on passes, velocity outlet first (#539)") {
    // Max's outlets fire right to left, and here that is load-bearing rather
    // than cosmetic: everything downstream that takes a pair takes its pitch on
    // a hot inlet and its velocity on a cold one, so a pitch sent first would be
    // paired with the *previous* note's velocity.
    Rig rig;
    rig.Int(100, 1);
    rig.Int(60);
    CHECK(rig.out.trace() == "v100 p60");
  }

  TEST_CASE("stripnote: a release sends nothing at all (#539)") {
    // **The object.** An implementation that forwarded every pair would pass
    // every other case in this file and leave the patch downstream firing twice
    // per key, which is the bug `.stripnote` exists to remove.
    Rig rig;
    rig.Int(100, 1);
    rig.Int(60);
    REQUIRE(rig.out.trace() == "v100 p60");

    rig.out.Clear();
    rig.Int(0, 1); // the key comes up
    rig.Int(60);
    CHECK(rig.out.n() == 0); // neither outlet, not even the pitch
  }

  TEST_CASE("stripnote: the velocity survives across notes (#539)") {
    // Stored, not consumed: a keyboard sends one velocity per note-on, and a
    // sequencer driving the right inlet once expects every pitch after it to
    // carry that velocity.
    Rig rig;
    rig.Int(90, 1);
    rig.Int(60);
    rig.Int(64);
    rig.Int(67);
    CHECK(rig.out.trace() == "v90 p60 v90 p64 v90 p67");
    CHECK(rig.obj.Velocity() == 90);
  }

  TEST_CASE("stripnote: the right inlet stores without emitting (#539)") {
    // Only the pitch half completes a note, so only the pitch half can pass one
    // on. A right inlet that emitted would turn every velocity change into a
    // phantom note.
    Rig rig;
    rig.Int(100, 1);
    rig.Float(70.9f, 1);
    rig.List("55", 1);
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.Velocity() == 55);

    rig.Int(60);
    CHECK(rig.out.trace() == "v55 p60");
  }

  TEST_CASE("stripnote: a float is converted to an int in both inlets (#539)") {
    Rig rig;
    rig.Float(100.7f, 1);
    CHECK(rig.obj.Velocity() == 100);
    rig.Float(60.9f);
    CHECK(rig.out.trace() == "v100 p60");
  }

  TEST_CASE("stripnote: nothing is clamped or range-checked on the way through (#539)") {
    // Max's rule is literally "provided it is not 0". This object filters notes;
    // it does not rewrite them, and range belongs to the formatters downstream.
    // A velocity clamped to 0-127 here would silently change what a patch built
    // on wider values sends.
    Rig rig;
    rig.Int(200, 1);
    CHECK(rig.obj.Velocity() == 200);
    rig.Int(60);
    CHECK(rig.out.trace() == "v200 p60");

    // Negative is not 0, so it passes — and it passes unaltered.
    rig.out.Clear();
    rig.Int(-5, 1);
    rig.Int(300);
    CHECK(rig.out.trace() == "v-5 p300");
  }

  // ─── the grammar ────────────────────────────────────────────────────────────

  TEST_CASE("stripnote: a list is Max's inlet distribution — pitch then velocity (#539)") {
    // The velocity is stored *before* the pitch is tested against it, exactly as
    // if it had reached the right inlet first. A list that tested the pitch
    // first would filter the very first note against the wrong velocity — a bug
    // that only ever shows on note one, and the reason this is the shape
    // `.midiparse`'s note outlet can be wired straight into.
    Rig rig;
    rig.List("60 100");
    CHECK(rig.out.trace() == "v100 p60");
    CHECK(rig.obj.Velocity() == 100);

    // And it persists, as a right-inlet value would.
    rig.out.Clear();
    rig.Int(62);
    CHECK(rig.out.trace() == "v100 p62");

    // A release spelled as a list is dropped whole.
    rig.out.Clear();
    rig.List("62 0");
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.Velocity() == 0);

    // Further elements are ignored.
    rig.out.Clear();
    rig.List("64 90 7 7");
    CHECK(rig.out.trace() == "v90 p64");
  }

  TEST_CASE("stripnote: a single-token numeric list is a pitch (#539)") {
    // A `.m 60` reaches this inlet as a list carrying "60"; one that did not
    // read it as a pitch would silently pass nothing.
    Rig rig;
    rig.Int(100, 1);
    rig.List("60");
    CHECK(rig.out.trace() == "v100 p60");

    // And anything that is not a number is not a pitch.
    rig.out.Clear();
    rig.List("wibble");
    rig.List("");
    rig.List("wibble", 1);
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.Velocity() == 100);
  }

  TEST_CASE("stripnote: there is no bang method, as Max's stripnote has none (#539)") {
    Rig rig;
    rig.Int(100, 1);
    rig.obj.GetInlet(0)->SetBang(YSE::T_GUI);
    rig.obj.GetInlet(1)->SetBang(YSE::T_GUI);
    CHECK(rig.out.n() == 0);
  }

  TEST_CASE("stripnote: Calculate sends nothing (#539)") {
    // The object is driven entirely by its inlets; one that emitted would fire a
    // note on every DSP tick from a stimulus no patch sent.
    Rig rig;
    rig.Int(100, 1);
    for (int i = 0; i < 8; i++)
      rig.obj.Calculate(YSE::T_DSP);
    CHECK(rig.out.n() == 0);
  }

  // ─── what a save carries ────────────────────────────────────────────────────

  TEST_CASE("stripnote: it survives a DumpJSON / ParseJSON round trip (#539)") {
    // There are no creation arguments to carry — Max's stripnote takes none —
    // so what a save has to preserve is the object itself, under its own name.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* obj = src.CreateObject(YSE::OBJ::M_STRIPNOTE, "");
    REQUIRE(obj != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".stripnote") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".stripnote");
    CHECK(std::string(copy->GetParams()).empty());
  }

  // ─── end to end, in a real patcher ──────────────────────────────────────────

  TEST_CASE("stripnote: the rig can see the doubled trigger (#539)") {
    // The control for the case below, and it is not optional: a test that only
    // asserted "one event arrived" would pass just as happily against a chain
    // that never decoded anything. Here the same bytes a keyboard sends — a
    // note-on and the note-off that follows it — are decoded by a real
    // `.midiparse` and go straight downstream, and the patch fires *twice* for
    // one key press. That is the bug.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* parse = p.CreateObject(YSE::OBJ::M_PARSE, "");
    REQUIRE(parse != nullptr);

    NoteCounter counter;
    YSE::pHandle counterHandle(&counter);
    p.Connect(parse, 0, &counterHandle, 0); // the note outlet

    const int bytes[] = {0x90, 60, 100, 0x80, 60, 64};
    for (int b : bytes)
      parse->SetIntData(0, b);

    CHECK(counter.seen == 2);
    REQUIRE(counter.lists.size() == 2);
    CHECK(counter.lists[0] == "60 100");
    CHECK(counter.lists[1] == "60 0"); // the release, which a patch must not act on
  }

  TEST_CASE("stripnote: only the attack survives the same byte stream (#539)") {
    // The issue's claim at the level a patch makes it: a real `.midiparse`
    // decoding real MIDI bytes into a real `.stripnote` through real cords. One
    // key press, one trigger — which is the whole point of the object and the
    // one thing the control case above shows does not happen without it.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* parse = p.CreateObject(YSE::OBJ::M_PARSE, "");
    YSE::pHandle* strip = p.CreateObject(YSE::OBJ::M_STRIPNOTE, "");
    REQUIRE(parse != nullptr);
    REQUIRE(strip != nullptr);

    // One cord: `.midiparse` sends the pair as the list `.stripnote`'s left
    // inlet distributes across its own two inlets.
    p.Connect(parse, 0, strip, 0);

    Notes out;
    YSE::pHandle outHandle(&out);
    p.Connect(strip, 0, &outHandle, 0);
    p.Connect(strip, 1, &outHandle, 1);

    const int bytes[] = {0x90, 60, 100, 0x80, 60, 64};
    for (int b : bytes)
      parse->SetIntData(0, b);

    CHECK(out.trace() == "v100 p60"); // the attack, and only the attack

    // A second key press behaves the same way, and the release velocity a
    // note-off carries never leaks through as a note.
    out.Clear();
    const int again[] = {0x90, 67, 40, 0x90, 67, 0}; // release as a zero-velocity note-on
    for (int b : again)
      parse->SetIntData(0, b);
    CHECK(out.trace() == "v40 p67");
  }

  // ─── real-time discipline ───────────────────────────────────────────────────

  TEST_CASE("stripnote: the message paths allocate nothing (#539)") {
    // Everything a message can do on the way in, measured on the thread that
    // does it. A note routinely arrives on the audio callback — an in-patcher
    // dispatch runs on T_DSP — so every one of these paths is audio-thread code.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    Rig rig;

    // Built outside the probe: it is the *handler* that must not allocate, not
    // the test's own construction of the message.
    const std::string pair = "64 90";
    const std::string one = "67";
    const std::string release = "67 0";
    const std::string vel = "77";
    const std::string word = "wibble";
    {
      TestHelpers::ProbeScope probe;
      rig.Int(100, 1);
      rig.Float(90.5f, 1);
      rig.List(vel, 1);
      rig.List(word, 1);
      rig.Int(60);
      rig.Float(62.5f);
      rig.List(pair);
      rig.List(one);
      rig.List(release);
      rig.List(word);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }

    // The probed messages really did something — an assertion that only proves
    // nothing happened proves nothing.
    CHECK(rig.out.n() > 0);
  }
}
