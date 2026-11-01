// Tests for the ~zigzag patcher object (patcher/generatorObjects/dZigzag),
// issue #1067. The function is DSP::zigzag's and is tested in
// Tests/dsp/test_zigzag.cpp; these show that the lists, numbers, messages,
// signals and arguments reach it in the order they were sent, that dump
// reads the list back, that the end bang goes out only through the armed
// outlet (#1037), and that it works in a rendered patch after a JSON round
// trip.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/generatorObjects/dZigzag.h"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BangSink;
using TestHelpers::BufferSink;
using TestHelpers::ScopedSampleRate;
using TestHelpers::Wire;
using YSE::DSP::buffer;
using YSE::PATCHER::dZigzag;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // Every list that arrives, in order.
  struct ListLog : TestHelpers::SinkBase {
    std::vector<std::string> lists;
    ListLog() {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterList(
          [this](const std::string& v, int, YSE::THREAD) { lists.push_back(v); });
    }
    const char* Type() const override {
      return "list_log";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // ~zigzag with sinks on its four outlets. Block() runs one block and the
  // flush the patcher runs after its traversal; `ended` says whether the end
  // bang went out.
  struct Rig {
    BufferSink out;
    BufferSink index;
    ListLog dump;
    BangSink endSink;
    dZigzag zz;
    bool ended = false;

    Rig() {
      Wire(zz, 0, out);
      Wire(zz, 1, index);
      Wire(zz, 2, dump);
      Wire(zz, 3, endSink);
    }
    void Block() {
      endSink.gotBang = false;
      zz.ResetDSP();
      zz.Calculate(YSE::T_DSP);
      zz.FlushArmedOutlets(YSE::T_GUI);
      ended = endSink.gotBang;
    }
    // Signals on the left and right inlets, either null for none.
    void Block(buffer* left, buffer* right) {
      endSink.gotBang = false;
      zz.ResetDSP();
      if (right != nullptr) zz.GetInlet(1)->SetBuffer(right, YSE::T_DSP);
      if (left != nullptr) zz.GetInlet(0)->SetBuffer(left, YSE::T_DSP); // runs CALC()
      zz.FlushArmedOutlets(YSE::T_GUI);
      ended = endSink.gotBang;
    }
    void Send(const std::string& message) {
      zz.GetInlet(0)->SetMessage(message, YSE::T_GUI);
    }
    float At(unsigned int i) const {
      return out.received->getPtr()[i];
    }
    float IndexAt(unsigned int i) const {
      return index.received->getPtr()[i];
    }
  };

  float GuiFloat(YSE::pHandle* h) {
    return std::stof(h->GetGuiValue());
  }

  int GuiInt(YSE::pHandle* h) {
    return std::stoi(h->GetGuiValue());
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~zigzag: registry and shape (#1067)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_ZIGZAG);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~zigzag");
    CHECK(obj->GetInputs() == 2);
    CHECK(obj->GetOutputs() == 4);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->IsDSPInput(1));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(obj->OutputDataType(1) == YSE::OUT_TYPE::BUFFER);
    CHECK(obj->OutputDataType(2) == YSE::OUT_TYPE::LIST);
    CHECK(obj->OutputDataType(3) == YSE::OUT_TYPE::BANG);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_ZIGZAG));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::OSC);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 3);
  }

  TEST_CASE("~zigzag: holds 0, or the initial argument, until points arrive (#1067)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.Block();
    REQUIRE(rig.out.received != nullptr);
    REQUIRE(rig.index.received != nullptr);
    REQUIRE(rig.out.received->getLength() == N);
    for (unsigned int i = 0; i < N; ++i)
      REQUIRE(rig.At(i) == 0.f);
    CHECK_FALSE(rig.ended);

    Rig initial;
    initial.zz.SetParams("0.5");
    initial.Block();
    for (unsigned int i = 0; i < N; ++i)
      REQUIRE(initial.At(i) == 0.5f);
  }

  TEST_CASE("~zigzag: a list of pairs plays with its index; the end bang is armed, then sent "
            "(#1067, #1037)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.zz.GetInlet(0)->SetList("0 0 1 4 -1 2", YSE::T_GUI);
    CHECK(rig.zz.GetParams() == "");
    rig.zz.ResetDSP();
    rig.zz.Calculate(YSE::T_DSP);
    CHECK_FALSE(rig.endSink.gotBang); // CALC() only arms
    CHECK(rig.zz.GetOutlet(3)->Armed());
    rig.zz.FlushArmedOutlets(YSE::T_GUI);
    CHECK(rig.endSink.gotBang);
    const float want[] = {0.25f, 0.5f, 0.75f, 1.f, 0.f, -1.f, -1.f};
    const float index[] = {1, 1, 1, 2, 2, 2, 2}; // on in the sample a point is reached
    for (unsigned int i = 0; i < 7; ++i) {
      CAPTURE(i);
      CHECK(rig.At(i) == doctest::Approx(want[i]));
      CHECK(rig.IndexAt(i) == index[i]);
    }
    rig.Block();
    CHECK_FALSE(rig.ended);
    CHECK(rig.At(N - 1) == -1.f);

    // A last single value takes time 0: a jump as the ramp ends.
    rig.zz.GetInlet(0)->SetList("2 2 5", YSE::T_GUI);
    rig.Block();
    CHECK(rig.At(0) == doctest::Approx(0.5f));
    CHECK(rig.At(1) == 5.f);
    CHECK(rig.zz.Shadow().size() == 2);
    CHECK(rig.ended);
  }

  TEST_CASE("~zigzag: a number is one point reached over the ramptime (#1067)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.zz.GetInlet(0)->SetInt(3, YSE::T_GUI); // ramptime 0: a jump
    rig.Block();
    CHECK(rig.At(0) == 3.f);
    CHECK(rig.ended);
    rig.Send("ramptime 4");
    rig.zz.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    rig.Block();
    CHECK(rig.At(0) == doctest::Approx(2.5f));
    CHECK(rig.At(3) == doctest::Approx(1.f));
    CHECK(rig.zz.Shadow().size() == 1);
    CHECK(rig.zz.GetParams() == "");
  }

  TEST_CASE("~zigzag: edit messages change the list on both sides; dump reads it back "
            "(#1067)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.Send("setline 0 0 10 10 20 10");
    rig.Block();
    CHECK(rig.At(0) == 0.f); // setline does not play
    rig.Send("append 30 10"); // plays to the new point
    rig.Send("insert 1 5 5 6 6"); // two points before index 1
    rig.Send("delete 0 4 4"); // 0 and 4, once
    rig.Send("set 0 50 1");
    rig.Send("setindex 1 60 2");
    rig.Send("dump");
    REQUIRE(rig.dump.lists.size() == 4);
    CHECK(rig.dump.lists[0] == "0 50. 1.");
    CHECK(rig.dump.lists[1] == "1 60. 2.");
    CHECK(rig.dump.lists[2] == "2 10. 10.");
    CHECK(rig.dump.lists[3] == "3 30. 10.");
    CHECK(rig.zz.Shadow().size() == 4);

    // The audio side gets the same edits in the same order: append's ramp
    // from 0 heads for 30, now at index 3.
    rig.Block();
    CHECK(rig.IndexAt(0) == 3.f);
    CHECK(rig.At(0) == doctest::Approx(3.f));
    CHECK(rig.At(9) == doctest::Approx(30.f));
    CHECK(rig.ended);

    // Dumping an empty list sends nothing.
    rig.dump.lists.clear();
    rig.Send("delete 0 1 2 3");
    rig.Send("dump");
    CHECK(rig.dump.lists.empty());
    CHECK(rig.zz.Shadow().size() == 0);
  }

  TEST_CASE("~zigzag: moves, range, bang and stop messages (#1067)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.Send("setline 0 4 1 4 2 4 3 4 4 4");
    rig.Send("bound 1 3");
    rig.zz.GetInlet(0)->SetBang(YSE::T_GUI);
    rig.Block();
    CHECK(rig.At(0) == doctest::Approx(1.25f));
    CHECK(rig.At(7) == doctest::Approx(3.f));
    CHECK(rig.At(8) == doctest::Approx(3.f));
    CHECK(rig.ended);

    rig.Send("bangdelta 2");
    rig.Send("bang");
    rig.Block();
    CHECK(rig.At(1) == doctest::Approx(1.f));

    rig.Send("jump 4 2");
    rig.Block();
    CHECK(rig.At(1) == doctest::Approx(4.f));
    rig.Send("prev");
    rig.Send("stop");
    rig.Block();
    CHECK(rig.IndexAt(0) == 4.f); // stopped before it moved: the point reached
    CHECK(rig.At(0) == doctest::Approx(4.f));
    rig.Send("skip -2 1"); // to point 2 over 1 ms, then on to 3
    rig.Block();
    CHECK(rig.IndexAt(0) == 3.f);
    CHECK(rig.At(0) == doctest::Approx(2.f));
    CHECK(rig.At(4) == doctest::Approx(3.f));
    rig.Send("next");
    rig.Send("start 0");
    rig.Send("end 1");
    rig.Send("jumpend");
    rig.Block();
    CHECK(rig.At(0) == 1.f);
    rig.Send("jumpstart 1");
    rig.Block();
    CHECK(rig.At(0) == 0.f);
  }

  TEST_CASE("~zigzag: speed from the right inlet, a message or a signal that wins (#1067, "
            "#1036)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.zz.GetInlet(1)->SetFloat(0.5f, YSE::T_GUI);
    rig.zz.GetInlet(0)->SetList("0 0 1 4", YSE::T_GUI);
    rig.Block();
    CHECK(rig.At(0) == doctest::Approx(0.125f));
    CHECK(rig.At(7) == doctest::Approx(1.f));

    rig.Send("speed 2");
    rig.zz.GetInlet(0)->SetList("0 0 1 4", YSE::T_GUI);
    rig.Block();
    CHECK(rig.At(0) == doctest::Approx(0.5f));

    rig.zz.GetInlet(1)->SetInt(-1, YSE::T_GUI);
    rig.zz.GetInlet(0)->SetList("0 0 1 4", YSE::T_GUI);
    rig.Block();
    CHECK(rig.At(0) == doctest::Approx(0.75f)); // backward from the end

    // A connected speed signal wins over the number.
    buffer speed(N);
    speed = 0.25f;
    rig.zz.GetInlet(0)->SetList("0 0 1 4", YSE::T_GUI);
    rig.Block(nullptr, &speed);
    CHECK(rig.At(0) == doctest::Approx(0.0625f));
    CHECK(rig.At(15) == doctest::Approx(1.f));
  }

  TEST_CASE("~zigzag: a left signal is an index in mode 0 and a trigger in modes 1 and 2 "
            "(#1067)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.Send("setline 0 0 10 4 20 4");
    buffer in(N);
    in = 1.5f;
    rig.Block(&in, nullptr);
    CHECK(rig.At(0) == doctest::Approx(15.f));
    CHECK(rig.IndexAt(0) == doctest::Approx(1.5f));

    // Unconnected again: play.
    rig.Block();
    CHECK(rig.At(0) == doctest::Approx(15.f));
    CHECK_FALSE(rig.ended);

    rig.Send("mode 1");
    in = 0.f;
    in.getPtr()[0] = 1.f;
    in.getPtr()[1] = 2.f;
    in.getPtr()[2] = 1.f; // falls: a bang restarts at point 0
    rig.Block(&in, nullptr);
    CHECK(rig.At(1) == doctest::Approx(15.f));
    CHECK(rig.At(2) == doctest::Approx(2.5f));

    rig.Send("mode 2"); // a fall steps to the next point
    rig.Send("stop");
    rig.Block();
    in = 0.f;
    in.getPtr()[0] = 5.f;
    in.getPtr()[1] = 4.f;
    rig.Block(&in, nullptr);
    CHECK(rig.IndexAt(1) >= 1.f);
  }

  TEST_CASE("~zigzag: arguments set initial value, loopmode and mode (#1067)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.zz.SetParams("2 1 0");
    rig.zz.GetInlet(0)->SetList("0 0 1 4", YSE::T_GUI);
    rig.Block();
    CHECK(rig.At(3) == 0.f); // looped
    CHECK(rig.At(4) == doctest::Approx(0.25f));
    CHECK(rig.ended);

    Rig step;
    step.zz.SetParams("0 0 2");
    step.Send("setline 0 0 1 4 2 4");
    step.Send("bang"); // mode 2: to the next point, 1, then on
    step.Block();
    CHECK(step.At(0) == doctest::Approx(1.25f));
    CHECK(step.IndexAt(0) == 2.f);

    Rig messages;
    messages.Send("loopmode 2");
    messages.zz.GetInlet(0)->SetList("0 0 1 2", YSE::T_GUI);
    messages.Block();
    CHECK(messages.At(1) == 1.f);
    CHECK(messages.At(3) == 0.f); // turned round
    CHECK(messages.zz.GetParams() == "");
  }

  TEST_CASE("~zigzag: commands apply in the order they came (#1067)") {
    ScopedSampleRate rate(1000);
    Rig stopLast;
    stopLast.zz.GetInlet(0)->SetList("0 0 100 100", YSE::T_GUI);
    stopLast.Send("stop");
    stopLast.Block();
    CHECK(stopLast.At(N - 1) == 0.f);

    Rig stopFirst;
    stopFirst.Send("stop");
    stopFirst.zz.GetInlet(0)->SetList("0 0 100 100", YSE::T_GUI);
    stopFirst.Block();
    CHECK(stopFirst.At(N - 1) == 100.f);
    CHECK(stopFirst.ended);

    Rig editThenBang;
    editThenBang.Send("setline 5 0");
    editThenBang.Send("set 0 7 0");
    editThenBang.Send("bang");
    editThenBang.Block();
    CHECK(editThenBang.At(0) == 7.f);
  }

  TEST_CASE("~zigzag: malformed messages are refused whole, long lists cut (#1067)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.zz.GetInlet(0)->SetList("1 10 x 10", YSE::T_GUI);
    rig.zz.GetInlet(0)->SetFloat(NAN, YSE::T_GUI);
    rig.Send("line");
    rig.Send("append");
    rig.Send("append nan 3");
    rig.Send("set 1");
    rig.Send("bound 1");
    rig.Send("insert 2");
    rig.Send("insert -1 3 3");
    rig.Send("delete");
    rig.Send("jump x");
    CHECK(rig.zz.Dropped() == 11);
    rig.Send("frobnicate 3");
    rig.Block();
    CHECK(rig.At(N - 1) == 0.f);
    CHECK(rig.zz.Shadow().size() == 0);

    std::string longList;
    for (unsigned int i = 0; i < YSE::DSP::zigzag::DEFAULT_POINTS + 1; ++i)
      longList += std::to_string(i + 1) + " 0 ";
    rig.zz.GetInlet(0)->SetList(longList, YSE::T_GUI);
    CHECK(rig.zz.Truncated() == 1);
    CHECK(rig.zz.Shadow().size() == YSE::DSP::zigzag::DEFAULT_POINTS);
    // A second long list in the same block does not fit the queue.
    rig.zz.GetInlet(0)->SetList(longList, YSE::T_GUI);
    CHECK(rig.zz.Dropped() == 12);
    rig.Block();
    CHECK(rig.At(0) == static_cast<float>(YSE::DSP::zigzag::DEFAULT_POINTS));
  }

  TEST_CASE("~zigzag: the arguments survive a DumpJSON / ParseJSON round trip (#1067)") {
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* a = src.CreateObject(YSE::OBJ::D_ZIGZAG, "0.25 2 1");
      REQUIRE(a != nullptr);
      YSE::pHandle* b = src.CreateObject(YSE::OBJ::D_ZIGZAG, "");
      REQUIRE(b != nullptr);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* a = dst.GetHandleFromID(0);
    YSE::pHandle* b = dst.GetHandleFromID(1);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(std::string(a->Type()) == "~zigzag");
    CHECK(a->GetParams() == "0.25 2 1");
    CHECK(b->GetParams() == "");
  }

  TEST_CASE("~zigzag: in a rendered patch, message boxes play, edit and loop the points into "
            "a snapshot and a counter, after a JSON round trip (#1067)") {
    ScopedSampleRate rate(1000);
    std::string dump;
    unsigned int listID = 0;
    unsigned int editID = 0;
    unsigned int levelID = 0;
    unsigned int countID = 0;
    {
      patcherImplementation src(1, nullptr);
      // From the initial 1 to 0 at once, then to 0.5 over 256 ms, looped
      // (loopmode 1); the edit box appends a point while it plays.
      YSE::pHandle* list = src.CreateObject(YSE::OBJ::G_MESSAGE, "0 0 0.5 256");
      YSE::pHandle* edit = src.CreateObject(YSE::OBJ::G_MESSAGE, "append 1 256");
      YSE::pHandle* zz = src.CreateObject(YSE::OBJ::D_ZIGZAG, "1 1");
      YSE::pHandle* snap = src.CreateObject(YSE::OBJ::D_SNAPSHOT, "1 127");
      YSE::pHandle* level = src.CreateObject(YSE::OBJ::G_FLOAT, "");
      YSE::pHandle* count = src.CreateObject(YSE::OBJ::G_COUNTER, "");
      REQUIRE(list != nullptr);
      REQUIRE(edit != nullptr);
      REQUIRE(zz != nullptr);
      REQUIRE(snap != nullptr);
      REQUIRE(level != nullptr);
      REQUIRE(count != nullptr);
      src.Connect(list, 0, zz, 0);
      src.Connect(edit, 0, zz, 0);
      src.Connect(zz, 0, snap, 0);
      src.Connect(snap, 0, level, 1);
      src.Connect(zz, 3, count, 0);
      listID = list->GetID();
      editID = edit->GetID();
      levelID = level->GetID();
      countID = count->GetID();
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* list = dst.GetHandleFromID(listID);
    YSE::pHandle* edit = dst.GetHandleFromID(editID);
    YSE::pHandle* level = dst.GetHandleFromID(levelID);
    YSE::pHandle* count = dst.GetHandleFromID(countID);
    REQUIRE(list != nullptr);
    REQUIRE(edit != nullptr);
    REQUIRE(level != nullptr);
    REQUIRE(count != nullptr);

    dst.Calculate(YSE::T_DSP);
    CHECK(GuiFloat(level) == 1.f); // the initial argument
    CHECK(GuiInt(count) == 0);

    list->SetBang(0);
    // The snapshot reports each block's last sample: 0 at once, then up to
    // 0.5 over 256 ms, looped.
    const float levels[] = {0.25f, 0.f, 0.25f};
    const int counts[] = {0, 1, 1};
    for (int block = 0; block < 3; ++block) {
      CAPTURE(block);
      dst.Calculate(YSE::T_DSP);
      CHECK(GuiFloat(level) == doctest::Approx(levels[block]));
      CHECK(GuiInt(count) == counts[block]);
    }

    // An append while it loops ramps from where it is to the new point,
    // and the loop goes on over the longer list.
    edit->SetBang(0);
    const float longer[] = {0.625f, 0.f, 0.25f, 0.5f, 0.75f};
    const int longerCounts[] = {1, 2, 2, 2, 2};
    for (int block = 0; block < 5; ++block) {
      CAPTURE(block);
      dst.Calculate(YSE::T_DSP);
      CHECK(GuiFloat(level) == doctest::Approx(longer[block]));
      CHECK(GuiInt(count) == longerCounts[block]);
    }
  }

  TEST_CASE("~zigzag: lists, numbers, edits, moves, blocks and the flush on the audio thread "
            "allocate nothing (#1067)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    const std::string list = "0 0 1 50 0.5 30";
    const std::string messages[] = {
        "append 2 10", "insert 1 3 5 4 4", "delete 0 2", "set 0 1 1", "setline 0 1 2 3",
        "jump 1 2",    "next 1",           "prev",       "skip 2 1",  "jumpstart",
        "jumpend 3",   "bound 0 2",        "start 1",    "end -1",    "bangdelta 4",
        "ramptime 2",  "speed -0.5",       "loopmode 2", "mode 1",    "bang",
        "stop",        "line 1 1 2 2"};
    buffer in(N);
    in = 0.5f;
    rig.Block();
    {
      TestHelpers::ProbeScope probe;
      rig.zz.GetInlet(1)->SetFloat(0.3f, YSE::T_DSP);
      rig.zz.GetInlet(1)->SetInt(1, YSE::T_DSP);
      rig.zz.GetInlet(0)->SetFloat(0.5f, YSE::T_DSP);
      rig.zz.GetInlet(0)->SetInt(1, YSE::T_DSP);
      rig.zz.GetInlet(0)->SetList(list, YSE::T_DSP);
      rig.zz.GetInlet(0)->SetBang(YSE::T_DSP);
      rig.Block();
      for (const std::string& m : messages) {
        rig.zz.GetInlet(0)->SetMessage(m, YSE::T_DSP);
        rig.Block();
      }
      rig.Block(&in, &in);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")
