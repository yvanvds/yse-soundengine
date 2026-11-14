// Tests for ~frame and ~framesnap (patcher/spectral/dFrame), issue #1163.
// The frame math is DSP::frameSource / DSP::frameSnapshot's and is tested in
// Tests/dsp/test_frame_list.cpp; these show:
//  - outside a ~pfft a block is a frame: ~frame bit-exact against the class
//    with lists, numbers, the offset inlet, fill and the capacity; ~framesnap
//    reports a block on a bang, start and the interval, sent only from the
//    flush after the block (#1037), each list after the bin it starts at;
//  - inside a ~pfft new values wait for the start of a frame, and a large
//    frame goes out as lists an AtomList holds, over several blocks;
//  - in rendered ~pfft patches, bin by bin across passes: [.m list] ->
//    ~frame -> ~fftout bit-exact against DSP::stft with those values at a
//    frame of four passes, a hop below the block, and a full spectrum
//    mirrored; ~fftin -> ~framesnap reports DSP::stft's frame, exact through
//    the text, on a bang and on an interval; ~framesnap -> ~frame -> ~fftout
//    freezes a spectrum, bit-exact, after a JSON round trip;
//  - the arguments survive a JSON round trip, and messages, blocks and the
//    flush allocate nothing.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include "dsp/buffer.hpp"
#include "dsp/spectral/frameList.hpp"
#include "dsp/spectral/stft.hpp"
#include "headers/constants.hpp"
#include "patcher/pAtomList.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/spectral/dFrame.h"
#include "patcher/spectral/spectralPass.h"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using TestHelpers::Wire;
using YSE::STANDARD_BUFFERSIZE;
using YSE::DSP::buffer;
using YSE::DSP::frameSource;
using YSE::DSP::stft;
using YSE::DSP::stftFrame;
using YSE::PATCHER::AtomList;
using YSE::PATCHER::dFrame;
using YSE::PATCHER::dFramesnap;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = STANDARD_BUFFERSIZE;

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
      return "framesnap_log";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // What reports sent: every list's first bin and values, in order.
  struct Lists {
    std::vector<std::pair<int, std::vector<float>>> lists;
    std::size_t longestText = 0;

    // The values of the lists from the one at `from` on, as one frame: each
    // list must start where the one before ended.
    std::vector<float> Frame(std::size_t from = 0) const {
      std::vector<float> v;
      for (std::size_t i = from; i < lists.size(); ++i) {
        CHECK(lists[i].first == static_cast<int>(v.size()));
        v.insert(v.end(), lists[i].second.begin(), lists[i].second.end());
      }
      return v;
    }
  };

  Lists Read(const std::vector<std::string>& got) {
    Lists l;
    bool offset = false;
    for (const std::string& item : got) {
      if (item.rfind("i ", 0) == 0) {
        CHECK(!offset); // one offset per list, before it
        offset = true;
        l.lists.emplace_back(std::stoi(item.substr(2)), std::vector<float>());
        continue;
      }
      REQUIRE(item.rfind("l ", 0) == 0);
      REQUIRE(offset);
      offset = false;
      const std::string text = item.substr(2);
      l.longestText = std::max(l.longestText, text.size());
      std::istringstream in(text);
      std::string token;
      while (in >> token)
        l.lists.back().second.push_back(std::stof(token));
    }
    CHECK(!offset);
    return l;
  }

  // ~framesnap with a log on both outlets, into one log so the order shows.
  // Block() runs one block and the flush the patcher runs after its
  // traversal.
  struct SnapRig {
    Log log;
    dFramesnap snap;

    SnapRig() {
      Wire(snap, 0, log);
      Wire(snap, 1, log);
    }
    void Block(buffer& in) {
      snap.ResetDSP();
      snap.GetInlet(0)->SetBuffer(&in, YSE::T_DSP); // runs CALC()
      snap.FlushArmedOutlets(YSE::T_GUI);
    }
    void Send(const std::string& message) {
      snap.GetInlet(0)->SetMessage(message, YSE::T_GUI);
    }
    Lists Take() {
      Lists l = Read(log.got);
      log.got.clear();
      return l;
    }
  };

  // One block of ~frame, unconnected, as the patcher runs it.
  buffer& Step(dFrame& obj, BufferSink& out) {
    obj.ResetDSP();
    obj.Calculate(YSE::T_DSP);
    REQUIRE(out.received != nullptr);
    return *out.received;
  }

  void Ramp(buffer& b, float from) {
    for (unsigned int i = 0; i < b.getLength(); ++i)
      b.getPtr()[i] = from + 0.5f * static_cast<float>(i);
  }

  // Values exactly representable in the text a list is written in.
  float Value(UInt k, UInt seed) {
    return static_cast<float>((k * 7 + seed * 3) % 23) * 0.125f - 1.f;
  }

  std::string ListText(const std::vector<float>& v) {
    std::string s;
    for (float x : v) {
      if (!s.empty()) s += ' ';
      s += std::to_string(x);
    }
    return s;
  }

  // A rich signal: band content plus deterministic noise.
  float Rich(std::size_t n) {
    unsigned int s = static_cast<unsigned int>(n) * 2654435761u + 12345u;
    s ^= s >> 13;
    s *= 2246822519u;
    s ^= s >> 16;
    return 0.5f * static_cast<float>(std::sin(0.07 * static_cast<double>(n))) +
           0.3f * (static_cast<float>(s >> 8) / 16777216.f - 0.5f);
  }

  // One block of Rich through the patcher as an insert.
  std::vector<float> RenderBlock(patcherImplementation& p, std::size_t& at) {
    MULTICHANNELBUFFER io(1);
    io[0].resize(N);
    for (unsigned int i = 0; i < N; ++i)
      io[0].getPtr()[i] = Rich(at + i);
    p.ProcessAsInsert(io);
    at += N;
    return std::vector<float>(io[0].getPtr(), io[0].getPtr() + N);
  }

  std::vector<float> RichBlock(std::size_t at) {
    std::vector<float> v(N);
    for (unsigned int i = 0; i < N; ++i)
      v[i] = Rich(at + i);
    return v;
  }

  std::size_t Differences(const std::vector<float>& a, const std::vector<float>& b) {
    REQUIRE(a.size() == b.size());
    std::size_t differ = 0;
    for (std::size_t n = 0; n < a.size(); ++n)
      if (a[n] != b[n]) differ++;
    return differ;
  }

  YSE::PATCHER::spectralPass Pass(UInt size, UInt overlap) {
    YSE::PATCHER::spectralPass pass;
    pass.fftSize = size;
    pass.bins = size / 2;
    pass.hop = size / overlap;
    pass.overlap = overlap;
    pass.count = std::min(pass.bins, N);
    pass.passes = pass.bins / pass.count;
    return pass;
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~frame / ~framesnap: registry, category and shape (#1163)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* frame = p.CreateObject(YSE::OBJ::D_FRAME);
    YSE::pHandle* snap = p.CreateObject(YSE::OBJ::D_FRAMESNAP);
    REQUIRE(frame != nullptr);
    REQUIRE(snap != nullptr);
    CHECK(std::string(YSE::OBJ::D_FRAME) == "~frame");
    CHECK(std::string(YSE::OBJ::D_FRAMESNAP) == "~framesnap");
    CHECK(frame->GetInputs() == 2);
    CHECK(frame->GetOutputs() == 1);
    CHECK_FALSE(frame->IsDSPInput(0));
    CHECK_FALSE(frame->IsDSPInput(1));
    CHECK(frame->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(snap->GetInputs() == 2);
    CHECK(snap->GetOutputs() == 2);
    CHECK(snap->IsDSPInput(0));
    CHECK_FALSE(snap->IsDSPInput(1));
    CHECK(snap->OutputDataType(0) == YSE::OUT_TYPE::LIST);
    CHECK(snap->OutputDataType(1) == YSE::OUT_TYPE::INT);
    for (const char* type : {YSE::OBJ::D_FRAME, YSE::OBJ::D_FRAMESNAP}) {
      CAPTURE(type);
      std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(type));
      REQUIRE(o != nullptr);
      CHECK(o->GetCategory() == YSE::PATCHER::pCategory::SPECTRAL);
      CHECK(o->IsDSPObject());
    }
    std::unique_ptr<YSE::PATCHER::pObject> f(YSE::PATCHER::Register().Get(YSE::OBJ::D_FRAME));
    std::unique_ptr<YSE::PATCHER::pObject> s(YSE::PATCHER::Register().Get(YSE::OBJ::D_FRAMESNAP));
    CHECK(f->GetParamCount() == 1);
    CHECK(s->GetParamCount() == 2);
  }

  TEST_CASE("~frame: outside a ~pfft a block is the list, against DSP::frameSource; numbers, "
            "the offset, fill and the capacity (#1163)") {
    BufferSink out;
    dFrame frame;
    Wire(frame, 0, out);
    frameSource ref;
    // Nothing yet: 0.
    buffer& silent = Step(frame, out);
    CHECK(silent.getLength() == N);
    CHECK(silent.getPtr()[0] == 0.f);

    std::vector<float> v(150);
    for (UInt k = 0; k < 150; ++k)
      v[k] = Value(k, 1);
    frame.GetInlet(0)->SetList(ListText(v), YSE::T_GUI);
    ref.set(v.data(), 150);
    CHECK(TestHelpers::buffersNearlyEqual(Step(frame, out), ref(), 0.f));
    CHECK(out.received->getPtr()[127] == v[127]);
    // Every block the same.
    CHECK(TestHelpers::buffersNearlyEqual(Step(frame, out), ref(), 0.f));

    // A number is a list of one; fill sets the rest.
    frame.GetInlet(0)->SetFloat(0.75f, YSE::T_GUI);
    frame.GetInlet(0)->SetMessage("fill 0.5", YSE::T_GUI);
    buffer& one = Step(frame, out);
    CHECK(one.getPtr()[0] == 0.75f);
    CHECK(one.getPtr()[1] == 0.5f);
    CHECK(one.getPtr()[N - 1] == 0.5f);
    frame.GetInlet(0)->SetInt(-2, YSE::T_GUI);
    CHECK(Step(frame, out).getPtr()[0] == -2.f);

    // The right inlet: where the next lists start; the bins before stay.
    frame.GetInlet(1)->SetInt(10, YSE::T_GUI);
    frame.GetInlet(0)->SetList("1 2 3", YSE::T_GUI);
    buffer& shifted = Step(frame, out);
    CHECK(shifted.getPtr()[0] == -2.f);
    CHECK(shifted.getPtr()[1] == 0.5f); // skipped: the fill value
    CHECK(shifted.getPtr()[9] == 0.5f);
    CHECK(shifted.getPtr()[10] == 1.f);
    CHECK(shifted.getPtr()[12] == 3.f);
    CHECK(shifted.getPtr()[13] == 0.5f);
    // A word in a list counts as 0; a word-first message is not a list.
    frame.GetInlet(1)->SetFloat(0.f, YSE::T_GUI);
    frame.GetInlet(0)->SetList("4 x 6", YSE::T_GUI);
    frame.GetInlet(0)->SetMessage("bogus 7", YSE::T_GUI);
    frame.GetInlet(0)->SetMessage("fill", YSE::T_GUI);
    buffer& words = Step(frame, out);
    CHECK(words.getPtr()[0] == 4.f);
    CHECK(words.getPtr()[1] == 0.f);
    CHECK(words.getPtr()[2] == 6.f);
    CHECK(words.getPtr()[3] == 0.5f);
    CHECK(frame.Source().length() == 3);
    // Outside a full-spectrum ~pfft, mirror changes nothing.
    frame.GetInlet(0)->SetMessage("mirror 1", YSE::T_GUI);
    CHECK(Step(frame, out).getPtr()[N - 1] == 0.5f);
    CHECK(frame.Source().mirror());

    // ~frame 64: values past 64 are not kept.
    BufferSink out2;
    dFrame small;
    Wire(small, 0, out2);
    small.SetParams("64");
    CHECK(small.GetParams() == "64");
    CHECK(small.Source().capacity() == 64);
    small.GetInlet(0)->SetList(ListText(v), YSE::T_GUI);
    buffer& cut = Step(small, out2);
    CHECK(cut.getPtr()[63] == v[63]);
    CHECK(cut.getPtr()[64] == 0.f);
    small.SetParams("");
    CHECK(small.Source().capacity() == frameSource::DEFAULT_CAPACITY);
  }

  TEST_CASE("~frame: inside a ~pfft new values wait for the start of a frame (#1163)") {
    BufferSink out;
    dFrame frame;
    Wire(frame, 0, out);
    std::vector<float> a(256, 1.f);
    std::vector<float> b(256, 2.f);
    YSE::PATCHER::spectralPass pass = Pass(512, 4);
    const auto run = [&](UInt p) {
      pass.pass = p;
      pass.firstBin = p * N;
      const YSE::PATCHER::spectralPassScope scope(&pass);
      buffer& o = Step(frame, out);
      REQUIRE(o.getLength() == N);
      return o.getPtr()[N - 1];
    };
    frame.GetInlet(0)->SetList(ListText(a), YSE::T_GUI);
    CHECK(run(0) == 1.f);
    // Arriving mid-frame: the frame's second pass keeps the old values ...
    frame.GetInlet(0)->SetList(ListText(b), YSE::T_GUI);
    CHECK(run(1) == 1.f);
    // ... and the next frame has the new ones, every pass of it.
    CHECK(run(0) == 2.f);
    CHECK(run(1) == 2.f);
    // Bin firstBin + i: a ramp shows where each pass starts.
    std::vector<float> ramp(256);
    for (UInt k = 0; k < 256; ++k)
      ramp[k] = static_cast<float>(k);
    frame.GetInlet(0)->SetList(ListText(ramp), YSE::T_GUI);
    CHECK(run(0) == 127.f);
    CHECK(run(1) == 255.f);
    CHECK(YSE::PATCHER::CurrentSpectralPass() == nullptr);
  }

  TEST_CASE("~framesnap: outside a ~pfft a bang reports the block after it, from the flush "
            "(#1163)") {
    SnapRig rig;
    buffer in(N);
    Ramp(in, 0.f);
    rig.Block(in);
    CHECK(rig.log.got.empty()); // no interval: nothing on its own

    // A bang sends nothing itself; the next block's flush reports it, the
    // bin first.
    rig.snap.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.log.got.empty());
    Ramp(in, 10.f);
    rig.snap.ResetDSP();
    rig.snap.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
    CHECK(rig.log.got.empty()); // CALC() sends nothing
    rig.snap.FlushArmedOutlets(YSE::T_GUI);
    REQUIRE(rig.log.got.size() == 2);
    CHECK(rig.log.got[0] == "i 0");
    Lists l = rig.Take();
    std::vector<float> got = l.Frame();
    REQUIRE(got.size() == N);
    for (UInt i = 0; i < N; ++i)
      REQUIRE(got[i] == in.getPtr()[i]);
    // Once.
    rig.Block(in);
    CHECK(rig.log.got.empty());

    // start with no interval is a bang; stop, a number 0 and active 0 stop
    // nothing that is not running.
    rig.Send("start");
    Ramp(in, 20.f);
    rig.Block(in);
    got = rig.Take().Frame();
    REQUIRE(got.size() == N);
    CHECK(got[0] == 20.f);
    rig.Block(in);
    CHECK(rig.log.got.empty());
    CHECK(rig.snap.Snapshot().frames() == 5);
  }

  TEST_CASE("~framesnap: interval reporting, from the argument, the messages and the right "
            "inlet, following the sample rate (#1163)") {
    TestHelpers::ScopedSampleRate rate(48000);
    buffer in(N);
    // Reports in 24 blocks: which blocks reported, by their first value.
    const auto reports = [&in](SnapRig& rig) {
      std::vector<int> blocks;
      for (int b = 0; b < 24; ++b) {
        Ramp(in, static_cast<float>(b));
        rig.Block(in);
        const Lists l = rig.Take();
        if (l.lists.empty()) continue;
        const std::vector<float> f = l.Frame();
        REQUIRE(f.size() == N);
        CHECK(f[0] == static_cast<float>(b));
        blocks.push_back(b);
      }
      return blocks;
    };
    {
      // 8 ms at 48 kHz: 384 samples, every three blocks, from the first.
      SnapRig rig;
      rig.snap.SetParams("8");
      CHECK(rig.snap.GetParams() == "8");
      CHECK(reports(rig) == std::vector<int>{0, 3, 6, 9, 12, 15, 18, 21});
      // A number 0 stops it; a non-zero number starts it again at once.
      rig.snap.GetInlet(0)->SetFloat(0.f, YSE::T_GUI);
      CHECK(reports(rig).empty());
      rig.snap.GetInlet(0)->SetInt(1, YSE::T_GUI);
      CHECK(reports(rig).size() == 8);
      // stop / start, active 0 / 1.
      rig.Send("stop");
      CHECK(reports(rig).empty());
      rig.Send("active 1");
      CHECK(reports(rig).size() == 8);
      rig.Send("active 0");
      CHECK(reports(rig).empty());
      rig.Send("start");
      CHECK(reports(rig).size() == 8);
    }
    {
      // sampleinterval: in samples; 512 is every four blocks.
      SnapRig rig;
      rig.Send("sampleinterval 512");
      CHECK(reports(rig) == std::vector<int>{0, 4, 8, 12, 16, 20});
      // interval <ms> replaces it; shorter than a block is every block.
      rig.Send("interval 1");
      CHECK(reports(rig).size() == 24);
      // The right inlet: ms; 0 stops.
      rig.snap.GetInlet(1)->SetFloat(16.f, YSE::T_GUI);
      CHECK(reports(rig).size() == 4); // 768 samples: six blocks
      rig.snap.GetInlet(1)->SetInt(0, YSE::T_GUI);
      CHECK(reports(rig).empty());
    }
    {
      // At 96 kHz 8 ms is 768 samples: every six blocks.
      TestHelpers::ScopedSampleRate faster(96000);
      SnapRig rig;
      rig.snap.SetParams("8");
      CHECK(reports(rig) == std::vector<int>{0, 6, 12, 18});
    }
  }

  TEST_CASE("~framesnap: inside a ~pfft a large frame goes out as lists an AtomList holds, "
            "over several blocks (#1163)") {
    // 8192 bins in 64 passes, all values long numbers: lists cut by the
    // text capacity, 4096 values per block.
    SnapRig rig;
    rig.snap.SetParams("0 8192");
    CHECK(rig.snap.Snapshot().capacity() == 8192);
    YSE::PATCHER::spectralPass pass = Pass(16384, 4);
    REQUIRE(pass.passes == 64);
    std::vector<float> frame(8192);
    for (UInt k = 0; k < 8192; ++k)
      frame[k] = -1.234567f + 0.000123f * static_cast<float>(k);
    buffer in(N);
    const auto render = [&](U64 index) {
      pass.frame = index;
      for (UInt p = 0; p < pass.passes; ++p) {
        pass.pass = p;
        pass.firstBin = p * N;
        std::copy(frame.begin() + pass.firstBin, frame.begin() + pass.firstBin + N, in.getPtr());
        const YSE::PATCHER::spectralPassScope scope(&pass);
        rig.snap.ResetDSP();
        rig.snap.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      }
      rig.snap.FlushArmedOutlets(YSE::T_GUI);
    };
    // A bang before a frame: reported when the next one is whole.
    rig.snap.GetInlet(0)->SetBang(YSE::T_GUI);
    render(0);
    Lists first = rig.Take();
    REQUIRE_FALSE(first.lists.empty());
    std::vector<float> got = first.Frame();
    CHECK(got.size() == dFramesnap::VALUES_PER_BLOCK);
    // The rest at the next block's flush, even with no frame in it.
    rig.snap.FlushArmedOutlets(YSE::T_GUI);
    Lists second = rig.Take();
    REQUIRE_FALSE(second.lists.empty());
    CHECK(second.lists.front().first == static_cast<int>(dFramesnap::VALUES_PER_BLOCK));
    first.lists.insert(first.lists.end(), second.lists.begin(), second.lists.end());
    got = first.Frame();
    REQUIRE(got.size() == 8192);
    for (UInt k = 0; k < 8192; ++k)
      REQUIRE(got[k] == frame[k]);
    for (const auto& list : first.lists)
      CHECK(list.second.size() <= AtomList::MAX_ATOMS);
    CHECK(first.longestText <= AtomList::TEXT_CAPACITY);
    CHECK(std::max(first.longestText, second.longestText) <= AtomList::TEXT_CAPACITY);
    CHECK(first.lists.size() > 8192 / AtomList::MAX_ATOMS); // cut by the text
    rig.snap.FlushArmedOutlets(YSE::T_GUI);
    CHECK(rig.log.got.empty());

    // A bang while a report goes out waits for it to end.
    rig.snap.GetInlet(0)->SetBang(YSE::T_GUI);
    render(1);
    rig.Take();
    rig.snap.GetInlet(0)->SetBang(YSE::T_GUI);
    render(2); // the second half of the first, the bang waits
    CHECK(rig.Take().lists.front().first == static_cast<int>(dFramesnap::VALUES_PER_BLOCK));
    render(3); // a new report
    CHECK(rig.Take().lists.front().first == 0);
    CHECK(YSE::PATCHER::CurrentSpectralPass() == nullptr);
  }

  TEST_CASE("~frame: in a rendered ~pfft, bin by bin across passes, bit-exact against "
            "DSP::stft with the list's values (#1163)") {
    struct setting {
      UInt size;
      UInt overlap;
      bool full;
    };
    // 1024 at overlap 4: 512 bins in four passes. 64 at 4: 32 bins, eight
    // frames a block. 256 at 2 with the full spectrum, mirrored.
    const setting settings[] = {{1024, 4, false}, {64, 4, false}, {256, 2, true}};
    for (const setting& s : settings) {
      CAPTURE(s.size);
      CAPTURE(s.overlap);
      patcherImplementation p(1, nullptr);
      YSE::pHandle* pfft =
          p.CreateObject(YSE::OBJ::D_PFFT, "fx " + std::to_string(s.size) + " " +
                                               std::to_string(s.overlap) + (s.full ? " 0 1" : ""));
      YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
      YSE::pHandle* frame = p.CreateObject(YSE::OBJ::D_FRAME, "");
      YSE::pHandle* out = p.CreateObject(YSE::OBJ::D_FFTOUT, "0");
      for (YSE::pHandle* h : {pfft, dac, frame, out})
        REQUIRE(h != nullptr);
      p.SetObjectContainer(frame, pfft);
      p.SetObjectContainer(out, pfft);
      p.Connect(frame, 0, out, 0);
      p.Connect(pfft, 0, dac, 0);

      // The values: 300 of them (fewer than the bins of the small FFT), the
      // rest 0.25; later 50 from bin 200 on.
      frameSource values;
      std::vector<float> v(300);
      for (UInt k = 0; k < 300; ++k)
        v[k] = Value(k, 2);
      std::vector<float> w(50);
      for (UInt k = 0; k < 50; ++k)
        w[k] = Value(k, 5) * 2.f;

      stft ref(s.size, s.overlap, s.full);
      auto callback = [&values, &s](stftFrame& f) {
        const UInt half = f.size / 2;
        for (UInt k = 0; k < f.count; ++k) {
          f.imaginary[k] = 0.f;
          if (s.full) {
            f.real[k] = k <= half ? values.value(k) : values.value(f.size - k);
          } else {
            f.real[k] = k < half ? values.value(k) : 0.f;
          }
        }
      };
      buffer silence(N);
      std::size_t at = 0;
      std::vector<float> got;
      std::vector<float> expected;
      for (unsigned int block = 0; block < 40; ++block) {
        if (block == 0) {
          frame->SetListData(0, ListText(v));
          frame->SetListData(0, "fill 0.25");
          if (s.full) frame->SetListData(0, "mirror 1");
          values.set(v.data(), 300);
          values.fill(0.25f);
        }
        if (block == 24) {
          frame->SetIntData(1, 200);
          frame->SetListData(0, ListText(w));
          values.set(w.data(), 50, 200);
        }
        const std::vector<float> o = RenderBlock(p, at);
        buffer& e = ref(silence, callback);
        got.insert(got.end(), o.begin(), o.end());
        expected.insert(expected.end(), e.getPtr(), e.getPtr() + N);
      }
      CHECK(Differences(got, expected) == 0);
      float level = 0.f;
      for (float x : got)
        level = std::max(level, std::abs(x));
      CHECK(level > 0.01f);
    }
  }

  TEST_CASE("~framesnap: in a rendered ~pfft, reports DSP::stft's frame bin by bin across "
            "passes, on a bang and on an interval (#1163)") {
    struct setting {
      UInt size;
      UInt overlap;
    };
    const setting settings[] = {{1024, 4}, {64, 4}, {256, 2}};
    for (const setting& s : settings) {
      CAPTURE(s.size);
      CAPTURE(s.overlap);
      Log log;
      YSE::pHandle logHandle(&log);
      patcherImplementation p(1, nullptr);
      YSE::pHandle* adc = p.CreateObject(YSE::OBJ::D_ADC, "");
      YSE::pHandle* pfft = p.CreateObject(YSE::OBJ::D_PFFT, "fx " + std::to_string(s.size) + " " +
                                                                std::to_string(s.overlap));
      YSE::pHandle* in = p.CreateObject(YSE::OBJ::D_FFTIN, "0");
      YSE::pHandle* snap = p.CreateObject(YSE::OBJ::D_FRAMESNAP, "");
      for (YSE::pHandle* h : {adc, pfft, in, snap})
        REQUIRE(h != nullptr);
      p.SetObjectContainer(in, pfft);
      p.SetObjectContainer(snap, pfft);
      p.Connect(adc, 0, pfft, 0);
      p.Connect(in, 0, snap, 0);
      p.Connect(snap, 0, &logHandle, 0);
      p.Connect(snap, 1, &logHandle, 0);

      // The reference: the real parts of every frame, by the block it
      // completes in.
      stft ref(s.size, s.overlap);
      std::vector<std::vector<std::vector<float>>> frames; // per block
      auto callback = [&frames](stftFrame& f) {
        frames.back().emplace_back(f.real, f.real + f.size / 2);
      };
      const UInt hop = s.size / s.overlap;
      std::size_t at = 0;
      std::vector<std::vector<float>> reports;
      std::vector<std::vector<float>> want;
      bool banged = false;
      for (unsigned int block = 0; block < 48; ++block) {
        if (block == 20) {
          snap->SetBang(0);
          banged = true;
        }
        if (block == 30) snap->SetListData(0, "sampleinterval " + std::to_string(2 * hop));
        frames.emplace_back();
        buffer b(N);
        const std::vector<float> r = RichBlock(at);
        std::copy(r.begin(), r.end(), b.getPtr());
        ref(b, callback);
        RenderBlock(p, at);
        const Lists l = Read(log.got);
        log.got.clear();
        if (!l.lists.empty()) reports.push_back(l.Frame());
        // What must have been reported: the bang's first frame; from block
        // 30 every other frame, from the first; several in a block are the
        // first due.
        if (banged && !frames.back().empty()) {
          want.push_back(frames.back().front());
          banged = false;
        }
      }
      // From block 30 on, by the reference's frame count.
      std::size_t frameCount = 0;
      std::vector<std::vector<float>> interval;
      std::size_t untilNext = 0;
      for (unsigned int block = 30; block < 48; ++block) {
        bool reported = false;
        for (const auto& f : frames[block]) {
          if (untilNext == 0) {
            if (!reported) interval.push_back(f);
            reported = true;
            untilNext = 2;
          }
          untilNext--;
          frameCount++;
        }
      }
      CHECK(frameCount > 4);
      want.insert(want.end(), interval.begin(), interval.end());
      REQUIRE(reports.size() == want.size());
      for (std::size_t i = 0; i < want.size(); ++i) {
        CAPTURE(i);
        REQUIRE(reports[i].size() == s.size / 2);
        CHECK(reports[i] == want[i]);
      }
      p.Disconnect(snap, 0, &logHandle, 0);
      p.Disconnect(snap, 1, &logHandle, 0);
    }
  }

  TEST_CASE("~framesnap -> ~frame: inside a rendered ~pfft a spectrum is frozen from a bang, "
            "bit-exact, after a JSON round trip (#1163)") {
    constexpr UInt SIZE = 1024;
    std::string dump;
    unsigned int snapID = 0;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* adc = src.CreateObject(YSE::OBJ::D_ADC, "");
      YSE::pHandle* pfft = src.CreateObject(YSE::OBJ::D_PFFT, "freeze 1024 4");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      YSE::pHandle* in = src.CreateObject(YSE::OBJ::D_FFTIN, "0");
      YSE::pHandle* snap = src.CreateObject(YSE::OBJ::D_FRAMESNAP, "0 512");
      YSE::pHandle* real = src.CreateObject(YSE::OBJ::D_FRAME, "512");
      YSE::pHandle* imaginary = src.CreateObject(YSE::OBJ::D_FRAMESNAP, "0 512");
      YSE::pHandle* imaginaryOut = src.CreateObject(YSE::OBJ::D_FRAME, "512");
      YSE::pHandle* out = src.CreateObject(YSE::OBJ::D_FFTOUT, "0");
      for (YSE::pHandle* h : {adc, pfft, dac, in, snap, real, imaginary, imaginaryOut, out})
        REQUIRE(h != nullptr);
      for (YSE::pHandle* h : {in, snap, real, imaginary, imaginaryOut, out})
        src.SetObjectContainer(h, pfft);
      src.Connect(adc, 0, pfft, 0);
      src.Connect(in, 0, snap, 0);
      src.Connect(in, 1, imaginary, 0);
      src.Connect(snap, 0, real, 0);
      src.Connect(snap, 1, real, 1);
      src.Connect(imaginary, 0, imaginaryOut, 0);
      src.Connect(imaginary, 1, imaginaryOut, 1);
      src.Connect(real, 0, out, 0);
      src.Connect(imaginaryOut, 0, out, 1);
      src.Connect(pfft, 0, dac, 0);
      // One receive bangs both snapshots.
      YSE::pHandle* control = src.CreateObject(YSE::OBJ::G_RECEIVE, "freeze1163");
      REQUIRE(control != nullptr);
      src.Connect(control, 0, snap, 0);
      src.Connect(control, 0, imaginary, 0);
      snapID = snap->GetID();
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    CHECK(dst.DumpJSON() == dump);
    YSE::pHandle* snap = dst.GetHandleFromID(snapID);
    REQUIRE(snap != nullptr);
    CHECK(snap->GetParams() == "0 512");

    // The reference: silence until the frozen frame takes over, from the
    // block after the one whose first frame it is.
    stft ref(SIZE, 4);
    frameSource frozenReal;
    frameSource frozenImaginary;
    std::vector<float> capturedReal;
    std::vector<float> capturedImaginary;
    bool capture = false;
    bool frozen = false;
    bool captured = false;
    auto callback = [&](stftFrame& f) {
      const UInt half = f.size / 2;
      if (capture) {
        capturedReal.assign(f.real, f.real + half);
        capturedImaginary.assign(f.imaginary, f.imaginary + half);
        capture = false;
        captured = true;
      }
      for (UInt k = 0; k < f.count; ++k) {
        f.real[k] = frozen && k < half ? frozenReal.value(k) : 0.f;
        f.imaginary[k] = frozen && k < half ? frozenImaginary.value(k) : 0.f;
      }
    };
    std::size_t at = 0;
    std::vector<float> got;
    std::vector<float> expected;
    for (unsigned int block = 0; block < 64; ++block) {
      if (block == 21) {
        REQUIRE(dst.PassBang("freeze1163", YSE::T_GUI));
        capture = true;
      }
      if (captured && !frozen) {
        frozenReal.set(capturedReal.data(), static_cast<UInt>(capturedReal.size()));
        frozenImaginary.set(capturedImaginary.data(), static_cast<UInt>(capturedImaginary.size()));
        frozen = true;
      }
      buffer b(N);
      const std::vector<float> r = RichBlock(at);
      std::copy(r.begin(), r.end(), b.getPtr());
      buffer& e = ref(b, callback);
      expected.insert(expected.end(), e.getPtr(), e.getPtr() + N);
      const std::vector<float> o = RenderBlock(dst, at);
      got.insert(got.end(), o.begin(), o.end());
    }
    REQUIRE(frozen);
    CHECK(Differences(got, expected) == 0);
    // Frozen: the output repeats with the hop, and is not silent.
    const std::size_t hop = SIZE / 4;
    float worst = 0.f;
    float level = 0.f;
    for (std::size_t n = 40 * N; n + hop < got.size(); ++n) {
      worst = std::max(worst, std::abs(got[n] - got[n + hop]));
      level = std::max(level, std::abs(got[n]));
    }
    CHECK(worst < 1e-4f);
    CHECK(level > 0.01f);
  }

  TEST_CASE("~frame / ~framesnap: the arguments survive a DumpJSON / ParseJSON round trip "
            "(#1163)") {
    std::string dump;
    unsigned int frameID = 0;
    unsigned int snapID = 0;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* frame = src.CreateObject(YSE::OBJ::D_FRAME, "300");
      YSE::pHandle* snap = src.CreateObject(YSE::OBJ::D_FRAMESNAP, "12.5 2048");
      REQUIRE(frame != nullptr);
      REQUIRE(snap != nullptr);
      frameID = frame->GetID();
      snapID = snap->GetID();
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* frame = dst.GetHandleFromID(frameID);
    YSE::pHandle* snap = dst.GetHandleFromID(snapID);
    REQUIRE(frame != nullptr);
    REQUIRE(snap != nullptr);
    CHECK(frame->GetParams() == "300");
    CHECK(snap->GetParams() == "12.5 2048");
    CHECK(dst.DumpJSON() == dump);

    dFrame f;
    f.SetParams("300");
    CHECK(f.Source().capacity() == 300);
    f.SetParams("0");
    CHECK(f.Source().capacity() == 1);
    dFramesnap s;
    s.SetParams("12.5 2048");
    CHECK(s.Snapshot().capacity() == 2048);
    s.SetParams("-3 100000");
    CHECK(s.Snapshot().capacity() == YSE::DSP::frameSnapshot::MAXIMUM_CAPACITY);
  }

  TEST_CASE("~frame / ~framesnap: messages, blocks, passes and the flush on the audio thread "
            "allocate nothing (#1163)") {
    // Unwired: the sends still run, with no sink of their own to allocate.
    // The messages are built before the probe.
    dFrame frame;
    dFramesnap snap;
    snap.SetParams("0 1024");
    std::vector<float> v(600);
    for (UInt k = 0; k < 600; ++k)
      v[k] = Value(k, 3) + 0.001f * static_cast<float>(k);
    const std::string list = ListText(v);
    const std::string fill = "fill 0.5";
    const std::string mirror = "mirror 1";
    const std::string interval = "interval 1";
    const std::string sampleInterval = "sampleinterval 256";
    const std::string start = "start";
    const std::string stop = "stop";
    buffer in(N);
    Ramp(in, 0.3f);
    const auto block = [&]() {
      frame.ResetDSP();
      frame.Calculate(YSE::T_DSP);
      snap.ResetDSP();
      snap.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      snap.FlushArmedOutlets(YSE::T_GUI);
    };
    YSE::PATCHER::spectralPass pass = Pass(2048, 4);
    const auto spectral = [&](U64 index) {
      pass.frame = index;
      for (UInt p = 0; p < pass.passes; ++p) {
        pass.pass = p;
        pass.firstBin = p * N;
        const YSE::PATCHER::spectralPassScope scope(&pass);
        frame.ResetDSP();
        frame.Calculate(YSE::T_DSP);
        snap.ResetDSP();
        snap.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      }
      snap.FlushArmedOutlets(YSE::T_GUI);
    };
    block();
    {
      TestHelpers::ProbeScope probe;
      frame.GetInlet(0)->SetList(list, YSE::T_DSP);
      frame.GetInlet(0)->SetFloat(0.5f, YSE::T_DSP);
      frame.GetInlet(0)->SetInt(2, YSE::T_DSP);
      frame.GetInlet(1)->SetInt(100, YSE::T_DSP);
      frame.GetInlet(0)->SetList(list, YSE::T_DSP);
      frame.GetInlet(0)->SetMessage(fill, YSE::T_DSP);
      frame.GetInlet(0)->SetMessage(mirror, YSE::T_DSP);
      snap.GetInlet(0)->SetBang(YSE::T_DSP);
      snap.GetInlet(0)->SetMessage(interval, YSE::T_DSP);
      for (int b = 0; b < 8; ++b)
        block();
      snap.GetInlet(0)->SetMessage(sampleInterval, YSE::T_DSP);
      snap.GetInlet(1)->SetFloat(3.f, YSE::T_DSP);
      snap.GetInlet(0)->SetMessage(stop, YSE::T_DSP);
      snap.GetInlet(0)->SetMessage(start, YSE::T_DSP);
      for (U64 f = 0; f < 4; ++f)
        spectral(f);
      snap.GetInlet(0)->SetBang(YSE::T_DSP);
      spectral(4);
      block();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(snap.Snapshot().frames() > 10);
  }

} // TEST_SUITE("patcher")
