// Tests for the ~matrix patcher object (patcher/routing/dMatrix and its
// control side, matrixControl), issue #1134. The mixing and the ramps are
// DSP::mixMatrix's and are tested in Tests/dsp/test_mix_matrix.cpp; these
// show that the pins, the connection messages, the ramp and exclusive
// settings and the arguments reach it, that the dumps come out the rightmost
// outlet after the block and replay, and that ~matrix mixes and ramps in
// rendered patches after a JSON round trip.

#include <doctest/doctest.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/routing/dMatrix.h"
#include "dsp/routing/mixMatrix.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::mixMatrix;
using YSE::PATCHER::dMatrix;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // Every list that arrives, in order.
  struct LinesSink : TestHelpers::SinkBase {
    std::vector<std::string> lines;
    LinesSink() {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterList(
          [this](const std::string& v, int, YSE::THREAD) { lines.push_back(v); });
    }
    const char* Type() const override {
      return "lines_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // Counts lists without allocating, for the allocation test.
  struct CountSink : TestHelpers::SinkBase {
    int count = 0;
    CountSink() {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterList([this](const std::string&, int, YSE::THREAD) { ++count; });
    }
    const char* Type() const override {
      return "count_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // One block of a standalone ~matrix, the way the patcher runs it: reset,
  // the connected inputs, the calculation, then the armed dump.
  void Block(dMatrix& obj, const std::vector<buffer*>& ins) {
    obj.ResetDSP();
    for (std::size_t i = 0; i < ins.size(); ++i) {
      if (ins[i] != nullptr) obj.GetInlet(static_cast<int>(i))->SetBuffer(ins[i], YSE::T_GUI);
    }
    obj.Calculate(YSE::T_DSP);
    obj.FlushArmedOutlets(YSE::T_GUI);
  }

  void Text(dMatrix& obj, const std::string& text) {
    // Word-first text takes the message channel, a number-first list the
    // list handler, as from a message box or a receive.
    obj.GetInlet(0)->SetList(text, YSE::T_GUI);
  }

  // Every crosspoint's target in the object's DSP class equals the reference.
  void SameTargets(const dMatrix& obj, const mixMatrix& reference) {
    for (unsigned int in = 0; in < reference.inputs(); ++in)
      for (unsigned int out = 0; out < reference.outputs(); ++out) {
        CAPTURE(in);
        CAPTURE(out);
        CHECK(obj.Matrix().target(in, out) == reference.target(in, out));
      }
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~matrix: registry and shape (#1134)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* one = p.CreateObject(YSE::OBJ::D_MATRIX);
    REQUIRE(one != nullptr);
    CHECK(std::string(one->Type()) == "~matrix");
    CHECK(one->GetInputs() == 1);
    CHECK(one->GetOutputs() == 2);
    CHECK(one->IsDSPInput(0));
    CHECK(one->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(one->OutputDataType(1) == YSE::OUT_TYPE::LIST);

    YSE::pHandle* m = p.CreateObject(YSE::OBJ::D_MATRIX, "4 3");
    REQUIRE(m != nullptr);
    CHECK(m->GetInputs() == 4);
    CHECK(m->GetOutputs() == 4);
    for (unsigned int i = 0; i < 4; ++i)
      CHECK(m->IsDSPInput(i));
    for (unsigned int o = 0; o < 3; ++o)
      CHECK(m->OutputDataType(o) == YSE::OUT_TYPE::BUFFER);
    CHECK(m->OutputDataType(3) == YSE::OUT_TYPE::LIST);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_MATRIX));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::ROUTING);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);

    // Sizes are clamped to 1-128.
    dMatrix big;
    big.SetParams("500 0");
    CHECK(big.Inputs() == mixMatrix::MAX_CHANNELS);
    CHECK(big.Outputs() == 1);
    CHECK(big.NumInputs() == static_cast<int>(mixMatrix::MAX_CHANNELS));
    CHECK(big.NumOutputs() == 2);
  }

  TEST_CASE("~matrix: connection messages reach the DSP class at the next block (#1134)") {
    TestHelpers::ScopedSampleRate rate(48000);
    BufferSink out0;
    BufferSink out1;
    BufferSink out2;
    dMatrix obj;
    obj.SetParams("3 3 0.5");
    TestHelpers::Wire(obj, 0, out0, 0);
    TestHelpers::Wire(obj, 1, out1, 0);
    TestHelpers::Wire(obj, 2, out2, 0);
    mixMatrix reference(3, 3, 0.5f);

    buffer a(N);
    buffer b(N);
    buffer c(N);
    buffer ra(N);
    buffer rb(N);
    buffer rc(N);
    a = 1.f;
    b = 10.f;
    c = 100.f;
    ra = 1.f;
    rb = 10.f;
    rc = 100.f;
    const std::vector<buffer*> ins = {&a, &b, &c};
    buffer* refs[3] = {&ra, &rb, &rc};
    BufferSink* sinks[3] = {&out0, &out1, &out2};

    const auto step = [&]() {
      Block(obj, ins);
      reference(refs);
      SameTargets(obj, reference);
      for (unsigned int o = 0; o < 3; ++o) {
        CAPTURE(o);
        REQUIRE(sinks[o]->received != nullptr);
        CHECK(TestHelpers::buffersNearlyEqual(*sinks[o]->received, reference.output(o), 1e-4f));
      }
    };

    step();
    CHECK(out0.received->isSilent());

    // A list: in, out, gain.
    Text(obj, "0 1 0.8");
    // Nothing reaches the DSP class before the block.
    CHECK(obj.Matrix().target(0, 1) == 0.f);
    reference.set(0, 1, 0.8f);
    step();
    CHECK(obj.Matrix().target(0, 1) == 0.8f);

    // connect with several outlets at the default gain, and with a float gain.
    Text(obj, "connect 1 0 2");
    reference.connect(1, 0).connect(1, 2);
    step();
    Text(obj, "connect 2 0 1 0.25");
    reference.set(2, 0, 0.25f).set(2, 1, 0.25f);
    step();
    // A whole-number gain written as a float is a gain, not an outlet.
    Text(obj, "connect 2 2 1.");
    reference.set(2, 2, 1.f);
    step();
    CHECK(obj.Matrix().target(2, 2) == 1.f);
    CHECK(obj.Matrix().target(2, 1) == 0.25f);

    Text(obj, "disconnect 2 0 1");
    reference.disconnect(2, 0).disconnect(2, 1);
    step();

    // The fourth list item is that change's ramp, and `ramp` sets later ones.
    Text(obj, "1 1 2 0");
    reference.set(1, 1, 2.f, 0.f);
    Text(obj, "ramp 2");
    reference.rampTime(2.f);
    Text(obj, "0 0 -1");
    reference.set(0, 0, -1.f);
    step();
    step();
    Text(obj, "ramptime 0");
    reference.rampTime(0.f);
    Text(obj, "0 0 0.5");
    reference.set(0, 0, 0.5f);
    step();

    // Exclusive outputs: a new connection replaces the inputs on its output.
    Text(obj, "exclusive 2");
    reference.exclusive(mixMatrix::EXCLUSIVE_OUTPUTS);
    Text(obj, "connect 2 0");
    reference.connect(2, 0);
    step();
    CHECK(obj.Matrix().target(0, 0) == 0.f);
    CHECK(obj.Matrix().target(1, 0) == 0.f);
    CHECK(obj.Matrix().target(2, 0) == 0.5f);
    Text(obj, "exclusive 0");
    reference.exclusive(mixMatrix::EXCLUSIVE_OFF);
    Text(obj, "connect 0 0");
    reference.connect(0, 0);
    step();
    CHECK(obj.Matrix().target(2, 0) == 0.5f);

    // Several changes to one crosspoint between blocks: the last wins.
    Text(obj, "1 2 0.1");
    Text(obj, "1 2 0.2");
    Text(obj, "1 2 0.3");
    reference.set(1, 2, 0.3f);
    step();

    // Ports out of range, malformed messages and print / dictionary change
    // nothing.
    for (const char* text :
         {"5 0 1", "0 9 1", "-1 0 1", "0 1", "0 1 2 3 4", "connect 7 0", "connect",
          "disconnect x 1", "print", "dictionary routing", "nonsense 1 2"})
      Text(obj, text);
    step();

    Text(obj, "clear");
    reference.clear();
    step();
    for (unsigned int in = 0; in < 3; ++in)
      for (unsigned int o = 0; o < 3; ++o)
        CHECK(obj.Matrix().target(in, o) == 0.f);
    CHECK(out0.received->isSilent());
  }

  TEST_CASE("~matrix: arguments (#1134)") {
    const auto check = [](const std::string& args, unsigned int inputs, unsigned int outputs,
                          float gain, float rampMs, int exclusive) {
      CAPTURE(args);
      dMatrix obj;
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      CHECK(obj.Inputs() == inputs);
      CHECK(obj.Outputs() == outputs);
      CHECK(obj.NumInputs() == static_cast<int>(inputs));
      CHECK(obj.NumOutputs() == static_cast<int>(outputs) + 1);
      // The default gain shows in a connect, the ramp and the exclusive mode
      // in what the next block does with it.
      TestHelpers::ScopedSampleRate rate(48000);
      std::vector<buffer*> none(inputs, nullptr);
      Text(obj, "connect 0 0");
      Block(obj, none);
      CHECK(obj.Matrix().target(0, 0) == gain);
      const double samples = rampMs * 48.0;
      const float expected =
          samples > 1.0 ? gain * static_cast<float>(std::fmin(1.0, N / samples)) : gain;
      CHECK(obj.Matrix().gain(0, 0) == doctest::Approx(expected).epsilon(1e-5));
      if (inputs > 1) {
        Text(obj, "connect 1 0");
        Block(obj, none);
        // Exclusive outputs: input 0 was replaced on output 0.
        const bool replaced = (exclusive & mixMatrix::EXCLUSIVE_OUTPUTS) != 0;
        CHECK((obj.Matrix().target(0, 0) == 0.f) == replaced);
      }
    };

    check("", 1, 1, 1.f, 10.f, 0);
    check("2 3", 2, 3, 1.f, 10.f, 0);
    check("2 2 0.5", 2, 2, 0.5f, 10.f, 0);
    check("2 2 0.5 ramp 0", 2, 2, 0.5f, 0.f, 0);
    check("4 1 ramptime 5 exclusive 2", 4, 1, 1.f, 5.f, 2);
    check("exclusive 3 3 2 -1", 3, 2, -1.f, 10.f, 3);
    // A word is skipped, a negative ramp is 0, an exclusive mode is clamped.
    check("x 2 2 ramp -4 exclusive 9", 2, 2, 1.f, 0.f, 3);
  }

  TEST_CASE("~matrix: dump, dumptarget and dumpconnections come out the rightmost outlet "
            "after the block and replay into another matrix (#1134)") {
    TestHelpers::ScopedSampleRate rate(48000);
    LinesSink lines;
    dMatrix obj;
    obj.SetParams("2 3 1 ramp 0");
    TestHelpers::Wire(obj, 3, lines, 0);
    const std::vector<buffer*> none(2, nullptr);

    Text(obj, "1 2 0.5");
    Text(obj, "0 1 0.25");
    Text(obj, "connect 1 0");
    Text(obj, "dumptarget");
    // Nothing is sent before the block.
    CHECK(lines.lines.empty());
    Block(obj, none);
    // By input, then by output.
    CHECK(lines.lines == std::vector<std::string>{"0 1 0.25", "1 0 1.", "1 2 0.5"});

    // dump reports the current gains, which differ from the targets while a
    // crosspoint ramps; a crosspoint fading out is still reported.
    lines.lines.clear();
    Text(obj, "ramp 1000");
    Text(obj, "0 1 0");
    Text(obj, "0 0 1");
    Text(obj, "dump");
    Block(obj, none);
    REQUIRE(lines.lines.size() == 4);
    CHECK(lines.lines[2] == "1 0 1.");
    CHECK(lines.lines[3] == "1 2 0.5");
    // "<in> <out> <gain>": the ports exact, the gain part way through its
    // ramp.
    const auto gainOf = [](const std::string& line, const char* ports) {
      const std::size_t length = std::strlen(ports);
      REQUIRE(line.compare(0, length, ports) == 0);
      char* end = nullptr;
      const float gain = std::strtof(line.c_str() + length, &end);
      CHECK(*end == '\0');
      return gain;
    };
    CHECK(gainOf(lines.lines[0], "0 0 ") == doctest::Approx(128.f / 48000.f).epsilon(1e-3));
    CHECK(gainOf(lines.lines[1], "0 1 ") ==
          doctest::Approx(0.25f * (1.f - (128.f / 48000.f))).epsilon(1e-3));

    // dumpconnections is the targets.
    lines.lines.clear();
    Text(obj, "dumpconnections");
    Block(obj, none);
    CHECK(lines.lines == std::vector<std::string>{"0 0 1.", "1 0 1.", "1 2 0.5"});

    // The lines replay: fed into a cleared matrix, they rebuild the routing.
    dMatrix copy;
    copy.SetParams("2 3");
    for (const std::string& line : lines.lines)
      Text(copy, line);
    Block(copy, none);
    for (unsigned int i = 0; i < 2; ++i)
      for (unsigned int o = 0; o < 3; ++o)
        CHECK(copy.Matrix().target(i, o) == obj.Matrix().target(i, o));

    // Requests between two blocks are both kept: the current gains come out
    // after the first block (four crosspoints, one still fading out), the
    // targets after the second (three).
    lines.lines.clear();
    Text(obj, "dumptarget");
    Text(obj, "dump");
    Block(obj, none);
    CHECK(lines.lines.size() == 4);
    Block(obj, none);
    CHECK(lines.lines.size() == 7);
    Block(obj, none);
    CHECK(lines.lines.size() == 7);
  }

  TEST_CASE("~matrix: a large dump is spread over blocks (#1134)") {
    LinesSink lines;
    dMatrix obj;
    obj.SetParams("32 32 1 ramp 0");
    TestHelpers::Wire(obj, 32, lines, 0);
    const std::vector<buffer*> none(32, nullptr);
    for (int i = 0; i < 32; ++i)
      for (int o = 0; o < 32; ++o)
        Text(obj, "connect " + std::to_string(i) + " " + std::to_string(o));
    Text(obj, "dumptarget");
    Block(obj, none);
    CHECK(lines.lines.size() == dMatrix::DUMP_LINES_PER_BLOCK);
    for (int b = 0; b < 3; ++b)
      Block(obj, none);
    CHECK(lines.lines.size() == 32 * 32);
    CHECK(lines.lines.front() == "0 0 1.");
    CHECK(lines.lines.back() == "31 31 1.");
    Block(obj, none);
    CHECK(lines.lines.size() == 32 * 32);
  }

  TEST_CASE("~matrix: mixes ~sig sources onto dac channels, ramping, driven through a receive "
            "after a JSON round trip (#1134)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* a = src.CreateObject(YSE::OBJ::D_SIG, "1");
      YSE::pHandle* b = src.CreateObject(YSE::OBJ::D_SIG, "10");
      YSE::pHandle* m = src.CreateObject(YSE::OBJ::D_MATRIX, "2 2 1 ramp 5");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "mx1134");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(a != nullptr);
      REQUIRE(b != nullptr);
      REQUIRE(m != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(a, 0, m, 0);
      src.Connect(b, 0, m, 1);
      src.Connect(cmd, 0, m, 0);
      src.Connect(m, 0, dac, 0);
      src.Connect(m, 1, dac, 1);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    const auto all = [&](unsigned int channel, float value) {
      const float* y = dst.output[channel].getPtr();
      for (unsigned int i = 0; i < N; ++i)
        if (std::fabs(y[i] - value) > 1e-4f) return false;
      return true;
    };

    // Nothing connected yet.
    dst.Calculate(YSE::T_DSP);
    CHECK(all(0, 0.f));
    CHECK(all(1, 0.f));

    // 5 ms at 48 kHz: 240 samples of ramp, no step larger than 1/240 of the
    // signal.
    REQUIRE(dst.PassData("connect 0 0 1", "mx1134", YSE::T_GUI));
    REQUIRE(dst.PassData("1 1 0.5", "mx1134", YSE::T_GUI));
    float previous0 = 0.f;
    float previous1 = 0.f;
    float largest0 = 0.f;
    float largest1 = 0.f;
    for (unsigned int block = 0; block < 2; ++block) {
      dst.Calculate(YSE::T_DSP);
      for (unsigned int i = 0; i < N; ++i) {
        const unsigned int k = block * N + i;
        const float ramp = std::fmin(1.f, static_cast<float>(k + 1) / 240.f);
        const float y0 = dst.output[0].getPtr()[i];
        const float y1 = dst.output[1].getPtr()[i];
        CHECK(y0 == doctest::Approx(ramp).epsilon(1e-4));
        CHECK(y1 == doctest::Approx(ramp * (1.f + 5.f)).epsilon(1e-4));
        largest0 = std::fmax(largest0, std::fabs(y0 - previous0));
        largest1 = std::fmax(largest1, std::fabs(y1 - previous1));
        previous0 = y0;
        previous1 = y1;
      }
    }
    CHECK(largest0 == doctest::Approx(1.f / 240.f).epsilon(1e-3));
    CHECK(largest1 == doctest::Approx(6.f / 240.f).epsilon(1e-3));
    dst.Calculate(YSE::T_DSP);
    CHECK(all(0, 1.f));
    CHECK(all(1, 6.f));

    // A hard switch with ramp 0, and clear.
    REQUIRE(dst.PassData("ramp 0", "mx1134", YSE::T_GUI));
    REQUIRE(dst.PassData("disconnect 0 1", "mx1134", YSE::T_GUI));
    dst.Calculate(YSE::T_DSP);
    CHECK(all(0, 1.f));
    CHECK(all(1, 5.f));
    REQUIRE(dst.PassData("clear", "mx1134", YSE::T_GUI));
    dst.Calculate(YSE::T_DSP);
    CHECK(all(0, 0.f));
    CHECK(all(1, 0.f));
  }

  TEST_CASE("~matrix: a dump in a rendered patch configures a second matrix (#1134)") {
    // ~sig 3 -> [~matrix 1 2] (A, driven by a receive), whose dump outlet
    // feeds [~matrix 1 2] (B). A's outputs go nowhere; B's go to the dac.
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* s = src.CreateObject(YSE::OBJ::D_SIG, "3");
      YSE::pHandle* a = src.CreateObject(YSE::OBJ::D_MATRIX, "1 2 1 ramp 0");
      YSE::pHandle* b = src.CreateObject(YSE::OBJ::D_MATRIX, "1 2 1 ramp 0");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "mxa1134");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(s != nullptr);
      REQUIRE(a != nullptr);
      REQUIRE(b != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(s, 0, a, 0);
      src.Connect(s, 0, b, 0);
      src.Connect(cmd, 0, a, 0);
      src.Connect(a, 2, b, 0);
      src.Connect(b, 0, dac, 0);
      src.Connect(b, 1, dac, 1);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    dst.Calculate(YSE::T_DSP);
    CHECK(dst.output[0].isSilent());
    CHECK(dst.output[1].isSilent());

    REQUIRE(dst.PassData("0 1 0.5", "mxa1134", YSE::T_GUI));
    REQUIRE(dst.PassData("dumptarget", "mxa1134", YSE::T_GUI));
    // The dump is sent after this block; B uses it from the next one.
    dst.Calculate(YSE::T_DSP);
    dst.Calculate(YSE::T_DSP);
    for (unsigned int i = 0; i < N; ++i) {
      CHECK(dst.output[0].getPtr()[i] == 0.f);
      CHECK(dst.output[1].getPtr()[i] == doctest::Approx(1.5f));
    }
  }

  TEST_CASE("~matrix: messages, blocks and dumps on the audio thread allocate nothing "
            "(#1134)") {
    CountSink counted;
    BufferSink out0;
    dMatrix obj;
    obj.SetParams("4 4 1 ramp 3");
    TestHelpers::Wire(obj, 0, out0, 0);
    TestHelpers::Wire(obj, 4, counted, 0);
    buffer in(N);
    in = 0.5f;
    const std::vector<buffer*> ins = {&in, nullptr, &in, &in};
    const std::string list = "0 1 0.5";
    const std::string list4 = "2 3 0.25 7";
    const std::string connect = "connect 1 0 2 3 0.75";
    const std::string disconnect = "disconnect 1 2";
    const std::string exclusive = "exclusive 3";
    const std::string ramp = "ramp 1";
    const std::string clear = "clear";
    const std::string dumpCurrent = "dump";
    const std::string dumpTarget = "dumptarget";
    Block(obj, ins);
    {
      TestHelpers::ProbeScope probe;
      auto* control = obj.GetInlet(0);
      control->SetList(list, YSE::T_DSP);
      control->SetList(list4, YSE::T_DSP);
      control->SetList(connect, YSE::T_DSP);
      control->SetList(disconnect, YSE::T_DSP);
      control->SetList(dumpCurrent, YSE::T_DSP);
      Block(obj, ins);
      control->SetList(exclusive, YSE::T_DSP);
      control->SetList(ramp, YSE::T_DSP);
      control->SetList(connect, YSE::T_DSP);
      control->SetList(dumpTarget, YSE::T_DSP);
      Block(obj, ins);
      control->SetList(clear, YSE::T_DSP);
      Block(obj, ins);
      Block(obj, ins);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(counted.count > 0);
  }

} // TEST_SUITE("patcher")
