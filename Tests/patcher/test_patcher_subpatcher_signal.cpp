// Tests for signal crossing at a subpatcher boundary — `~inlet` / `~outlet`
// (issue #764). The audio-rate half of the boundary #545 built control-rate;
// test_patcher_subpatcher.cpp covers that half and the encapsulation model both
// halves share.
//
// Three claims are being pinned, and they are not the same claim.
//
// The **objects** are small: `~inlet` and `~outlet` are buffer pass-throughs
// with an index, and their cases drive standalone objects through a sink.
// One of those cases is the whole performance argument and is asserted as
// pointer identity rather than as a timing: what leaves the boundary is *the
// same buffer* the upstream object computed into, so a crossing copies nothing
// and adds no indirection to the audio path.
//
// The **encapsulation** is what the issue is for, and it can only be tested
// where a user meets it: build a real patcher, put DSP objects inside a
// `patcher` object, wire the *group* by pin number from outside, and render
// real audio through `patcherInsert` — the same rig test_patcher_adc.cpp uses
// for `~adc`. Both acceptance directions are here: a generator inside reaching
// a `~dac` outside, and a `~adc` outside reaching a filter inside. Every such
// case is paired with a control, because a boundary that silently did nothing
// and a boundary that worked would otherwise be told apart by nothing.
//
// The **architecture** is the third layer. A signal boundary must not undo any
// of #545's model, so: the recorded cord names the boundary object rather than
// the façade (there are no subpatchers in the compiled graph); nesting three
// deep is still bit-transparent (depth costs what the author wrote and nothing
// more); an edit inside a published subpatcher takes effect through the one
// atomic GraphState swap; the crossing allocates nothing; and the index space
// is shared with the control-rate boundary, because a subpatcher has one set of
// pins per side.
//
// No audio device required.

#include <doctest/doctest.h>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "dsp/buffer.hpp"
#include "dsp/patcherInsert.hpp"
#include "headers/defines.hpp"
#include "patcher/genericObjects/dInlet.h"
#include "patcher/genericObjects/dOutlet.h"
#include "patcher/inlet.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObject.h"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using TestHelpers::BufferSink;
using TestHelpers::measureRms;
using TestHelpers::Wire;
using YSE::PATCHER::dInlet;
using YSE::PATCHER::dOutlet;
using YSE::PATCHER::Register;

namespace {

  constexpr float kPi = 3.14159265358979323846f;

  void fillSine(YSE::DSP::buffer& buf, float freq, float sr = 44100.0f) {
    float* p = buf.getPtr();
    for (unsigned i = 0; i < buf.getLength(); ++i)
      p[i] = std::sin(2.0f * kPi * freq * static_cast<float>(i) / sr);
  }

  // A distinctive, non-trivial signal, so a bit-transparency check across the
  // boundary is worth something.
  void fillPattern(YSE::DSP::buffer& buf, float phase = 0.f) {
    float* p = buf.getPtr();
    const unsigned n = buf.getLength();
    for (unsigned i = 0; i < n; ++i) {
      p[i] = 0.5f * std::sin(phase + 0.13f * static_cast<float>(i)) +
             (static_cast<float>(i) / static_cast<float>(n)) - 0.5f;
    }
  }

  bool exactlyEqual(YSE::DSP::buffer& a, YSE::DSP::buffer& b) {
    if (a.getLength() != b.getLength()) return false;
    float* pa = a.getPtr();
    float* pb = b.getPtr();
    for (unsigned i = 0; i < a.getLength(); ++i)
      if (pa[i] != pb[i]) return false;
    return true;
  }

  bool isSilent(YSE::DSP::buffer& b) {
    float* p = b.getPtr();
    for (unsigned i = 0; i < b.getLength(); ++i)
      if (p[i] != 0.f) return false;
    return true;
  }

  // A subpatcher whose contents are a bare signal pass-through:
  //
  //   `patcher`      — the subpatcher
  //   `~inlet 0`     — inside it
  //   `~outlet 0`    — inside it, wired straight from the inlet
  //
  // The parent wires to (sub, inlet 0) and from (sub, outlet 0) and never names
  // what is inside, which is the point. Nothing but the two boundary objects is
  // in it, so anything the signal loses on the way through is the boundary's
  // doing and nobody else's.
  YSE::pHandle* BuildPassthroughSubpatch(YSE::patcher& p) {
    YSE::pHandle* sub = p.CreateObject(YSE::OBJ::PATCHER, "");
    YSE::pHandle* in = p.CreateObject(YSE::OBJ::D_INLET, "0");
    YSE::pHandle* out = p.CreateObject(YSE::OBJ::D_OUTLET, "0");
    REQUIRE(sub != nullptr);
    REQUIRE(in != nullptr);
    REQUIRE(out != nullptr);
    p.SetContainer(in, sub);
    p.SetContainer(out, sub);
    p.Connect(in, 0, out, 0);
    return sub;
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── the objects exist and have the shape a signal boundary needs ──────────

  TEST_CASE("subpatcher signal: ~inlet and ~outlet are registered DSP objects (#764)") {
    auto names = Register().AllNames();
    bool hasIn = false;
    bool hasOut = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::D_INLET)) hasIn = true;
      if (name == std::string(YSE::OBJ::D_OUTLET)) hasOut = true;
    }
    CHECK(hasIn);
    CHECK(hasOut);

    // One buffer inlet, one buffer outlet — the same shape as `.inlet`, one
    // rate up. The outlet type is what `inlet::Connect` reads to decide an edge
    // is a DSP edge rather than a control one, so it is load-bearing rather
    // than decorative.
    std::unique_ptr<YSE::PATCHER::pObject> in(Register().Get(YSE::OBJ::D_INLET));
    REQUIRE(in != nullptr);
    CHECK(std::string(in->Type()) == "~inlet");
    CHECK(in->NumInputs() == 1);
    CHECK(in->NumOutputs() == 1);
    CHECK(in->GetOutputType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(in->GetInlet(0)->AcceptsDSP());

    std::unique_ptr<YSE::PATCHER::pObject> out(Register().Get(YSE::OBJ::D_OUTLET));
    REQUIRE(out != nullptr);
    CHECK(std::string(out->Type()) == "~outlet");
    CHECK(out->NumInputs() == 1);
    CHECK(out->NumOutputs() == 1);
    CHECK(out->GetOutputType(0) == YSE::OUT_TYPE::BUFFER);

    // And they really are DSP objects, which is the half a signal-capable
    // `.inlet` could not have been: `IsDSPObject()` is what decides start-point
    // selection, what an inlet does with a T_GUI dispatch, and what every
    // palette is told the object is. The control-rate pair must stay the other
    // answer, or the split has bought nothing.
    CHECK(in->IsDSPObject());
    CHECK(out->IsDSPObject());
    std::unique_ptr<YSE::PATCHER::pObject> ctrl(Register().Get(YSE::OBJ::G_INLET));
    REQUIRE(ctrl != nullptr);
    CHECK_FALSE(ctrl->IsDSPObject());
  }

  // ─── the boundary objects, on their own ────────────────────────────────────

  TEST_CASE("subpatcher signal: the boundary forwards the buffer without copying it (#764)") {
    // The performance claim, and the reason a signal boundary is affordable at
    // all. This patcher's signal path passes buffers *by address* — a DSP
    // object computes into a buffer it owns and hands the pointer down the cord
    // — so a pass-through has nothing to copy. Asserted as pointer identity:
    // the sink is handed the very buffer the test filled, not a copy of it and
    // not a handle to one. A boundary that copied would still pass a
    // value-equality check, which is exactly why this one is written on the
    // address.
    BufferSink sink;
    dInlet in;
    Wire(in, 0, sink, 0);

    YSE::DSP::buffer upstream(128);
    fillPattern(upstream, 0.4f);

    in.GetInlet(0)->SetBuffer(&upstream, YSE::T_DSP);
    CHECK(sink.received == &upstream);

    // `~outlet` is the same object read the other way round.
    BufferSink sink2;
    dOutlet out;
    Wire(out, 0, sink2, 0);
    out.GetInlet(0)->SetBuffer(&upstream, YSE::T_DSP);
    CHECK(sink2.received == &upstream);
  }

  TEST_CASE("subpatcher signal: a block with nothing on the boundary sends nothing (#764)") {
    // `ResetDSP` runs over every object at the top of every block, and a
    // boundary that kept last block's pointer would forward a buffer whose
    // contents belong to a block that has already been rendered — a stale
    // signal rather than silence, and the kind of bug that sounds like a glitch
    // instead of like nothing.
    BufferSink sink;
    dInlet in;
    Wire(in, 0, sink, 0);

    YSE::DSP::buffer upstream(128);
    in.GetInlet(0)->SetBuffer(&upstream, YSE::T_DSP);
    REQUIRE(sink.received == &upstream);

    sink.received = nullptr;
    in.ResetDSP();
    in.Calculate(YSE::T_DSP);
    CHECK(sink.received == nullptr);
  }

  TEST_CASE("subpatcher signal: the boundary is a DSP start point only when unfed (#764)") {
    // Start-point selection is how the render finds the roots of its push
    // traversal. An unconnected `~inlet` is a root that produces nothing, which
    // is right; a *fed* one must not be a root, or it would be calculated
    // before the object upstream of it had run and would forward last block's
    // pointer — or, after ResetDSP, nothing at all.
    dInlet solo;
    CHECK(solo.IsDSPStartPoint());

    // A `~sine` standing in for whatever the parent patch wires to the pin.
    std::unique_ptr<YSE::PATCHER::pObject> src(Register().Get(YSE::OBJ::D_SINE));
    REQUIRE(src != nullptr);
    dInlet fed;
    Wire(*src, 0, fed, 0);
    CHECK_FALSE(fed.IsDSPStartPoint());
  }

  TEST_CASE("subpatcher signal: the boundary index is a creation argument (#764)") {
    dInlet in;
    CHECK(in.Index() == 0); // what an unargumented one is, as for `.inlet`
    in.SetParams("3");
    CHECK(in.Index() == 3);
    CHECK(in.GetParams() == "3");

    dOutlet out;
    out.SetParams("2");
    CHECK(out.Index() == 2);
  }

  TEST_CASE("subpatcher signal: crossing a boundary allocates nothing (#764)") {
    // The RT claim. A crossing runs on the audio callback — the whole DSP
    // traversal does — so `SetBuffer` -> `Calculate` -> `SendBuffer` has to be
    // free of allocation, locks and I/O. It is a stored pointer and a fan-out
    // loop, and the object holds no buffer of its own to allocate in the first
    // place.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    BufferSink sink;
    dInlet in;
    dOutlet out;
    Wire(in, 0, out, 0);
    Wire(out, 0, sink, 0);

    // Built outside the probe: it is the *crossing* that must not allocate, not
    // the test's own buffer.
    YSE::DSP::buffer upstream(128);
    fillPattern(upstream, 0.9f);

    // Read out and asserted outside the armed region — doctest's own assertion
    // machinery allocates on first use and would otherwise be counted against
    // the code under test.
    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      for (int block = 0; block < 8; block++) {
        in.ResetDSP();
        out.ResetDSP();
        in.GetInlet(0)->SetBuffer(&upstream, YSE::T_DSP);
      }
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);
    // The probed crossings really happened: an assertion that only proves
    // nothing happened proves nothing.
    CHECK(sink.received == &upstream);
  }

  // ─── acceptance: audio crosses the boundary, both directions ───────────────

  TEST_CASE("subpatcher signal: a generator inside reaches a ~dac outside (#764)") {
    // Acceptance criterion one, and the sentence the issue opens with: "a
    // ~sine inside a subpatcher cannot reach a ~dac outside it". It can now.
    //
    //   inside:  `~sine 440` -> `~outlet 0`
    //   outside: subpatcher outlet 0 -> `~dac`
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* sub = p.CreateObject(YSE::OBJ::PATCHER, "");
    YSE::pHandle* sine = p.CreateObject(YSE::OBJ::D_SINE, "440");
    YSE::pHandle* out = p.CreateObject(YSE::OBJ::D_OUTLET, "0");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC);
    REQUIRE(sub != nullptr);
    REQUIRE(dac != nullptr);
    for (YSE::pHandle* h : {sine, out}) {
      REQUIRE(h != nullptr);
      p.SetContainer(h, sub);
    }
    p.Connect(sine, 0, out, 0);
    p.Connect(sub, 0, dac, 0); // the parent names the *group*, by pin number

    YSE::DSP::patcherInsert insert(p);
    MULTICHANNELBUFFER io;
    io.resize(1);
    io[0].resize(128);
    io[0] = 0.f;
    insert.process(io);

    CHECK(measureRms(io[0]) > 0.1f);

    // The control. Cut the one cord that crosses the boundary and the same
    // patch goes silent — so what was measured above was the crossing and not
    // the oscillator leaking out some other way.
    p.Disconnect(sub, 0, dac, 0);
    io[0] = 0.f;
    insert.process(io);
    CHECK(isSilent(io[0]));
  }

  TEST_CASE("subpatcher signal: a ~adc outside reaches a filter inside (#764)") {
    // Acceptance criterion two, the other direction, and with something inside
    // that provably *did* something: a 200 Hz lowpass eating an 8 kHz tone.
    //
    //   outside: `~adc` -> subpatcher inlet 0 ... subpatcher outlet 0 -> `~dac`
    //   inside:  `~inlet 0` -> `~lp 200` -> `~outlet 0`
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* adc = p.CreateObject(YSE::OBJ::D_ADC);
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC);
    YSE::pHandle* sub = p.CreateObject(YSE::OBJ::PATCHER, "");
    YSE::pHandle* in = p.CreateObject(YSE::OBJ::D_INLET, "0");
    YSE::pHandle* lp = p.CreateObject(YSE::OBJ::D_LOWPASS, "200");
    YSE::pHandle* out = p.CreateObject(YSE::OBJ::D_OUTLET, "0");
    REQUIRE(adc != nullptr);
    REQUIRE(dac != nullptr);
    REQUIRE(sub != nullptr);
    for (YSE::pHandle* h : {in, lp, out}) {
      REQUIRE(h != nullptr);
      p.SetContainer(h, sub);
    }
    p.Connect(in, 0, lp, 0);
    p.Connect(lp, 0, out, 0);
    p.Connect(adc, 0, sub, 0);
    p.Connect(sub, 0, dac, 0);

    YSE::DSP::patcherInsert insert(p);
    MULTICHANNELBUFFER io;
    io.resize(1);
    io[0].resize(128);

    YSE::DSP::buffer dry(128);
    fillSine(dry, 8000.0f);
    const float dryRms = measureRms(dry);
    REQUIRE(dryRms > 0.0f);

    // Block after block so the filter settles, measuring the last one.
    for (int iter = 0; iter < 40; ++iter) {
      fillSine(io[0], 8000.0f);
      insert.process(io);
    }

    CHECK(measureRms(io[0]) < dryRms * 0.5f);
    CHECK_FALSE(exactlyEqual(io[0], dry));

    // Attenuation on its own would also be what a boundary that dropped the
    // signal produced — silence passes any "quieter than dry" check, which is
    // the way a test like this fails to fail. So the pass band is measured too:
    // a 100 Hz tone through the same 200 Hz lowpass has to come back out at
    // close to full level, which only silence-free audio can do.
    YSE::DSP::buffer lowDry(128);
    fillSine(lowDry, 100.0f);
    const float lowDryRms = measureRms(lowDry);
    REQUIRE(lowDryRms > 0.0f);
    for (int iter = 0; iter < 40; ++iter) {
      fillSine(io[0], 100.0f);
      insert.process(io);
    }
    CHECK(measureRms(io[0]) > lowDryRms * 0.5f);
  }

  TEST_CASE("subpatcher signal: crossing a boundary is bit-transparent (#764)") {
    // The complement of the filter case, and the strongest statement of "the
    // boundary does nothing to the signal": a subpatcher containing only
    // `~inlet 0` -> `~outlet 0` returns the host's audio sample for sample. Any
    // copy, sum, scale or one-block delay the boundary introduced would show up
    // here as an inequality.
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* adc = p.CreateObject(YSE::OBJ::D_ADC);
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC);
    YSE::pHandle* sub = BuildPassthroughSubpatch(p);
    REQUIRE(adc != nullptr);
    REQUIRE(dac != nullptr);
    p.Connect(adc, 0, sub, 0);
    p.Connect(sub, 0, dac, 0);

    YSE::DSP::patcherInsert insert(p);
    MULTICHANNELBUFFER io;
    io.resize(1);
    io[0].resize(128);
    fillPattern(io[0], 0.7f);
    YSE::DSP::buffer dry(io[0]);

    insert.process(io);
    CHECK(exactlyEqual(io[0], dry));
  }

  // ─── the architecture #545 established, still standing ─────────────────────

  TEST_CASE("subpatcher signal: the cord is recorded against the boundary object (#764, #545)") {
    // The compiled graph must contain no subpatchers, which is what keeps the
    // audio thread free of nesting. A signal cord to "inlet 0 of the group" has
    // to end up as an ordinary DSP edge naming the `~inlet` object, exactly as
    // a control cord names the `.inlet`.
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* sine = p.CreateObject(YSE::OBJ::D_SINE, "440");
    YSE::pHandle* sub = p.CreateObject(YSE::OBJ::PATCHER, "");
    YSE::pHandle* in = p.CreateObject(YSE::OBJ::D_INLET, "0");
    REQUIRE(sine != nullptr);
    REQUIRE(sub != nullptr);
    REQUIRE(in != nullptr);
    p.SetContainer(in, sub);
    p.Connect(sine, 0, sub, 0);

    REQUIRE(sine->GetConnections(0) == 1);
    CHECK(sine->GetConnectionTarget(0, 0) == in->GetID()); // the `~inlet`...
    CHECK(sine->GetConnectionTarget(0, 0) != sub->GetID()); // ...not the façade
    CHECK(sine->GetConnectionTargetInlet(0, 0) == 0);

    // And the façade owns no pins, which is why it could not have been the
    // target: its boundary changes whenever a boundary object is added, and a
    // real pin vector would have to be resized on an object the render is
    // walking.
    CHECK(sub->GetInputs() == 0);
    CHECK(sub->GetOutputs() == 0);
  }

  TEST_CASE("subpatcher signal: nesting three deep is still bit-transparent (#764)") {
    // Depth is the property the flat-storage model exists to make free. Three
    // nested pass-through subpatchers means six boundary crossings, and the
    // signal comes back sample for sample — so a crossing neither copies nor
    // delays, however many of them there are, and nesting adds nothing of its
    // own to what the author wrote.
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* adc = p.CreateObject(YSE::OBJ::D_ADC);
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC);
    REQUIRE(adc != nullptr);
    REQUIRE(dac != nullptr);

    // Innermost first, then wrap each in the next one out. Inside a subpatcher
    // the enclosed subpatcher is addressed by pin number exactly as the top
    // level addresses the outermost — the addressing does not know its depth.
    YSE::pHandle* inner = BuildPassthroughSubpatch(p);
    YSE::pHandle* middle = BuildPassthroughSubpatch(p);
    YSE::pHandle* outer = BuildPassthroughSubpatch(p);

    p.SetContainer(inner, middle);
    p.SetContainer(middle, outer);

    // The enclosed group is wired in between its parent's own two boundary
    // objects, replacing the direct inlet->outlet cord. Find those by container
    // and type rather than by ID arithmetic.
    YSE::pHandle* mIn = nullptr;
    YSE::pHandle* mOut = nullptr;
    YSE::pHandle* oIn = nullptr;
    YSE::pHandle* oOut = nullptr;
    for (unsigned i = 0; i < p.Objects(); ++i) {
      YSE::pHandle* h = p.GetHandleFromList(i);
      REQUIRE(h != nullptr);
      const std::string type = h->Type();
      if (p.GetContainer(h) == middle && type == YSE::OBJ::D_INLET) mIn = h;
      if (p.GetContainer(h) == middle && type == YSE::OBJ::D_OUTLET) mOut = h;
      if (p.GetContainer(h) == outer && type == YSE::OBJ::D_INLET) oIn = h;
      if (p.GetContainer(h) == outer && type == YSE::OBJ::D_OUTLET) oOut = h;
    }
    REQUIRE(mIn != nullptr);
    REQUIRE(mOut != nullptr);
    REQUIRE(oIn != nullptr);
    REQUIRE(oOut != nullptr);

    p.Disconnect(mIn, 0, mOut, 0);
    p.Connect(mIn, 0, inner, 0);
    p.Connect(inner, 0, mOut, 0);

    p.Disconnect(oIn, 0, oOut, 0);
    p.Connect(oIn, 0, middle, 0);
    p.Connect(middle, 0, oOut, 0);

    p.Connect(adc, 0, outer, 0);
    p.Connect(outer, 0, dac, 0);

    YSE::DSP::patcherInsert insert(p);
    MULTICHANNELBUFFER io;
    io.resize(1);
    io[0].resize(128);
    fillPattern(io[0], 1.3f);
    YSE::DSP::buffer dry(io[0]);

    insert.process(io);
    CHECK(exactlyEqual(io[0], dry));

    // The control: cut the innermost group out of the chain and the signal
    // stops arriving at all, so the transparency above was measured over a path
    // that really ran through all three.
    p.Disconnect(mIn, 0, inner, 0);
    io[0] = 0.f;
    insert.process(io);
    CHECK(isSilent(io[0]));
  }

  TEST_CASE("subpatcher signal: a signal edit inside a published subpatcher takes effect (#764)") {
    // "A live edit inside a subpatcher goes through the same single atomic
    // GraphState swap as a top-level edit" is an architecture claim; from
    // outside it means the edit takes effect on the next block and the patch
    // keeps rendering. Both halves are asserted — the graph before the edit and
    // the graph after it — because a rig that only looked afterwards could not
    // tell a working edit from a patch that was audible all along.
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC);
    YSE::pHandle* sub = p.CreateObject(YSE::OBJ::PATCHER, "");
    YSE::pHandle* sine = p.CreateObject(YSE::OBJ::D_SINE, "440");
    YSE::pHandle* out = p.CreateObject(YSE::OBJ::D_OUTLET, "0");
    REQUIRE(dac != nullptr);
    REQUIRE(sub != nullptr);
    for (YSE::pHandle* h : {sine, out}) {
      REQUIRE(h != nullptr);
      p.SetContainer(h, sub);
    }
    p.Connect(sub, 0, dac, 0);

    YSE::DSP::patcherInsert insert(p);
    MULTICHANNELBUFFER io;
    io.resize(1);
    io[0].resize(128);

    // Before: the boundary is wired to the parent but nothing inside feeds it.
    io[0] = 0.f;
    insert.process(io);
    REQUIRE(isSilent(io[0]));

    // The edit — an ordinary Connect that happens to be inside a subpatcher, on
    // a patcher that has already rendered and published.
    p.Connect(sine, 0, out, 0);

    io[0] = 0.f;
    insert.process(io);
    CHECK(measureRms(io[0]) > 0.1f);
  }

  TEST_CASE("subpatcher signal: a signal subpatch round-trips through JSON (#764)") {
    // Boundary objects are ordinary objects and boundary cords are ordinary
    // edges, so persistence should need to know nothing about either. What that
    // means from outside is that a saved signal subpatch still passes audio
    // after a reload.
    std::string dump;
    {
      YSE::patcher src;
      src.create(1);
      YSE::pHandle* adc = src.CreateObject(YSE::OBJ::D_ADC);
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC);
      YSE::pHandle* sub = BuildPassthroughSubpatch(src);
      src.Connect(adc, 0, sub, 0);
      src.Connect(sub, 0, dac, 0);
      dump = src.DumpJSON();
    }
    CHECK(dump.find("~inlet") != std::string::npos);
    CHECK(dump.find("~outlet") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(1);
    loaded.ParseJSON(dump);
    CHECK(loaded.Objects() == 5);

    // The reloaded graph is still a bit-transparent insert, which is what
    // proves the containment *and* the boundary cords survived: lose either and
    // this goes silent.
    YSE::DSP::patcherInsert insert(loaded);
    MULTICHANNELBUFFER io;
    io.resize(1);
    io[0].resize(128);
    fillPattern(io[0], 2.4f);
    YSE::DSP::buffer dry(io[0]);
    insert.process(io);
    CHECK(exactlyEqual(io[0], dry));
  }

  // ─── the two rates share one boundary ──────────────────────────────────────

  TEST_CASE("subpatcher signal: the two rates share one pin index space (#764)") {
    // A subpatcher has one set of inlet pins, numbered by the parent; "inlet 1"
    // is a single pin whose *rate* is a property of what sits behind it. So a
    // subpatcher with a `.inlet 0` and a `~inlet 1` presents two inlets, not one
    // of each, and each pin number resolves to the boundary object claiming it
    // whichever rate that object is.
    //
    //   inside: `.inlet 0` -> `.+ 10` -> `.outlet 0`   (the message pin)
    //           `~inlet 1` -> `~outlet 1`              (the signal pin)
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* adc = p.CreateObject(YSE::OBJ::D_ADC);
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC);
    YSE::pHandle* sink = p.CreateObject(YSE::OBJ::G_INT, "");
    YSE::pHandle* sub = p.CreateObject(YSE::OBJ::PATCHER, "");
    YSE::pHandle* cIn = p.CreateObject(YSE::OBJ::G_INLET, "0");
    YSE::pHandle* add = p.CreateObject(YSE::OBJ::G_ADD, "10");
    YSE::pHandle* cOut = p.CreateObject(YSE::OBJ::G_OUTLET, "0");
    YSE::pHandle* sIn = p.CreateObject(YSE::OBJ::D_INLET, "1");
    YSE::pHandle* sOut = p.CreateObject(YSE::OBJ::D_OUTLET, "1");
    REQUIRE(sub != nullptr);
    REQUIRE(sink != nullptr);
    for (YSE::pHandle* h : {cIn, add, cOut, sIn, sOut}) {
      REQUIRE(h != nullptr);
      p.SetContainer(h, sub);
    }
    p.Connect(cIn, 0, add, 0);
    p.Connect(add, 0, cOut, 0);
    p.Connect(sIn, 0, sOut, 0);

    // Two pins per side, counted across both rates.
    CHECK(p.SubpatcherInlets(sub) == 2);
    CHECK(p.SubpatcherOutlets(sub) == 2);

    p.Connect(adc, 0, sub, 1); // signal into pin 1
    p.Connect(sub, 1, dac, 0); // signal out of pin 1
    p.Connect(sub, 0, sink, 0); // message out of pin 0

    // The message pin still works, unchanged by the signal pin next to it.
    sub->SetIntData(0, 5);
    CHECK(sink->GetGuiValue() == "15");

    // ...and the signal pin carries audio at the same time.
    YSE::DSP::patcherInsert insert(p);
    MULTICHANNELBUFFER io;
    io.resize(1);
    io[0].resize(128);
    fillPattern(io[0], 3.1f);
    YSE::DSP::buffer dry(io[0]);
    insert.process(io);
    CHECK(exactlyEqual(io[0], dry));
  }

  TEST_CASE("subpatcher signal: connecting to a pin the boundary does not have is refused (#764)") {
    // The refusal has to be silent-and-safe rather than a crash or a wrong
    // edge: a wrong edge is the failure mode that only shows up later, as audio
    // arriving somewhere nobody wired it to.
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* sine = p.CreateObject(YSE::OBJ::D_SINE, "440");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC);
    YSE::pHandle* sub = BuildPassthroughSubpatch(p);
    REQUIRE(sine != nullptr);
    REQUIRE(dac != nullptr);

    p.Connect(sine, 0, sub, 4); // no boundary object claims inlet 4
    CHECK(sine->GetConnections(0) == 0);
    p.Connect(sub, 4, dac, 0); // nor outlet 4

    YSE::DSP::patcherInsert insert(p);
    MULTICHANNELBUFFER io;
    io.resize(1);
    io[0].resize(128);
    io[0] = 0.f;
    insert.process(io);
    CHECK(isSilent(io[0])); // neither refusal wired anything up by accident

    // The pins that do exist still resolve, so the refusal was specific rather
    // than a boundary that stopped resolving anything.
    p.Connect(sine, 0, sub, 0);
    p.Connect(sub, 0, dac, 0);
    io[0] = 0.f;
    insert.process(io);
    CHECK(measureRms(io[0]) > 0.1f);
  }

} // TEST_SUITE("patcher")
