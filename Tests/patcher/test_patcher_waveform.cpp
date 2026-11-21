// Tests for the ~waveform patcher object (patcher/sampling/dWaveform), MSP's
// waveform~, issue #1151. The view is DSP::waveformView's and the summary
// sampleBuffer::peaks', both tested in Tests/dsp/test_waveform_view.cpp.
// These show that the inlets, messages and arguments reach the view, that
// the outlets send what each event asks for in right-to-left order, that the
// GUI value is the view in the cell shape the header documents, that drawing
// writes the ~buffer in place and that crop, normalize and
// replacebuffercontents edit it in the background, that name and set rebind
// and show from the block that delivers them, that linked views in a circle
// settle, and that in a rendered patch after a JSON round trip a host's mouse
// gestures draw into a ~buffer the host then reads back through the GUI value
// and peaksMs, with a linked view following.

#include <doctest/doctest.h>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "dsp/sampling/sampleBuffer.hpp"
#include "dsp/sampling/waveformView.hpp"
#include "headers/constants.hpp"
#include "patcher/io/fileScheduler.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sampling/dWaveform.h"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::OrderSink;
using TestHelpers::ScopedSampleRate;
using YSE::DSP::sampleBuffer;
using YSE::DSP::sampleBufferRegistry;
using YSE::DSP::waveformView;
using YSE::PATCHER::dWaveform;
using YSE::PATCHER::patcherImplementation;

namespace {

  // The GUI cells, by name.
  enum CELL {
    SERIAL,
    ID,
    CHANNEL,
    START,
    LENGTH,
    SEL_START,
    SEL_END,
    LINE,
    MODE,
    UNIT,
    SNAP,
    GRID,
    TICKS,
    OFFSET,
    BPM,
    BEATS,
    VZOOM,
    VOFFSET,
  };

  std::vector<double> Numbers(const std::string& value) {
    std::vector<double> all;
    std::istringstream in(value);
    std::string token;
    while (in >> token)
      all.push_back(std::stod(token));
    return all;
  }

  // The bulk read, checked against the per-cell reads.
  std::vector<double> Cells(YSE::pHandle& h) {
    const std::vector<double> all = Numbers(h.GetGuiValue());
    REQUIRE(all.size() == dWaveform::CELLS);
    REQUIRE(h.GetGuiValueCount() == dWaveform::CELLS);
    for (unsigned int i = 0; i < dWaveform::CELLS; ++i)
      CHECK(std::stod(h.GetGuiValueAt(i)) == all[i]);
    CHECK(h.GetGuiValueAt(dWaveform::CELLS).empty());
    return all;
  }

  // A named buffer of `frames` samples at 1000 Hz (1 ms a sample), sample n
  // equal to `fill(n)`.
  template <class Fill>
  std::shared_ptr<sampleBuffer> Named(const std::string& name, UInt frames, Fill fill) {
    std::shared_ptr<sampleBuffer> b = sampleBufferRegistry::acquire(name);
    b->setSize(frames, 1);
    b->setSampleRate(1000.f);
    std::vector<Flt> values(frames);
    for (UInt n = 0; n < frames; ++n)
      values[n] = fill(n);
    b->setSamples(0, 0, values.data(), frames);
    return b;
  }

  std::shared_ptr<sampleBuffer> Silent(const std::string& name, UInt frames = 1000) {
    return Named(name, frames, [](UInt) { return 0.f; });
  }

  Flt At(const sampleBuffer& b, UInt c, UInt frame) {
    Flt out = 0.f;
    b.getSamples(c, frame, &out, 1);
    return out;
  }

  // A ~waveform in a patcher with a sink on every outlet, logging the order
  // they receive in (the outlet's digit). The sinks come first so they
  // outlive the patcher (issue #967).
  struct Rig {
    std::vector<char> log;
    OrderSink sinks[6];
    std::unique_ptr<YSE::pHandle> sinkHandles[6];
    patcherImplementation p{1, nullptr};
    YSE::pHandle* handle = nullptr;
    dWaveform* wf = nullptr;

    explicit Rig(const std::string& args) {
      handle = p.CreateObject(YSE::OBJ::D_WAVEFORM, args);
      REQUIRE(handle != nullptr);
      wf = static_cast<dWaveform*>(p.GetObjectFromID(handle->GetID()));
      for (int i = 0; i < 6; ++i) {
        sinks[i].log = &log;
        sinks[i].tag = static_cast<char>('0' + i);
        sinkHandles[i] = std::make_unique<YSE::pHandle>(&sinks[i]);
        p.Connect(handle, i, sinkHandles[i].get(), 0);
      }
    }

    // The host's way in: text to an inlet.
    void Send(const std::string& text, unsigned int inlet = 0) {
      handle->SetListData(inlet, text);
    }
    void Number(float value, unsigned int inlet) {
      handle->SetFloatData(inlet, value);
    }
    std::string Order() {
      std::string out(log.begin(), log.end());
      log.clear();
      return out;
    }
    // The edit or rebind task runs on the background pool; the next block
    // delivers its result, and the task freeing an old binding runs after.
    void Settle() {
      YSE::PATCHER::fileScheduler* io = p.FileIO();
      REQUIRE(io != nullptr);
      io->WaitIdle();
      p.Calculate(YSE::T_DSP);
      io->WaitIdle();
      p.Calculate(YSE::T_DSP);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~waveform: registry and shape, and the GUI value's defaults (#1151)") {
    auto b = Silent("wf_shape_1151", 2000);
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_WAVEFORM);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~waveform");
    CHECK(obj->GetInputs() == 5);
    CHECK(obj->GetOutputs() == 6);
    CHECK_FALSE(obj->GuiValueIsSettable());
    std::vector<double> c = Cells(*obj);
    CHECK(c[SERIAL] >= 1.0);
    CHECK(c[ID] == 0.0);
    CHECK(c[CHANNEL] == 1.0);
    CHECK(c[START] == 0.0);
    CHECK(c[LENGTH] == 0.0); // the whole buffer
    CHECK(c[SEL_START] == 0.0);
    CHECK(c[SEL_END] == 0.0);
    CHECK(c[LINE] == -1.0);
    CHECK(c[MODE] == 1.0);
    CHECK(c[UNIT] == 0.0);
    CHECK(c[SNAP] == 0.0);
    CHECK(c[GRID] == 1000.0);
    CHECK(c[TICKS] == 0.0);
    CHECK(c[OFFSET] == 0.0);
    CHECK(c[BPM] == 120.0);
    CHECK(c[BEATS] == 4.0);
    CHECK(c[VZOOM] == 1.0);
    CHECK(c[VOFFSET] == 0.0);

    YSE::pHandle* named = p.CreateObject(YSE::OBJ::D_WAVEFORM, "wf_shape_1151 2");
    REQUIRE(named != nullptr);
    c = Cells(*named);
    CHECK(c[ID] == static_cast<double>(b->id()));
    CHECK(c[CHANNEL] == 2.0);
  }

  TEST_CASE("~waveform: numbers set the view and send the link; a bang sends one value "
            "(#1151)") {
    auto b = Silent("wf_inlets_1151", 2000); // 2000 ms
    Rig r("wf_inlets_1151");
    r.Number(100.f, 0);
    CHECK(r.Order() == "5");
    CHECK(r.sinks[5].lastList == "100. 2000. 0. 0.");
    r.Number(100.f, 0); // no change, no link
    CHECK(r.Order().empty());
    r.Number(500.f, 1);
    r.Number(150.f, 2);
    r.Number(300.f, 3);
    CHECK(r.Order() == "555");
    CHECK(r.sinks[5].lastList == "100. 500. 150. 300.");
    CHECK(r.wf->View().displayLength() == 500.0);
    const std::vector<double> c = Cells(*r.handle);
    CHECK(c[START] == 100.0);
    CHECK(c[LENGTH] == 500.0);
    CHECK(c[SEL_START] == 150.0);
    CHECK(c[SEL_END] == 300.0);

    // A bang sends the value of its inlet's outlet only.
    for (unsigned int i = 0; i < 4; ++i) {
      CAPTURE(i);
      r.handle->SetBang(i);
      CHECK(r.Order() == std::string(1, static_cast<char>('0' + i)));
    }
    CHECK(r.sinks[0].lastFloat == 100.f);
    CHECK(r.sinks[1].lastFloat == 500.f);
    CHECK(r.sinks[2].lastFloat == 150.f);
    CHECK(r.sinks[3].lastFloat == 300.f);
    // 0 or less is the whole buffer again; an int works as a float.
    r.handle->SetIntData(1, 0);
    CHECK(r.sinks[5].lastList == "100. 2000. 150. 300.");
    CHECK(r.Order() == "5");
    CHECK(Cells(*r.handle)[LENGTH] == 0.0);
    // A link list sets all four; one that changes nothing sends nothing.
    r.Send("10 20 30 40", 4);
    CHECK(r.Order() == "5");
    CHECK(r.sinks[5].lastList == "10. 20. 30. 40.");
    r.Send("10 20 30 40", 4);
    CHECK(r.Order().empty());
    // A list to inlets 0 to 3 is not a link.
    r.Send("1 2 3 4", 0);
    CHECK(r.wf->View().displayStart() == 10.0);
  }

  TEST_CASE("~waveform: mouse gestures select, and the outlets go right to left (#1151)") {
    auto b = Silent("wf_mouse_1151");
    Rig r("wf_mouse_1151");
    r.Send("mouse 0.25 0.5 1");
    // link, mouse, selection end, selection start.
    CHECK(r.Order() == "5432");
    CHECK(r.sinks[4].lastList == "0.25 0.5 1");
    CHECK(r.sinks[2].lastFloat == 250.f);
    CHECK(r.sinks[3].lastFloat == 250.f);
    r.Send("drag 0.75 0");
    CHECK(r.Order() == "5432");
    CHECK(r.sinks[4].lastList == "0.75 0. 2");
    CHECK(r.sinks[3].lastFloat == 750.f);
    r.Send("mouse 0.75 0 0");
    CHECK(r.Order() == "432"); // nothing moved: no link
    CHECK(r.sinks[4].lastList == "0.75 0. 0");
    CHECK(Cells(*r.handle)[SEL_END] == 750.0);

    // mouseoutput down: the selection on the click only; the link on change.
    r.Send("mouseoutput down");
    r.Send("mouse 0.1 0 1");
    CHECK(r.Order() == "5432");
    r.Send("mouse 0.2 0 2");
    CHECK(r.Order() == "54");
    r.Send("mouse 0.2 0 0");
    CHECK(r.Order() == "4");
    // outmode by number, and quiet.
    r.Send("outmode 2");
    r.Send("quiet 1");
    r.Send("mouse 0.3 0 1");
    CHECK(r.Order() == "54");
    r.Send("mouse 0.4 0 2");
    CHECK(r.Order() == "4");
    r.Send("mouse 0.4 0 0");
    CHECK(r.Order() == "5432");
    CHECK(r.sinks[2].lastFloat == 300.f);
    CHECK(r.sinks[3].lastFloat == 400.f);
    // A bad state, or missing numbers: ignored.
    r.Send("mouse 0.5 0 7");
    r.Send("mouse 0.5");
    CHECK(r.Order().empty());
    // undo: back to before the last gesture.
    r.Send("undo");
    CHECK(r.Order() == "532");
    CHECK(r.sinks[2].lastFloat == 100.f);
    CHECK(r.sinks[3].lastFloat == 200.f);
  }

  TEST_CASE("~waveform: move scrolls and zooms; the display outlets follow (#1151)") {
    auto b = Silent("wf_move_1151");
    Rig r("wf_move_1151 1 3");
    CHECK(r.wf->View().mode() == waveformView::MOVE);
    r.Send("mouse 0.5 0 1");
    CHECK(r.Order() == "4");
    r.Send("mouse 0.5 1 2");
    CHECK(r.Order() == "5410");
    CHECK(r.sinks[0].lastFloat == 250.f);
    CHECK(r.sinks[1].lastFloat == 500.f);
    r.Send("zoom_orientation 1");
    r.Send("mouse 0.5 0 1");
    r.Send("mouse 0.5 1 0");
    CHECK(r.sinks[1].lastFloat == 1000.f);
    const std::vector<double> c = Cells(*r.handle);
    CHECK(c[START] == 0.0);
    CHECK(c[LENGTH] == 1000.0);
  }

  TEST_CASE("~waveform: draw writes the ~buffer in place, as the host sees it (#1151)") {
    auto b = Silent("wf_draw_1151");
    Rig r("wf_draw_1151");
    r.Send("mode draw");
    const double serial = Cells(*r.handle)[SERIAL];
    r.Send("mouse 0.1005 0.5 1"); // sample 100
    CHECK(r.Order() == "4");
    CHECK(At(*b, 0, 100) == 0.5f);
    r.Send("drag 0.1105 -0.5"); // a line to sample 110
    for (UInt f = 100; f <= 110; ++f)
      CHECK(At(*b, 0, f) ==
            doctest::Approx(0.5 - static_cast<double>(f - 100) / 10.0).epsilon(1e-6));
    r.Send("mouse 0.1105 -0.5 0");
    CHECK(Cells(*r.handle)[SERIAL] > serial);
    // The host draws through the GUI value: the buffer by id, its peaks over
    // the display.
    const std::vector<double> c = Cells(*r.handle);
    std::shared_ptr<sampleBuffer> shown =
        sampleBufferRegistry::findById(static_cast<std::uint64_t>(c[ID]));
    REQUIRE(shown == b);
    std::vector<Flt> lo(100), hi(100);
    REQUIRE(shown->peaksMs(static_cast<UInt>(c[CHANNEL]) - 1, static_cast<Flt>(c[START]),
                           static_cast<Flt>(c[LENGTH]), 100, lo.data(), hi.data()) == 100);
    CHECK(hi[10] == 0.5f); // samples 100 to 109
    CHECK(lo[10] == doctest::Approx(-0.4));
    CHECK(lo[11] == -0.5f);
    CHECK(hi[50] == 0.f);

    // vzoom, voffset and clipdraw shape the amplitude.
    r.Send("vzoom 2");
    r.Send("voffset 0.5");
    r.Send("mouse 0.2005 1 1");
    CHECK(At(*b, 0, 200) == 0.25f);
    CHECK(r.sinks[4].lastList == "0.2005 0.25 1");
    r.Send("vzoom 1");
    r.Send("voffset 0");
    r.Send("clipdraw 1");
    r.Send("mouse 0.3005 3 1");
    CHECK(At(*b, 0, 300) == 1.f);
    // chanoffset picks the channel; a channel the buffer lacks draws nothing.
    r.Send("chanoffset 2");
    r.Send("mouse 0.4005 0.5 1");
    CHECK(At(*b, 0, 400) == 0.f);
    CHECK(Cells(*r.handle)[CHANNEL] == 2.0);
  }

  TEST_CASE("~waveform: the settings by message (#1151)") {
    auto b = Silent("wf_settings_1151");
    Rig r("wf_settings_1151");
    const waveformView& v = r.wf->View();
    r.Send("mode loop");
    CHECK(v.mode() == waveformView::LOOP);
    r.Send("setmode 4");
    CHECK(v.mode() == waveformView::DRAW);
    r.Send("mode nonsense");
    CHECK(v.mode() == waveformView::DRAW);
    r.Send("mode select");
    r.Send("snap grid");
    r.Send("grid 100");
    r.Send("mouse 0.23 0 1");
    r.Send("mouse 0.78 0 0");
    CHECK(v.selectionStart() == doctest::Approx(200.0));
    CHECK(v.selectionEnd() == doctest::Approx(800.0));
    r.Send("snapto 2");
    CHECK(v.snap() == waveformView::SNAP_ZERO);
    r.Send("snap tick");
    r.Send("ticks 4");
    CHECK(v.snap() == waveformView::SNAP_TICK);
    CHECK(v.ticks() == 4);
    r.Send("unit samples");
    CHECK(v.unit() == waveformView::SAMPLES);
    r.Send("setunit 2");
    CHECK(v.unit() == waveformView::PHASE);
    r.Send("offset 30");
    CHECK(v.offset() == 30.0);
    r.Send("bpm 90 3");
    CHECK(v.tempo() == 90.0);
    CHECK(v.beats() == 3);
    r.Send("beats 4");
    CHECK(v.beats() == 4);
    r.Send("line 250");
    CHECK(v.line() == 250.0);
    r.Send("line phase");
    r.Send("line 0.5");
    CHECK(v.line() == doctest::Approx(500.0));
    r.Send("line -1");
    CHECK(Cells(*r.handle)[LINE] == -1.0);
    r.Send("mouseoutput upp");
    CHECK(v.output() == waveformView::OUTPUT_CONTINUOUS);

    // setbpm: the selection 200..800 is a bar of 4 beats; the tempo goes out
    // of the link outlet.
    r.Order();
    r.Send("setbpm");
    CHECK(r.Order() == "5");
    CHECK(r.sinks[5].lastList == "bpm 400. 4");
    CHECK(v.unit() == waveformView::BPM);
    const std::vector<double> c = Cells(*r.handle);
    CHECK(c[BPM] == 400.0);
    CHECK(c[UNIT] == 3.0);
    CHECK(c[GRID] == doctest::Approx(600.0));
    CHECK(c[OFFSET] == doctest::Approx(200.0));
    CHECK(c[SNAP] == 2.0);
    CHECK(c[TICKS] == 4.0);
  }

  TEST_CASE("~waveform: linked views follow each other and settle in a circle (#1151)") {
    auto b = Silent("wf_link_1151", 2000);
    OrderSink tail;
    std::unique_ptr<YSE::pHandle> tailHandle;
    patcherImplementation p(1, nullptr);
    YSE::pHandle* a = p.CreateObject(YSE::OBJ::D_WAVEFORM, "wf_link_1151");
    YSE::pHandle* c = p.CreateObject(YSE::OBJ::D_WAVEFORM, "wf_link_1151");
    REQUIRE(a != nullptr);
    REQUIRE(c != nullptr);
    p.Connect(a, 5, c, 4);
    p.Connect(c, 5, a, 4);
    tailHandle = std::make_unique<YSE::pHandle>(&tail);
    p.Connect(c, 3, tailHandle.get(), 0);
    const auto* wa = static_cast<const dWaveform*>(p.GetObjectFromID(a->GetID()));
    const auto* wc = static_cast<const dWaveform*>(p.GetObjectFromID(c->GetID()));

    a->SetFloatData(0, 100.f);
    a->SetFloatData(1, 400.f);
    a->SetListData(0, "mouse 0.5 0 1");
    a->SetListData(0, "mouse 0.75 0 0");
    CHECK(wc->View().displayStart() == 100.0);
    CHECK(wc->View().displayLength() == 400.0);
    CHECK(wc->View().selectionStart() == doctest::Approx(300.0));
    CHECK(wc->View().selectionEnd() == doctest::Approx(400.0));
    // A link sets the view, it is not a gesture: no selection outlet.
    CHECK(tail.count == 0);
    // The other way round too.
    c->SetFloatData(3, 900.f);
    CHECK(wa->View().selectionEnd() == 900.0);
    // Each send came back to its sender, which was busy sending: dropped,
    // so the circle ends there.
    CHECK(wa->Dropped() + wc->Dropped() > 0);
    const std::vector<double> ca = Cells(*a);
    const std::vector<double> cc = Cells(*c);
    for (int i = START; i <= SEL_END; ++i)
      CHECK(ca[i] == cc[i]);
  }

  TEST_CASE("~waveform: name and set rebind, shown from the block that delivers them "
            "(#1151)") {
    auto first = Silent("wf_set_1151_a");
    auto second = Silent("wf_set_1151_b", 500);
    second->setSize(500, 2);
    Rig r("wf_set_1151_a");
    r.Number(100.f, 1);
    r.Send("set wf_set_1151_b 2");
    // The view is reset at once; the buffer is taken by the next block,
    // without another message.
    CHECK(r.wf->View().wholeBuffer());
    r.Settle();
    std::vector<double> c = Cells(*r.handle);
    CHECK(c[ID] == static_cast<double>(second->id()));
    CHECK(c[CHANNEL] == 2.0);
    CHECK(r.wf->Buffer() == second);
    CHECK(r.wf->View().displayLength() == 500.0);
    // Drawing now writes the second buffer's second channel.
    r.Send("mode draw");
    r.Send("mouse 0.501 0.25 1"); // sample 250
    CHECK(At(*second, 1, 250) == 0.25f);
    CHECK(At(*first, 0, 500) == 0.f);
    // name alone unbinds.
    r.Send("name");
    r.Settle();
    CHECK(Cells(*r.handle)[ID] == 0.0);
    CHECK(r.wf->Buffer() == nullptr);
    r.Send("mouse 0.5 0.5 1"); // nothing to draw into
    CHECK(At(*first, 0, 500) == 0.f);
    r.Send("name wf_set_1151_a");
    r.Settle();
    CHECK(r.wf->Buffer() == first);
    CHECK(r.p.FileIO()->Dropped() == 0);
  }

  TEST_CASE("~waveform: crop, normalize and replacebuffercontents edit the ~buffer in the "
            "background (#1151)") {
    auto b = Named("wf_edit_1151", 1000, [](UInt n) { return static_cast<Flt>(n) / 2000.f; });
    Rig r("wf_edit_1151");
    // crop keeps the selection, whichever way round it was set.
    r.Number(750.f, 2);
    r.Number(250.f, 3);
    const double serial = Cells(*r.handle)[SERIAL];
    r.Send("crop");
    r.Settle();
    REQUIRE(b->frames() == 500);
    CHECK(At(*b, 0, 0) == 250.f / 2000.f);
    CHECK(At(*b, 0, 499) == 749.f / 2000.f);
    const std::vector<double> c = Cells(*r.handle);
    CHECK(c[SERIAL] > serial);
    CHECK(c[SEL_START] == 0.0);
    CHECK(c[SEL_END] == 0.0);
    CHECK(r.wf->View().displayLength() == 500.0);
    // An empty selection crops nothing.
    r.Send("crop");
    r.Settle();
    CHECK(b->frames() == 500);

    r.Send("normalize 0.5");
    r.Settle();
    CHECK(At(*b, 0, 499) == doctest::Approx(0.5));
    r.Send("normalize");
    r.Settle();
    CHECK(At(*b, 0, 499) == doctest::Approx(1.0));

    // replacebuffercontents reads a file into the buffer and shows it whole.
    auto source = YSE::DSP::sampleBuffer::create(1, 300, 1000.f);
    source->fill(0.25f);
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "wf_replace_1151.wav";
    REQUIRE(source->write(path.string().c_str()));
    r.Number(50.f, 1);
    r.Send("replacebuffercontents " + path.string());
    CHECK(r.wf->View().wholeBuffer());
    r.Settle();
    CHECK(b->frames() == 300);
    CHECK(At(*b, 0, 100) == 0.25f);
    CHECK(r.wf->View().displayLength() == doctest::Approx(300.0));
    CHECK(r.wf->FailedTasks() == 0);
    r.Send("replacebuffercontents /no/such/file_1151.wav");
    r.Settle();
    CHECK(r.wf->FailedTasks() == 1);
    CHECK(b->frames() == 300);
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
  }

  TEST_CASE("~waveform: the arguments survive a DumpJSON / ParseJSON round trip (#1151)") {
    auto b = Silent("wf_json_1151");
    const std::string args = "wf_json_1151 2 3 1 1 250 4 10 1 90 3 2 0.5 1 1 1";
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_WAVEFORM, args) != nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_WAVEFORM, "") != nullptr);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    CHECK(dst.DumpJSON() == dump);
    YSE::pHandle* h = dst.GetHandleFromID(0);
    REQUIRE(h != nullptr);
    CHECK(h->GetParams() == args);
    const waveformView& v = static_cast<const dWaveform*>(dst.GetObjectFromID(0))->View();
    CHECK(v.channel() == 1);
    CHECK(v.mode() == waveformView::MOVE);
    CHECK(v.output() == waveformView::OUTPUT_DOWN);
    CHECK(v.snap() == waveformView::SNAP_GRID);
    CHECK(v.grid() == 250.0);
    CHECK(v.ticks() == 4);
    CHECK(v.offset() == 10.0);
    CHECK(v.unit() == waveformView::SAMPLES);
    CHECK(v.tempo() == 90.0);
    CHECK(v.beats() == 3);
    CHECK(v.verticalZoom() == 2.f);
    CHECK(v.verticalOffset() == 0.5f);
    CHECK(v.clipDraw());
    CHECK(v.quiet());
    CHECK(v.zoomDown());
    CHECK(Cells(*h)[ID] == static_cast<double>(b->id()));
    // A message is not saved.
    h->SetListData(0, "mode draw");
    CHECK(h->GetParams() == args);
  }

  TEST_CASE("~waveform: in a rendered patch after a JSON round trip, a host's gestures "
            "draw into a ~buffer it reads back, and a linked view follows (#1151)") {
    ScopedSampleRate rate(48000);
    std::string dump;
    unsigned int drawID = 0;
    unsigned int followID = 0;
    {
      patcherImplementation src(1, nullptr);
      // A ~buffer of 100 ms (4800 samples at 48 kHz), a ~waveform drawing
      // into it (setmode 4) whose link feeds a second one, and a receive
      // that carries messages to the first.
      YSE::pHandle* store = src.CreateObject(YSE::OBJ::D_BUFFER, "wf_patch_1151 100");
      YSE::pHandle* control = src.CreateObject(YSE::OBJ::G_RECEIVE, "wf1151");
      YSE::pHandle* draw = src.CreateObject(YSE::OBJ::D_WAVEFORM, "wf_patch_1151 1 4");
      YSE::pHandle* follow = src.CreateObject(YSE::OBJ::D_WAVEFORM, "wf_patch_1151");
      REQUIRE(store != nullptr);
      REQUIRE(control != nullptr);
      REQUIRE(draw != nullptr);
      REQUIRE(follow != nullptr);
      src.Connect(control, 0, draw, 0);
      src.Connect(draw, 5, follow, 4);
      drawID = draw->GetID();
      followID = follow->GetID();
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* draw = dst.GetHandleFromID(drawID);
    YSE::pHandle* follow = dst.GetHandleFromID(followID);
    REQUIRE(draw != nullptr);
    REQUIRE(follow != nullptr);
    CHECK(draw->GetParams() == "wf_patch_1151 1 4");
    for (int i = 0; i < 4; ++i)
      dst.Calculate(YSE::T_DSP);

    // Zoom the display to 50..75 ms through the receive; the link carries it.
    draw->SetFloatData(0, 50.f);
    draw->SetFloatData(1, 25.f);
    std::vector<double> c = Cells(*draw);
    CHECK(c[START] == 50.0);
    CHECK(c[LENGTH] == 25.0);
    CHECK(Cells(*follow)[START] == 50.0);
    CHECK(Cells(*follow)[LENGTH] == 25.0);

    // A stroke from 25% to 75% across the display (56.25 to 68.75 ms) from
    // 0.8 down to -0.8, the down event through the receive, the rest from
    // the host, between blocks.
    REQUIRE(dst.PassData("mouse 0.25 0.8 1", "wf1151", YSE::T_GUI));
    dst.Calculate(YSE::T_DSP);
    draw->SetListData(0, "drag 0.5 0");
    dst.Calculate(YSE::T_DSP);
    draw->SetListData(0, "mouse 0.75 -0.8 0");
    dst.Calculate(YSE::T_DSP);

    // The host draws: the buffer from the id cell, its peaks over the display.
    c = Cells(*draw);
    std::shared_ptr<sampleBuffer> shown =
        sampleBufferRegistry::findById(static_cast<std::uint64_t>(c[ID]));
    REQUIRE(shown != nullptr);
    CHECK(shown == sampleBufferRegistry::find("wf_patch_1151"));
    REQUIRE(shown->frames() == 4800);
    std::vector<Flt> lo(8), hi(8);
    REQUIRE(shown->peaksMs(0, static_cast<Flt>(c[START]), static_cast<Flt>(c[LENGTH]), 8, lo.data(),
                           hi.data()) == 8);
    // The stroke covers buckets 2 to 5 (each 150 samples): a line falling
    // from 0.8 at sample 2700 to -0.8 at sample 3300, untouched around it.
    CHECK(hi[0] == 0.f);
    CHECK(lo[1] == 0.f);
    CHECK(hi[2] == doctest::Approx(0.8));
    CHECK(lo[5] == doctest::Approx(-0.8).epsilon(0.01));
    CHECK(hi[6] == 0.f);
    CHECK(lo[7] == 0.f);
    CHECK(At(*shown, 0, 3000) == doctest::Approx(0.0).epsilon(0.01));
    CHECK(At(*shown, 0, 2850) == doctest::Approx(0.4).epsilon(0.01));
    // The selection did not change: drawing is not selecting, the linked
    // view still shows what the first one does.
    CHECK(Cells(*follow)[SEL_END] == 0.0);
    CHECK(Cells(*follow)[START] == 50.0);
  }

  TEST_CASE("~waveform: a host thread drawing while the audio thread renders and messages "
            "(#1151)") {
    auto b = Silent("wf_race_1151", 4000);
    Rig r("wf_race_1151 1 4");
    std::atomic<bool> stop{false};
    std::atomic<int> sent{0};
    std::thread host([&] {
      int i = 0;
      while (!stop.load()) {
        const float x = static_cast<float>(i % 100) / 100.f;
        r.handle->SetListData(0, std::string("mouse ") + std::to_string(x) + " 0.5 " +
                                     (i % 10 == 0 ? "1" : "2"));
        (void)r.handle->GetGuiValue();
        sent.fetch_add(1);
        ++i;
      }
    });
    const std::string line = "line 10";
    for (int block = 0; block < 400; ++block) {
      r.p.Calculate(YSE::T_DSP);
      r.wf->GetInlet(0)->SetMessage(line, YSE::T_DSP);
    }
    stop.store(true);
    host.join();
    r.wf->GetInlet(0)->SetMessage(line, YSE::T_DSP);
    // Every sample drawn is the one value the host draws.
    std::vector<Flt> lo(1), hi(1);
    REQUIRE(b->peaks(0, 0, 4000, 1, lo.data(), hi.data()) == 1);
    CHECK(lo[0] >= 0.f);
    CHECK(hi[0] <= 0.5f);
    CHECK(sent.load() > 0);
    CHECK(Cells(*r.handle)[LINE] == 10.0);
  }

  TEST_CASE("~waveform: numbers, lists and messages on the audio thread allocate nothing "
            "(#1151)") {
    auto b = Silent("wf_rt_1151");
    auto other = Silent("wf_rt_1151_b");
    Rig r("wf_rt_1151");
    // Built before the probe: the strings themselves allocate. The sinks'
    // first sends, too, so their strings have the capacity.
    const std::vector<std::string> messages = {
        "mouse 0.25 0.5 1", "drag 0.5 0.25", "mouse 0.75 0 0",   "mode loop",     "mouse 0.1 0 1",
        "mouse 0.2 0 0",    "mode move",     "mouse 0.5 0 1",    "mouse 0.5 1 0", "mode draw",
        "mouse 0.3 0.5 1",  "drag 0.4 -0.5", "mouse 0.4 0 0",    "snap zero",     "mode select",
        "mouse 0.6 0 1",    "mouse 0.7 0 0", "line 250",         "undo",          "setbpm",
        "bpm 120 4",        "vzoom 2",       "set wf_rt_1151_b", "crop",          "normalize 0.5",
        "mouseoutput up",   "unit samples",  "chanoffset 1",
    };
    const std::string link = "10 20 30 40";
    r.log.reserve(4096);
    r.Send("bpm 1234.5678 4"); // a long list to every sink first
    r.Send("setbpm");
    r.Send("123456.78 123456.78 123456.78 123456.78", 4);
    r.Send("mouse 0.123456 0.123456 1");
    r.Send("mouse 0.123456 0.123456 0");
    for (int i = 0; i < 4; ++i)
      r.handle->SetBang(static_cast<unsigned int>(i));
    {
      TestHelpers::ProbeScope probe;
      for (const std::string& m : messages)
        r.wf->GetInlet(0)->SetList(m, YSE::T_DSP);
      r.wf->GetInlet(4)->SetList(link, YSE::T_DSP);
      r.wf->GetInlet(1)->SetFloat(100.f, YSE::T_DSP);
      r.wf->GetInlet(2)->SetInt(5, YSE::T_DSP);
      r.wf->GetInlet(3)->SetBang(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    r.p.FileIO()->WaitIdle();
    {
      // The block that takes the rebind and delivers the edits.
      TestHelpers::ProbeScope probe;
      r.p.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    r.Settle();
    CHECK(r.wf->Buffer() == other);
  }

} // TEST_SUITE("patcher")
