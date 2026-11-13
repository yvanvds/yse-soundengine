// Tests for ~line on DSP::lineSegments (patcher/genericObjects/pLine), the
// line~ retrofit of issue #1042. The ramps are DSP::lineSegments' and are
// tested in Tests/dsp/test_line_segments.cpp; these show that numbers, lists,
// messages and arguments reach it, that patches written for the old ~line
// (a float on the left, a time on the right) still behave as they did, that
// the done bang goes out only through the armed outlet (#1037), and that it
// works in a rendered patch after a JSON round trip. Older single-object
// cases stay in test_dsp_objects.cpp.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/genericObjects/pLine.h"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BangSink;
using TestHelpers::BufferSink;
using TestHelpers::ScopedSampleRate;
using TestHelpers::Wire;
using YSE::PATCHER::patcherImplementation;
using YSE::PATCHER::pLine;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // ~line with sinks on its three outlets. Block() runs one block and the
  // flush the patcher runs after its traversal; `done` says whether the bang
  // was sent.
  struct Rig {
    BufferSink out;
    BangSink doneSink;
    BufferSink activeSink;
    pLine line;
    bool done = false;

    explicit Rig(const std::string& args = "") {
      if (!args.empty()) line.SetParams(args);
      Wire(line, 0, out);
      Wire(line, 1, doneSink);
      Wire(line, 2, activeSink);
    }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;
    Rig(Rig&&) = delete;
    Rig& operator=(Rig&&) = delete;

    void Block() {
      doneSink.gotBang = false;
      line.ResetDSP();
      line.Calculate(YSE::T_DSP);
      line.FlushArmedOutlets(YSE::T_GUI);
      done = doneSink.gotBang;
    }
    float At(unsigned int i) const {
      return out.received->getPtr()[i];
    }
    float Active(unsigned int i) const {
      return activeSink.received->getPtr()[i];
    }
    void Target(float v) {
      line.GetInlet(0)->SetFloat(v, YSE::T_GUI);
    }
    void Time(float ms) {
      line.GetInlet(1)->SetFloat(ms, YSE::T_GUI);
    }
    void List(const std::string& text) {
      line.GetInlet(0)->SetList(text, YSE::T_GUI);
    }
  };

  // Sample k (0-based) of a straight segment from `a` to `b` over `n`
  // samples, written out independently of the class.
  float Segment(float a, float b, unsigned int n, unsigned int k) {
    return static_cast<float>(a + (b - a) * (static_cast<double>(k + 1) / n));
  }

  float GuiFloat(YSE::pHandle* h) {
    return std::stof(h->GetGuiValue());
  }

  int GuiInt(YSE::pHandle* h) {
    return std::stoi(h->GetGuiValue());
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~line: registry and shape (#1042)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_LINE);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~line");
    CHECK(obj->GetInputs() == 2);
    CHECK(obj->GetOutputs() == 3);
    CHECK_FALSE(obj->IsDSPInput(0));
    CHECK_FALSE(obj->IsDSPInput(1));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(obj->OutputDataType(1) == YSE::OUT_TYPE::BANG);
    CHECK(obj->OutputDataType(2) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_LINE));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::OSC);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 3);
  }

  TEST_CASE("~line: holds 0, or the initial argument, until a target arrives (#1042)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.Block();
    REQUIRE(rig.out.received != nullptr);
    REQUIRE(rig.out.received->getLength() == N);
    for (unsigned int i = 0; i < N; ++i) {
      REQUIRE(rig.At(i) == 0.f);
      REQUIRE(rig.Active(i) == 0.f);
    }
    CHECK_FALSE(rig.done);

    // Max's initial-value: the output starts there, it does not ramp to it.
    Rig initial("0.5 20");
    initial.Block();
    for (unsigned int i = 0; i < N; ++i)
      REQUIRE(initial.At(i) == 0.5f);
    CHECK_FALSE(initial.done);
  }

  TEST_CASE("~line: existing patches - a float on the left ramps over the time on the right, "
            "which is kept (#1042)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.Time(200.f);
    rig.Target(2.f);
    rig.Block();
    for (unsigned int k = 0; k < N; ++k)
      REQUIRE(rig.At(k) == doctest::Approx(Segment(0.f, 2.f, 200, k)));
    CHECK_FALSE(rig.done);
    rig.Block(); // samples 128-255: the ramp ends at sample 199
    CHECK(rig.At(199 - N) == 2.f);
    CHECK(rig.At(N - 1) == 2.f);
    CHECK(rig.done);

    // The time is kept: the next target ramps over 200 ms again, unlike Max,
    // where it is used once.
    rig.line.GetInlet(0)->SetInt(0, YSE::T_GUI);
    rig.Block();
    for (unsigned int k = 0; k < N; ++k)
      REQUIRE(rig.At(k) == doctest::Approx(Segment(2.f, 0.f, 200, k)));

    // A new time does not change the ramp that runs; the next target uses it.
    rig.line.GetInlet(1)->SetInt(10, YSE::T_GUI);
    rig.Block();
    CHECK(rig.At(0) == doctest::Approx(Segment(2.f, 0.f, 200, N)));
    rig.Target(1.f);
    rig.Block();
    for (unsigned int k = 0; k < 10; ++k)
      REQUIRE(rig.At(k) == doctest::Approx(Segment(0.f, 1.f, 10, k)));
    CHECK(rig.At(N - 1) == 1.f);
    CHECK(rig.done);

    // A time of 0 jumps, and bangs in that block.
    rig.Time(0.f);
    rig.Target(0.75f);
    rig.Block();
    for (unsigned int k = 0; k < N; ++k)
      REQUIRE(rig.At(k) == 0.75f);
    CHECK(rig.done);

    // The time argument is the time until one arrives at the right inlet:
    // `~line 0 20` ramps every target over 20 ms.
    Rig glide("0 20");
    glide.Target(1.f);
    glide.Block();
    for (unsigned int k = 0; k < 20; ++k)
      REQUIRE(glide.At(k) == doctest::Approx(Segment(0.f, 1.f, 20, k)));
    glide.Target(0.f);
    glide.Block();
    for (unsigned int k = 0; k < 20; ++k)
      REQUIRE(glide.At(k) == doctest::Approx(Segment(1.f, 0.f, 20, k)));
    CHECK(glide.line.GetParams() == "0 20"); // numbers are not saved
  }

  TEST_CASE("~line: existing patches - stop holds until the next target (#1019, #1042)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.Time(1000.f);
    rig.Target(1.f);
    rig.Block();
    rig.line.SetMessage("stop", 0.f);
    rig.Block();
    const float held = rig.At(0);
    CHECK(held == doctest::Approx(Segment(0.f, 1.f, 1000, N - 1)));
    for (int b = 0; b < 10; ++b)
      rig.Block();
    CHECK(rig.At(N - 1) == held);
    CHECK_FALSE(rig.done); // a stop is not an end
    rig.Time(0.f);
    rig.Block();
    CHECK(rig.At(N - 1) == held); // a time alone does not restart it
    rig.Time(1000.f);
    rig.Target(1.f);
    rig.Block();
    CHECK(rig.At(0) == doctest::Approx(held + (1.f - held) / 1000.f));
  }

  TEST_CASE("~line: a list is target-time pairs; a last single number jumps (#1042)") {
    ScopedSampleRate rate(1000);
    Rig rig("0 500"); // the kept time is not used by a list
    rig.List("1 20 0 40");
    rig.Block();
    for (unsigned int k = 0; k < 20; ++k)
      REQUIRE(rig.At(k) == doctest::Approx(Segment(0.f, 1.f, 20, k)));
    for (unsigned int k = 0; k < 40; ++k)
      REQUIRE(rig.At(20 + k) == doctest::Approx(Segment(1.f, 0.f, 40, k)));
    CHECK(rig.At(N - 1) == 0.f);
    for (unsigned int k = 0; k < 59; ++k)
      REQUIRE(rig.Active(k) == 1.f);
    CHECK(rig.Active(59) == 0.f);
    CHECK(rig.done); // once, at the end of the list

    // "5" alone is a jump at the end of the list, at sample 10.
    rig.List("1 10 5");
    rig.Block();
    for (unsigned int k = 0; k < 9; ++k)
      REQUIRE(rig.At(k) == doctest::Approx(Segment(0.f, 1.f, 10, k)));
    CHECK(rig.At(9) == 5.f);
    CHECK(rig.At(N - 1) == 5.f);
    CHECK(rig.done);

    // A one-number list is a jump, whatever the kept time.
    rig.List("2");
    rig.Block();
    CHECK(rig.At(0) == 2.f);
    CHECK(rig.done);
    // A pair alone is one segment.
    rig.List("3 10");
    rig.Block();
    CHECK(rig.At(0) == doctest::Approx(Segment(2.f, 3.f, 10, 0)));
    CHECK(rig.line.GetParams() == "0 500");
  }

  TEST_CASE("~line: Max's initial-value form '<value>, <target> <time> ...' (#1042)") {
    ScopedSampleRate rate(1000);
    Rig rig("0.5");
    // Through the command channel, as a message box sends it...
    rig.line.GetInlet(0)->SetMessage("0, 1 100 0 20", YSE::T_GUI);
    rig.Block();
    for (unsigned int k = 0; k < 100; ++k)
      REQUIRE(rig.At(k) == doctest::Approx(Segment(0.f, 1.f, 100, k)));
    for (unsigned int k = 0; k < 20; ++k)
      REQUIRE(rig.At(100 + k) == doctest::Approx(Segment(1.f, 0.f, 20, k)));
    CHECK(rig.done);

    // ...as a list, and with the comma on its own.
    Rig spaced("0.5");
    spaced.List("-1 , 1 10");
    spaced.Block();
    for (unsigned int k = 0; k < 10; ++k)
      REQUIRE(spaced.At(k) == doctest::Approx(Segment(-1.f, 1.f, 10, k)));
    Rig attached("0.5");
    attached.List("-1, 1 10");
    attached.Block();
    CHECK(attached.At(0) == doctest::Approx(Segment(-1.f, 1.f, 10, 0)));

    // A value and a comma alone is a jump.
    Rig alone;
    alone.line.GetInlet(0)->SetMessage("4,", YSE::T_GUI);
    alone.Block();
    CHECK(alone.At(0) == 4.f);
    CHECK(alone.done);

    // A comma anywhere else is not a number: refused whole.
    Rig bad;
    bad.List("1 10, 2 10");
    bad.List("1 10 2 , 10");
    CHECK(bad.line.Dropped() == 2);
    bad.Block();
    CHECK(bad.At(N - 1) == 0.f);
  }

  TEST_CASE("~line: a jump and a list in one block both apply, in the order they came "
            "(#1042)") {
    ScopedSampleRate rate(1000);
    // [.t b b] sending 0 and then 1 1000: the list starts from the jump.
    Rig jumpFirst("0.5");
    jumpFirst.Target(0.f);
    jumpFirst.List("1 1000");
    jumpFirst.Block();
    CHECK(jumpFirst.At(0) == doctest::Approx(Segment(0.f, 1.f, 1000, 0)));
    CHECK_FALSE(jumpFirst.done); // the list replaced the jump's end

    Rig listFirst;
    listFirst.List("1 100");
    listFirst.Target(0.25f);
    listFirst.Block();
    CHECK(listFirst.At(0) == 0.25f);
    CHECK(listFirst.At(N - 1) == 0.25f);
    CHECK(listFirst.done);

    Rig stopLast;
    stopLast.List("100 100");
    stopLast.line.SetMessage("stop", 0.f);
    stopLast.Block();
    CHECK(stopLast.At(N - 1) == 0.f);

    Rig listLast;
    listLast.line.SetMessage("stop", 0.f);
    listLast.List("100 100");
    listLast.Block();
    CHECK(listLast.At(N - 1) == 100.f);
    CHECK(listLast.done);

    // A stop then a target before the next block: the target wins (#1019).
    Rig target;
    target.Time(1000.f);
    target.Target(1.f);
    target.Block();
    target.line.SetMessage("stop", 0.f);
    target.Time(0.f);
    target.Target(0.25f);
    target.Block();
    CHECK(target.At(N - 1) == 0.25f);

    // Two lists: the last wins, from where the output is.
    Rig twoLists;
    twoLists.List("9 10");
    twoLists.List("2 10");
    twoLists.Block();
    CHECK(twoLists.At(9) == 2.f);
  }

  TEST_CASE("~line: pause, resume and stop (#1042)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.List("512 512 0 10");
    rig.Block();
    CHECK(rig.At(N - 1) == doctest::Approx(128.f));
    CHECK(rig.Active(N - 1) == 1.f);

    rig.line.SetMessage("pause", 0.f);
    rig.Block();
    CHECK(rig.At(0) == doctest::Approx(128.f));
    CHECK(rig.At(N - 1) == doctest::Approx(128.f));
    CHECK(rig.Active(0) == 0.f);

    rig.line.GetInlet(0)->SetMessage("resume", YSE::T_GUI);
    rig.Block();
    CHECK(rig.At(0) == doctest::Approx(129.f));
    CHECK(rig.Active(0) == 1.f);

    // A word-led list (through a receive) is a message too. A stop drops the
    // pending segments as well.
    rig.List("stop");
    rig.Block();
    CHECK_FALSE(rig.done);
    for (int b = 0; b < 4; ++b)
      rig.Block();
    CHECK(rig.At(N - 1) == doctest::Approx(256.f));
    CHECK_FALSE(rig.done);
    CHECK(rig.Active(N - 1) == 0.f);

    // A new target ends a pause.
    rig.List("0 1000");
    rig.Block();
    rig.line.SetMessage("pause", 0.f);
    rig.Block();
    rig.Target(7.f);
    rig.Block();
    CHECK(rig.At(0) == 7.f);

    // Unknown messages change nothing.
    rig.line.SetMessage("set 3", 0.f);
    rig.line.SetMessage("", 0.f);
    rig.line.SetMessage("factor 2", 0.f);
    rig.Block();
    CHECK(rig.At(N - 1) == 7.f);
    CHECK(rig.line.GetParams() == "");
  }

  TEST_CASE("~line: the done bang is armed in CALC() and sent by the flush (#1042, #1037)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.List("1 10");
    rig.line.ResetDSP();
    rig.line.Calculate(YSE::T_DSP);
    CHECK_FALSE(rig.doneSink.gotBang); // CALC() only arms
    CHECK(rig.line.GetOutlet(1)->Armed());
    rig.line.FlushArmedOutlets(YSE::T_GUI);
    CHECK(rig.doneSink.gotBang);
    CHECK(rig.doneSink.bangCount == 1);
    rig.Block();
    CHECK_FALSE(rig.done);
  }

  TEST_CASE("~line: malformed lists are refused whole, long ones cut to maxpoints and logged "
            "(#1042)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.List("1 10 x 10");
    rig.List("1 nan");
    rig.Target(NAN);
    CHECK(rig.line.Dropped() == 3);
    rig.Block();
    CHECK(rig.At(N - 1) == 0.f);
    CHECK_FALSE(rig.done);

    // Max's default: 129 segments.
    std::string longList;
    for (int i = 0; i < pLine::DEFAULT_MAXPOINTS + 1; ++i)
      longList += std::to_string(i + 1) + " 0 ";
    rig.List(longList);
    CHECK(rig.line.Truncated() == 1);
    rig.Block();
    CHECK(rig.At(0) == static_cast<float>(pLine::DEFAULT_MAXPOINTS));

    // maxpoints is the third argument; an initial value counts as one.
    Rig small("0 0 3");
    small.List("9, 1 1 2 1 3 1 4 1");
    CHECK(small.line.Truncated() == 1);
    small.Block();
    CHECK(small.At(N - 1) == 2.f);
    CHECK(small.line.GetParams() == "0 0 3");

    // Beyond the capacity it is held to it.
    Rig huge("0 0 100000");
    std::string longer;
    for (unsigned int i = 0; i < pLine::CAPACITY + 1; ++i)
      longer += std::to_string(i + 1) + " 0 ";
    huge.List(longer);
    CHECK(huge.line.Truncated() == 1);
    huge.Block();
    CHECK(huge.At(0) == static_cast<float>(pLine::CAPACITY));
  }

  TEST_CASE("~line: the arguments survive a DumpJSON / ParseJSON round trip (#1042)") {
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* a = src.CreateObject(YSE::OBJ::D_LINE, "0.25 20 16");
      REQUIRE(a != nullptr);
      YSE::pHandle* b = src.CreateObject(YSE::OBJ::D_LINE, "");
      REQUIRE(b != nullptr);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* a = dst.GetHandleFromID(0);
    YSE::pHandle* b = dst.GetHandleFromID(1);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(std::string(a->Type()) == "~line");
    CHECK(a->GetParams() == "0.25 20 16");
    CHECK(b->GetParams() == "");
  }

  TEST_CASE("~line: in a rendered patch, an old-style float and time and a new list reach "
            "snapshots, a counter and the active signal, after a JSON round trip (#1042)") {
    ScopedSampleRate rate(1000);
    std::string dump;
    unsigned int timeID = 0;
    unsigned int targetID = 0;
    unsigned int listID = 0;
    unsigned int levelID = 0;
    unsigned int countID = 0;
    unsigned int activeID = 0;
    {
      patcherImplementation src(1, nullptr);
      // The old way: a time box into the right inlet, a target box into the
      // left. The new way: a list of pairs.
      YSE::pHandle* time = src.CreateObject(YSE::OBJ::G_MESSAGE, "256");
      YSE::pHandle* target = src.CreateObject(YSE::OBJ::G_MESSAGE, "1");
      YSE::pHandle* list = src.CreateObject(YSE::OBJ::G_MESSAGE, "0, 0.5 128");
      YSE::pHandle* line = src.CreateObject(YSE::OBJ::D_LINE, "0.25");
      YSE::pHandle* snap = src.CreateObject(YSE::OBJ::D_SNAPSHOT, "1 127");
      YSE::pHandle* activeSnap = src.CreateObject(YSE::OBJ::D_SNAPSHOT, "1 127");
      YSE::pHandle* level = src.CreateObject(YSE::OBJ::G_FLOAT, "");
      YSE::pHandle* count = src.CreateObject(YSE::OBJ::G_COUNTER, "");
      YSE::pHandle* active = src.CreateObject(YSE::OBJ::G_FLOAT, "");
      for (YSE::pHandle* h : {time, target, list, line, snap, activeSnap, level, count, active})
        REQUIRE(h != nullptr);
      src.Connect(time, 0, line, 1);
      src.Connect(target, 0, line, 0);
      src.Connect(list, 0, line, 0);
      src.Connect(line, 0, snap, 0);
      src.Connect(snap, 0, level, 1);
      src.Connect(line, 1, count, 0);
      src.Connect(line, 2, activeSnap, 0);
      src.Connect(activeSnap, 0, active, 1);
      timeID = time->GetID();
      targetID = target->GetID();
      listID = list->GetID();
      levelID = level->GetID();
      countID = count->GetID();
      activeID = active->GetID();
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* time = dst.GetHandleFromID(timeID);
    YSE::pHandle* target = dst.GetHandleFromID(targetID);
    YSE::pHandle* list = dst.GetHandleFromID(listID);
    YSE::pHandle* level = dst.GetHandleFromID(levelID);
    YSE::pHandle* count = dst.GetHandleFromID(countID);
    YSE::pHandle* active = dst.GetHandleFromID(activeID);
    for (YSE::pHandle* h : {time, target, list, level, count, active})
      REQUIRE(h != nullptr);

    dst.Calculate(YSE::T_DSP);
    CHECK(GuiFloat(level) == 0.25f); // the initial argument
    CHECK(GuiInt(count) == 0);

    // The old way: 0.25 to 1 over 256 ms, kept for the next target.
    time->SetBang(0);
    target->SetBang(0);
    const float levels[] = {0.25f + 0.75f * 128.f / 256.f, 1.f, 1.f};
    const int counts[] = {0, 1, 1};
    const float actives[] = {1.f, 0.f, 0.f};
    for (int block = 0; block < 3; ++block) {
      CAPTURE(block);
      dst.Calculate(YSE::T_DSP);
      CHECK(GuiFloat(level) == doctest::Approx(levels[block]));
      CHECK(GuiInt(count) == counts[block]);
      CHECK(GuiFloat(active) == actives[block]);
    }

    // The new way: jump to 0, then to 0.5 over 128 ms.
    list->SetBang(0);
    dst.Calculate(YSE::T_DSP);
    CHECK(GuiFloat(level) == 0.5f);
    CHECK(GuiInt(count) == 2);
    CHECK(GuiFloat(active) == 0.f);
  }

  TEST_CASE("~line: numbers, lists, messages, blocks and the flush on the audio thread "
            "allocate nothing (#1042)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    std::string longList;
    for (int i = 0; i < pLine::DEFAULT_MAXPOINTS + 5; ++i)
      longList += "1 1 ";
    const std::string list = "1 50 0 30 0.5";
    const std::string initial = "0, 1 50";
    const std::string pause = "pause";
    const std::string resume = "resume";
    const std::string stop = "stop";
    rig.Block();
    {
      TestHelpers::ProbeScope probe;
      rig.line.GetInlet(1)->SetFloat(20.f, YSE::T_DSP);
      rig.line.GetInlet(1)->SetInt(10, YSE::T_DSP);
      rig.line.GetInlet(0)->SetFloat(0.5f, YSE::T_DSP);
      rig.Block();
      rig.line.GetInlet(0)->SetInt(1, YSE::T_DSP);
      rig.line.GetInlet(0)->SetList(list, YSE::T_DSP);
      rig.Block();
      rig.line.GetInlet(0)->SetMessage(initial, YSE::T_DSP);
      rig.line.GetInlet(0)->SetList(longList, YSE::T_DSP); // cut and logged
      rig.Block();
      rig.line.GetInlet(0)->SetMessage(pause, YSE::T_DSP);
      rig.Block();
      rig.line.GetInlet(0)->SetMessage(resume, YSE::T_DSP);
      rig.Block();
      rig.line.GetInlet(1)->SetFloat(0.f, YSE::T_DSP);
      rig.line.GetInlet(0)->SetFloat(3.f, YSE::T_DSP);
      rig.line.GetInlet(0)->SetMessage(stop, YSE::T_DSP);
      rig.Block();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(rig.line.Truncated() == 1);
  }

} // TEST_SUITE("patcher")
