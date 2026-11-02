// Tests for the ~capture patcher object (patcher/analysis/dCapture), issue
// #1128. The store is DSP::sampleCapture's and is tested in
// Tests/dsp/test_sample_capture.cpp; these show that the signal, the
// numbers, the arguments and the messages reach it, that a dump goes out
// only through the flush after the block (#1037), count first, as lists an
// AtomList holds, spread over blocks when large, that 'write' goes through
// the file scheduler, and that it works in a rendered patch driven from a
// message box and a receive after a JSON round trip.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pAtomList.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/analysis/dCapture.h"
#include "patcher/io/fileScheduler.h"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::ScopedSampleRate;
using TestHelpers::Wire;
using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::PATCHER::AtomList;
using YSE::PATCHER::dCapture;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // Every message that arrives, in order, as "i 60" or "l text".
  struct Log : TestHelpers::SinkBase {
    std::vector<std::string> got;
    Log() {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterInt(
          [this](int v, int, YSE::THREAD) { got.push_back("i " + std::to_string(v)); });
      inputs.back().RegisterList(
          [this](const std::string& v, int, YSE::THREAD) { got.push_back("l " + v); });
    }
    const char* Type() const override {
      return "capture_log";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // What a dump sent: the count, then the values of every list, in order.
  struct Dump {
    int count = -1;
    std::vector<float> values;
    std::vector<std::size_t> listSizes;
    std::size_t longestText = 0;
  };

  Dump Read(const std::vector<std::string>& got) {
    Dump d;
    for (const std::string& item : got) {
      if (item.rfind("i ", 0) == 0) {
        CHECK(d.count == -1); // one count, before the lists
        CHECK(d.values.empty());
        d.count = std::stoi(item.substr(2));
        continue;
      }
      REQUIRE(item.rfind("l ", 0) == 0);
      const std::string text = item.substr(2);
      d.longestText = std::max(d.longestText, text.size());
      std::istringstream in(text);
      std::string token;
      std::size_t size = 0;
      while (in >> token) {
        d.values.push_back(std::stof(token));
        size++;
      }
      d.listSizes.push_back(size);
    }
    return d;
  }

  // ~capture with a log on both outlets, into one log so the order between
  // them shows. Block() runs one block and the flush the patcher runs after
  // its traversal.
  struct Rig {
    Log log;
    dCapture cap;

    Rig() {
      Wire(cap, 0, log);
      Wire(cap, 1, log);
    }
    void Block(buffer& in) {
      cap.ResetDSP();
      cap.GetInlet(0)->SetBuffer(&in, YSE::T_DSP); // runs CALC()
      cap.FlushArmedOutlets(YSE::T_GUI);
    }
    // One block with nothing connected: the inlet reads its number.
    void Block() {
      cap.ResetDSP();
      cap.Calculate(YSE::T_DSP);
      cap.FlushArmedOutlets(YSE::T_GUI);
    }
    void Send(const std::string& message) {
      cap.GetInlet(0)->SetMessage(message, YSE::T_GUI);
    }
    Dump Take() {
      Dump d = Read(log.got);
      log.got.clear();
      return d;
    }
  };

  // Block `b` of a ramp: sample n of the whole stream is n * 0.5.
  void RampBlock(buffer& in, unsigned int b) {
    for (unsigned int i = 0; i < N; ++i)
      in.getPtr()[i] = static_cast<float>(b * N + i) * 0.5f;
  }

  std::vector<float> RampValues(unsigned int from, unsigned int to) {
    std::vector<float> out;
    for (unsigned int n = from; n < to; ++n)
      out.push_back(static_cast<float>(n) * 0.5f);
    return out;
  }

  std::string TempFile(const char* name) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return path.string();
  }

  std::string ReadWholeFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~capture: registry and shape (#1128)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_CAPTURE);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~capture");
    CHECK(obj->GetInputs() == 1);
    CHECK(obj->GetOutputs() == 2);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::LIST);
    CHECK(obj->OutputDataType(1) == YSE::OUT_TYPE::INT);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_CAPTURE));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::ANALYSIS);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~capture: a dump sends the count, then the values oldest first, only from the "
            "flush (#1128, #1037)") {
    Rig rig;
    buffer in(N);
    RampBlock(in, 0);
    rig.Block(in);
    CHECK(rig.log.got.empty()); // nothing without a dump

    rig.Send("dump");
    CHECK(rig.log.got.empty()); // a dump only asks
    RampBlock(in, 1);
    rig.cap.ResetDSP();
    rig.cap.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
    CHECK(rig.log.got.empty()); // CALC() only arms
    rig.cap.FlushArmedOutlets(YSE::T_GUI);
    // The store as it is at the end of the block the dump arrived before.
    Dump d = rig.Take();
    CHECK(d.count == static_cast<int>(2 * N));
    CHECK(d.values == RampValues(0, 2 * N));

    // A bang dumps as well; the store keeps recording.
    rig.cap.GetInlet(0)->SetBang(YSE::T_GUI);
    RampBlock(in, 2);
    rig.Block(in);
    d = rig.Take();
    CHECK(d.count == static_cast<int>(3 * N));
    CHECK(d.values == RampValues(0, 3 * N));

    // An empty store sends its count of 0 and no list: a block position past
    // the block's end stores nothing.
    Rig empty;
    empty.cap.SetParams("10 500");
    empty.Send("dump");
    empty.Block(in);
    CHECK(empty.log.got == std::vector<std::string>{"i 0"});
  }

  TEST_CASE("~capture: by default the most recent 4096 samples, with 'f' the first; 'clear' "
            "applies before the next block (#1128)") {
    Rig last;
    Rig first;
    first.cap.SetParams("f");
    CHECK(first.cap.GetParams() == "f");
    buffer in(N);
    const unsigned int blocks = 4096 / N + 3;
    for (unsigned int b = 0; b < blocks; ++b) {
      RampBlock(in, b);
      last.Block(in);
      first.Block(in);
    }
    last.Send("dump");
    first.Send("dump");
    in = 0.f;
    last.Block(in);
    first.Block(in);
    Dump d = last.Take();
    CHECK(d.count == 4096);
    std::vector<float> want = RampValues(blocks * N + N - 4096, blocks * N);
    want.insert(want.end(), N, 0.f);
    CHECK(d.values == want);
    d = first.Take();
    CHECK(d.count == 4096);
    CHECK(d.values == RampValues(0, 4096));

    // 'clear' and 'dump' in the same block: emptied, then this block stored.
    first.Send("clear");
    first.Send("dump");
    RampBlock(in, 0);
    first.Block(in);
    d = first.Take();
    CHECK(d.count == static_cast<int>(N));
    CHECK(d.values == RampValues(0, N));
  }

  TEST_CASE("~capture: arguments set the capacity and the block positions (#1128)") {
    Rig rig;
    rig.cap.SetParams("10 0 64 64");
    CHECK(rig.cap.GetParams() == "10 0 64 64");
    CHECK(rig.cap.Store().capacity() == 10);
    CHECK(rig.cap.Store().mode() == YSE::DSP::sampleCapture::LAST);
    CHECK(rig.cap.Store().indexCount() == 2);
    buffer in(N);
    for (unsigned int b = 0; b < 8; ++b) {
      RampBlock(in, b);
      rig.Block(in);
    }
    rig.Send("dump");
    RampBlock(in, 8);
    rig.Block(in);
    std::vector<float> want;
    for (unsigned int b = 4; b < 9; ++b) {
      want.push_back(static_cast<float>(b * N) * 0.5f);
      want.push_back(static_cast<float>(b * N + 64) * 0.5f);
    }
    CHECK(rig.Take().values == want);

    // 'f' anywhere, words ignored, the capacity clamped, at most ten positions.
    rig.cap.SetParams("x 0 f 1 2 3 4 5 6 7 8 9 10 11");
    CHECK(rig.cap.Store().mode() == YSE::DSP::sampleCapture::FIRST);
    CHECK(rig.cap.Store().capacity() == 1);
    CHECK(rig.cap.Store().indexCount() == YSE::DSP::sampleCapture::MAX_INDICES);
    rig.cap.SetParams("99999999");
    CHECK(rig.cap.Store().capacity() == YSE::DSP::sampleCapture::MAXIMUM_CAPACITY);
    // Back to no arguments: the defaults.
    rig.cap.SetParams("");
    CHECK(rig.cap.Store().capacity() == YSE::DSP::sampleCapture::DEFAULT_CAPACITY);
    CHECK(rig.cap.Store().mode() == YSE::DSP::sampleCapture::LAST);
    CHECK(rig.cap.Store().indexCount() == 0);
  }

  TEST_CASE("~capture: an unconnected inlet stores its number (#1128, #1036)") {
    Rig rig;
    rig.cap.SetParams("4 0 1"); // two samples a block
    rig.cap.GetInlet(0)->SetFloat(0.25f, YSE::T_GUI);
    rig.Block();
    rig.cap.GetInlet(0)->SetInt(-3, YSE::T_GUI);
    rig.Send("dump");
    rig.Block();
    CHECK(rig.Take().values == std::vector<float>{0.25f, 0.25f, -3.f, -3.f});
  }

  TEST_CASE("~capture: lists stay within what an AtomList holds (#1128)") {
    // Short values: the atom bound.
    Rig zeros;
    zeros.cap.SetParams("1000");
    buffer in(N);
    in = 0.f;
    for (unsigned int b = 0; b < 1000 / N + 1; ++b)
      zeros.Block(in);
    zeros.Send("dump");
    zeros.Block(in);
    Dump d = zeros.Take();
    CHECK(d.count == 1000);
    CHECK(d.values.size() == 1000);
    REQUIRE(d.listSizes.size() == 4);
    CHECK(d.listSizes[0] == AtomList::MAX_ATOMS);
    CHECK(d.listSizes[3] == 1000 - 3 * AtomList::MAX_ATOMS);

    // Long values: the text bound.
    Rig thirds;
    thirds.cap.SetParams("1000");
    for (unsigned int i = 0; i < N; ++i)
      in.getPtr()[i] = -static_cast<float>(i + 1) / 3.f;
    for (unsigned int b = 0; b < 1000 / N + 1; ++b)
      thirds.Block(in);
    thirds.Send("dump");
    thirds.Block(in);
    d = thirds.Take();
    CHECK(d.values.size() == 1000);
    CHECK(d.listSizes.size() > 4);
    CHECK(d.longestText <= AtomList::TEXT_CAPACITY);
    // The last 1000 of the blocks so far, read back exactly: the formatting
    // round-trips.
    const std::size_t start = (1000 / N + 2) * N - 1000;
    for (std::size_t n = 0; n < 1000; ++n) {
      CAPTURE(n);
      CHECK(d.values[n] == -static_cast<float>((start + n) % N + 1) / 3.f);
    }
  }

  TEST_CASE("~capture: a large store is dumped over several blocks while recording goes on "
            "(#1128)") {
    Rig rig;
    rig.cap.SetParams("f 10000");
    buffer in(N);
    unsigned int b = 0;
    for (; b * N < 10000; ++b) {
      RampBlock(in, b);
      rig.Block(in);
    }
    rig.Send("clear"); // the snapshot is not touched by later blocks
    rig.Send("dump");
    rig.Block(in); // cleared, one block stored, then dumped: N values
    CHECK(rig.Take().count == static_cast<int>(N));

    for (; b < 200; ++b) {
      RampBlock(in, b);
      rig.Block(in);
    }
    REQUIRE(rig.cap.Store().size() == 10000);
    rig.Send("dump");
    rig.Block(in);
    Dump d = rig.Take();
    CHECK(d.count == 10000);
    CHECK(d.values.size() == dCapture::DUMP_VALUES_PER_BLOCK);
    rig.Send("dump"); // waits for the one in progress
    rig.Send("clear");
    rig.Block(in);
    d = rig.Take();
    CHECK(d.count == -1); // the same dump goes on
    CHECK(d.values.size() == dCapture::DUMP_VALUES_PER_BLOCK);
    rig.Block(in);
    d = rig.Take();
    CHECK(d.values.size() == 10000 - 2 * dCapture::DUMP_VALUES_PER_BLOCK);
    rig.Block(in); // now the waiting dump: the store since the clear
    d = rig.Take();
    CHECK(d.count == static_cast<int>(3 * N));
  }

  TEST_CASE("~capture: unknown messages change nothing; write without a patcher is refused "
            "(#1128)") {
    Rig rig;
    buffer in(N);
    in = 0.5f;
    rig.Block(in);
    rig.Send("dumps");
    rig.Send("open");
    rig.Send("wclose");
    rig.cap.SetMessage("", 0.f);
    rig.Block(in);
    CHECK(rig.log.got.empty());
    CHECK(rig.cap.Store().size() == 2 * N);
    rig.Send("write /tmp/never.txt"); // standalone: no file scheduler
    CHECK(rig.cap.Dropped() == 1);
  }

  TEST_CASE("~capture: write goes through the file scheduler, one value per line (#1128)") {
    const std::string path = TempFile("yse_capture_1128.txt");
    patcherImplementation p(1, nullptr);
    YSE::pHandle* cap = p.CreateObject(YSE::OBJ::D_CAPTURE, "3");
    YSE::pHandle* source = p.CreateObject(YSE::OBJ::D_SIG, "0.25");
    REQUIRE(cap != nullptr);
    REQUIRE(source != nullptr);
    p.Connect(source, 0, cap, 0);
    p.Calculate(YSE::T_DSP);
    cap->SetListData(0, "write " + path);
    // A second write before the first is taken is refused.
    cap->SetListData(0, "write " + path);
    cap->SetListData(0, "write");
    REQUIRE(p.FileIO() != nullptr);
    p.Calculate(YSE::T_DSP); // the write is built after this block
    p.FileIO()->WaitIdle();
    CHECK(ReadWholeFile(path) == "0.25\n0.25\n0.25\n");
    std::error_code ec;
    std::filesystem::remove(path, ec);
  }

  TEST_CASE("~capture: the arguments survive a DumpJSON / ParseJSON round trip (#1128)") {
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* a = src.CreateObject(YSE::OBJ::D_CAPTURE, "f 1000 0 32");
      REQUIRE(a != nullptr);
      YSE::pHandle* b = src.CreateObject(YSE::OBJ::D_CAPTURE, "");
      REQUIRE(b != nullptr);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* a = dst.GetHandleFromID(0);
    YSE::pHandle* b = dst.GetHandleFromID(1);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(std::string(a->Type()) == "~capture");
    CHECK(a->GetParams() == "f 1000 0 32");
    CHECK(b->GetParams() == "");
    CHECK(a->GetInputs() == 1);
    CHECK(a->GetOutputs() == 2);
  }

  TEST_CASE("~capture: in a rendered patch, a ~phasor's first samples at two block positions, "
            "dumped from a message box and cleared through a receive after a JSON round trip "
            "(#1128)") {
    ScopedSampleRate rate(48000);
    std::string dump;
    unsigned int dumpID = 0;
    unsigned int capID = 0;
    {
      patcherImplementation src(1, nullptr);
      // 300 Hz at 48 kHz: not a whole cycle per block, so each block's
      // samples differ from the last one's.
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "300");
      YSE::pHandle* cap = src.CreateObject(YSE::OBJ::D_CAPTURE, "f 6 0 64");
      YSE::pHandle* message = src.CreateObject(YSE::OBJ::G_MESSAGE, "dump");
      YSE::pHandle* control = src.CreateObject(YSE::OBJ::G_RECEIVE, "cap1128");
      REQUIRE(ramp != nullptr);
      REQUIRE(cap != nullptr);
      REQUIRE(message != nullptr);
      REQUIRE(control != nullptr);
      src.Connect(ramp, 0, cap, 0);
      src.Connect(message, 0, cap, 0);
      src.Connect(control, 0, cap, 0);
      dumpID = message->GetID();
      capID = cap->GetID();
      dump = src.DumpJSON();
    }
    Log log;
    YSE::pHandle logHandle(&log);
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* message = dst.GetHandleFromID(dumpID);
    YSE::pHandle* cap = dst.GetHandleFromID(capID);
    REQUIRE(message != nullptr);
    REQUIRE(cap != nullptr);
    CHECK(cap->GetParams() == "f 6 0 64");
    dst.Connect(cap, 0, &logHandle, 0);
    dst.Connect(cap, 1, &logHandle, 0);

    // The reference: the same ramp, at positions 0 and 64 of each block.
    phasor rampRef;
    std::vector<float> want;
    auto run = [&](int blocks) {
      for (int b = 0; b < blocks; ++b) {
        dst.Calculate(YSE::T_DSP);
        buffer& r = rampRef(300.f, N);
        want.push_back(r.getPtr()[0]);
        want.push_back(r.getPtr()[64]);
      }
    };
    run(5);
    CHECK(log.got.empty());
    message->SetBang(0);
    run(1);
    Dump d = Read(log.got);
    log.got.clear();
    CHECK(d.count == 6);
    REQUIRE(d.values.size() == 6);
    for (std::size_t i = 0; i < 6; ++i) {
      CAPTURE(i);
      CHECK(d.values[i] == doctest::Approx(want[i]).epsilon(1e-6));
    }
    CHECK(d.values[1] > d.values[0]); // later in the cycle

    // 'clear' through the receive: the next six from there.
    REQUIRE(dst.PassData(std::string("clear"), "cap1128", YSE::T_GUI));
    want.clear();
    run(3);
    message->SetBang(0);
    run(1);
    d = Read(log.got);
    log.got.clear();
    CHECK(d.count == 6);
    REQUIRE(d.values.size() == 6);
    for (std::size_t i = 0; i < 6; ++i) {
      CAPTURE(i);
      CHECK(d.values[i] == doctest::Approx(want[i]).epsilon(1e-6));
    }
    dst.Disconnect(cap, 0, &logHandle, 0);
    dst.Disconnect(cap, 1, &logHandle, 0);
  }

  TEST_CASE("~capture: numbers, messages, blocks and the flush on the audio thread allocate "
            "nothing (#1128)") {
    // Unwired: the sends still run, with no sink of its own to allocate. The
    // messages are built before the probe.
    dCapture cap;
    cap.SetParams("f 9000 0 3");
    cap.SetParams("9000");
    const std::string dumpMessage = "dump";
    const std::string clearMessage = "clear";
    const std::string writeMessage = "write somewhere.txt";
    buffer in(N);
    for (unsigned int i = 0; i < N; ++i)
      in.getPtr()[i] = static_cast<float>(i) / 7.f;
    auto block = [&cap, &in]() {
      cap.ResetDSP();
      cap.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      cap.FlushArmedOutlets(YSE::T_GUI);
    };
    block();
    {
      TestHelpers::ProbeScope probe;
      cap.GetInlet(0)->SetFloat(0.5f, YSE::T_DSP);
      cap.GetInlet(0)->SetInt(2, YSE::T_DSP);
      for (int b = 0; b < 80; ++b)
        block(); // wraps the store
      cap.GetInlet(0)->SetBang(YSE::T_DSP);
      cap.GetInlet(0)->SetMessage(dumpMessage, YSE::T_DSP);
      for (int b = 0; b < 4; ++b)
        block(); // a dump over three blocks
      cap.GetInlet(0)->SetMessage(clearMessage, YSE::T_DSP);
      cap.GetInlet(0)->SetMessage(writeMessage, YSE::T_DSP); // refused: no patcher
      block();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(cap.Dropped() == 1);
    // A message on T_DSP runs the block again, so the clear is followed by
    // three stored blocks.
    CHECK(cap.Store().size() == 3 * N);
  }

} // TEST_SUITE("patcher")
