// Tests for .array.stream (issue #806) — Max's array.stream on the
// name-addressed value model .array settled (#548).
//
// What has to be proven, and what every case below is one of:
//
//   - **the array is bound from the creation argument.** An array never
//     travels down a cord, so ".array.stream <name> [<size>]" resolves the
//     name once, on the control thread, and an `array <name>` message is
//     honoured only when it names the array already bound —
//     ArrayReferenceNames' bounded compare, never a registry lookup on a
//     message path.
//   - **the array is always the last <size> values.** Every arriving value
//     appends and the oldest slides off the front once the window is full —
//     the O(n) shift #806 accepts, bounded at the store's 256 — and however
//     many atoms one message carries, the whole collect is one hold of the
//     store's guard.
//   - **the window leaves when it is full — .zl stream's rule.** The
//     shortfall leaves the right outlet after every collect that lands, the
//     reference only when the array holds the full window, and the two go
//     right to left.
//   - **no window, no collect.** Size 0 — the unconfigured default — refuses
//     every trigger, counted: malformed, not a miss. A window is 1..256,
//     refused rather than clamped.
//   - **nothing on a message path allocates.** In-patcher delivery
//     dispatches on T_DSP, so "the audio thread streams a value" is the
//     ordinary case.
//
// No audio device and no engine of its own. The registry is process-wide, so
// every case that names an array uses names of its own — one case's contents
// must not be visible to the next.

#include <doctest/doctest.h>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "patcher/genericObjects/gArray.h"
#include "patcher/genericObjects/gArrayStream.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"

using TestHelpers::IntSink;
using TestHelpers::MultiSink;
using TestHelpers::OrderSink;
using TestHelpers::Wire;
using YSE::PATCHER::gArray;
using YSE::PATCHER::gArrayStream;

namespace {

  // An .array and one .array.stream on one name, sharing one
  // patcherImplementation so the name actually binds ("<patcherName>.<name>"
  // needs a patcher to prefix with — a parentless object stays private). The
  // keeper is also the witness: its Count()/ElementAt() read back what the
  // stream left in the shared store. The sinks are declared before the
  // objects so they are torn down last, while the outlets wired to them
  // still exist (see sinks.hpp on why that matters).
  struct Rig {
    MultiSink ref; // outlet 0: the reference, only when the window is full
    IntSink shortfall; // outlet 1: how many values the window still needs
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    gArray keeper;
    gArrayStream object;

    Rig(const std::string& patcherName, const std::string& name,
        const std::string& params = std::string()) {
      p.SetName(patcherName);
      keeper.SetParent(&p);
      keeper.SetParams(name);
      object.SetParent(&p);
      object.SetParams(params.empty() ? name : params);
      Wire(object, 0, ref);
      Wire(object, 1, shortfall);
    }

    void reset() {
      ref.reset();
      shortfall.gotInt = false;
      shortfall.received = -999;
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("array.stream: registered, with its inlets and outlets (#806)") {
    YSE::patcher p;
    p.create(2);

    // Value, size, reference — gArrayFill's arrangement; the reference and
    // the shortfall leave.
    YSE::pHandle* stream = p.CreateObject(YSE::OBJ::G_ARRAY_STREAM);
    REQUIRE(stream != nullptr);
    CHECK(std::string(stream->Type()) == ".array.stream");
    CHECK(stream->GetInputs() == 3);
    CHECK(stream->GetOutputs() == 2);

    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_ARRAY_STREAM)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("array.stream: the value inlet takes the stream, the cold inlets only their "
            "configuration (#806)") {
    gArrayStream stream;
    const unsigned int value = stream.GetInlet(0)->GetAcceptedTypes();
    CHECK((value & YSE::PATCHER::IT_BANG) != 0);
    CHECK((value & YSE::PATCHER::IT_INT) != 0);
    CHECK((value & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((value & YSE::PATCHER::IT_LIST) != 0);
    const unsigned int size = stream.GetInlet(1)->GetAcceptedTypes();
    CHECK((size & YSE::PATCHER::IT_INT) != 0);
    CHECK((size & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((size & YSE::PATCHER::IT_LIST) == 0);
    CHECK((size & YSE::PATCHER::IT_BANG) == 0);
    const unsigned int reference = stream.GetInlet(2)->GetAcceptedTypes();
    CHECK((reference & YSE::PATCHER::IT_LIST) != 0);
    CHECK((reference & YSE::PATCHER::IT_BANG) == 0);
  }

  // ─── the slide ──────────────────────────────────────────────────────────────

  TEST_CASE("array.stream: the array is always the last <size> values, and the reference "
            "leaves only once the window is full (#806)") {
    Rig rig("aps806a", "s806a", "s806a 3");

    // Filling: the shortfall counts down, and no reference leaves while the
    // window is short — .zl stream's rule.
    rig.object.GetInlet(0)->SetInt(10, YSE::T_GUI);
    CHECK(rig.shortfall.gotInt);
    CHECK(rig.shortfall.received == 2);
    CHECK_FALSE(rig.ref.gotList);

    rig.reset();
    rig.object.GetInlet(0)->SetInt(20, YSE::T_GUI);
    CHECK(rig.shortfall.received == 1);
    CHECK_FALSE(rig.ref.gotList);

    rig.reset();
    rig.object.GetInlet(0)->SetInt(30, YSE::T_GUI);
    CHECK(rig.shortfall.received == 0);
    CHECK(rig.ref.gotList);
    CHECK(rig.ref.listValue == "array s806a");
    CHECK(rig.keeper.Count() == 3);

    // Full: the fourth value slides the first off the front, so the array is
    // the last three — 20 30 40 — and the reference leaves again.
    rig.reset();
    rig.object.GetInlet(0)->SetInt(40, YSE::T_GUI);
    CHECK(rig.shortfall.received == 0);
    CHECK(rig.ref.gotList);
    REQUIRE(rig.keeper.Count() == 3);
    CHECK(rig.keeper.ElementAt(0) == "20");
    CHECK(rig.keeper.ElementAt(1) == "30");
    CHECK(rig.keeper.ElementAt(2) == "40");
    CHECK(rig.object.Dropped() == 0);
  }

  TEST_CASE("array.stream: every value lands as the text that spells it (#806)") {
    // An int, a float and a symbol, each one element by its spelling —
    // ExprFormatValue's rule, so 7.5 stays visibly a float and reads back as
    // one.
    Rig rig("aps806b", "s806b", "s806b 3");
    rig.object.GetInlet(0)->SetInt(10, YSE::T_GUI);
    rig.object.GetInlet(0)->SetFloat(7.5f, YSE::T_GUI);
    rig.object.GetInlet(0)->SetList("c4", YSE::T_GUI);
    REQUIRE(rig.keeper.Count() == 3);
    CHECK(rig.keeper.ElementAt(0) == "10");
    CHECK(rig.keeper.ElementAt(1) == "7.5");
    CHECK(rig.keeper.ElementAt(2) == "c4");

    // A non-finite float has no spelling that reads back — refused, counted,
    // nothing landed.
    rig.object.GetInlet(0)->SetFloat(std::numeric_limits<float>::quiet_NaN(), YSE::T_GUI);
    CHECK(rig.object.Dropped() == 1);
    CHECK(rig.keeper.Count() == 3);
  }

  TEST_CASE("array.stream: a list is collected whole, in order, with one announcement — "
            "shortfall before reference (#806)") {
    // Six atoms through a window of four: the array ends holding the last
    // four of the combined sequence, and the message announces once — the
    // shortfall first, then the reference, Max's outlets firing right to
    // left. OrderSink's log is the proof a counter could not give.
    std::vector<char> log;
    OrderSink refOrder;
    OrderSink shortOrder;
    refOrder.log = &log;
    refOrder.tag = 'r';
    shortOrder.log = &log;
    shortOrder.tag = 's';
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("aps806c");
    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("s806c");
    gArrayStream stream;
    stream.SetParent(&p);
    stream.SetParams("s806c 4");
    Wire(stream, 0, refOrder);
    Wire(stream, 1, shortOrder);

    stream.GetInlet(0)->SetList("1 2 3 4 5 6", YSE::T_GUI);
    REQUIRE(keeper.Count() == 4);
    CHECK(keeper.ElementAt(0) == "3");
    CHECK(keeper.ElementAt(1) == "4");
    CHECK(keeper.ElementAt(2) == "5");
    CHECK(keeper.ElementAt(3) == "6");
    REQUIRE(log.size() == 2);
    CHECK(log[0] == 's');
    CHECK(log[1] == 'r');
    CHECK(shortOrder.lastInt == 0);
    CHECK(refOrder.lastList == "array s806c");
    CHECK(stream.Dropped() == 0);
  }

  TEST_CASE("array.stream: a list with an atom no element can hold is refused whole (#806)") {
    Rig rig("aps806d", "s806d", "s806d 3");
    rig.object.GetInlet(0)->SetInt(10, YSE::T_GUI);
    rig.reset();

    // 65 characters outruns ELEMENT_CAPACITY: the whole message is refused —
    // one counted refusal, nothing changed, nothing announced — rather than
    // the valid prefix landing. gArrayEndsWriter's whole-or-nothing rule.
    const std::string overlong(65, 'x');
    rig.object.GetInlet(0)->SetList("20 " + overlong + " 30", YSE::T_GUI);
    CHECK(rig.object.Dropped() == 1);
    CHECK_FALSE(rig.shortfall.gotInt);
    CHECK_FALSE(rig.ref.gotList);
    REQUIRE(rig.keeper.Count() == 1);
    CHECK(rig.keeper.ElementAt(0) == "10");
  }

  // ─── the window ─────────────────────────────────────────────────────────────

  TEST_CASE("array.stream: no window, no collect — size 0 is unconfigured and every trigger "
            "is refused (#806)") {
    // A bare ".array.stream <name>" has no window: a value and a bang are
    // both malformed, not a miss — refused whole, counted, nothing landed.
    Rig rig("aps806e", "s806e");
    CHECK(rig.object.StoredSize() == 0);
    rig.object.GetInlet(0)->SetInt(10, YSE::T_GUI);
    CHECK(rig.object.Dropped() == 1);
    CHECK(rig.keeper.Count() == 0);
    CHECK_FALSE(rig.shortfall.gotInt);
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.object.Dropped() == 2);

    // An int on the size inlet opens the window — silently, nothing leaves —
    // and the stream starts collecting.
    rig.object.GetInlet(1)->SetInt(2, YSE::T_GUI);
    CHECK(rig.object.StoredSize() == 2);
    CHECK_FALSE(rig.shortfall.gotInt);
    rig.object.GetInlet(0)->SetInt(10, YSE::T_GUI);
    rig.object.GetInlet(0)->SetInt(20, YSE::T_GUI);
    CHECK(rig.ref.gotList);
    CHECK(rig.keeper.Count() == 2);
    CHECK(rig.object.Dropped() == 2);
  }

  TEST_CASE("array.stream: the size inlet refuses what is not a window — 0, negative, past "
            "the store — and never clamps (#806)") {
    Rig rig("aps806f", "s806f", "s806f 3");
    rig.object.GetInlet(1)->SetInt(0, YSE::T_GUI);
    CHECK(rig.object.Dropped() == 1);
    CHECK(rig.object.StoredSize() == 3);
    rig.object.GetInlet(1)->SetInt(-1, YSE::T_GUI);
    CHECK(rig.object.Dropped() == 2);
    rig.object.GetInlet(1)->SetInt(257, YSE::T_GUI);
    CHECK(rig.object.Dropped() == 3);
    CHECK(rig.object.StoredSize() == 3);

    // A float truncates to an int first — Max's float method on an int
    // attribute — and a non-finite one is refused rather than quietly
    // becoming size 0.
    rig.object.GetInlet(1)->SetFloat(4.9f, YSE::T_GUI);
    CHECK(rig.object.StoredSize() == 4);
    CHECK(rig.object.Dropped() == 3);
    rig.object.GetInlet(1)->SetFloat(std::numeric_limits<float>::infinity(), YSE::T_GUI);
    CHECK(rig.object.Dropped() == 4);
    CHECK(rig.object.StoredSize() == 4);
  }

  TEST_CASE("array.stream: a window narrowed live trims at the next trigger, and a bang "
            "re-announces without sliding (#806)") {
    Rig rig("aps806g", "s806g", "s806g 4");
    rig.object.GetInlet(0)->SetList("10 20 30 40", YSE::T_GUI);
    REQUIRE(rig.keeper.Count() == 4);

    // The cold inlet touches no shared data: the array still holds four.
    rig.reset();
    rig.object.GetInlet(1)->SetInt(2, YSE::T_GUI);
    CHECK(rig.keeper.Count() == 4);
    CHECK_FALSE(rig.shortfall.gotInt);

    // The next trigger trims — oldest first, .zl's trim-on-bang — and
    // announces the now-full window.
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(rig.keeper.Count() == 2);
    CHECK(rig.keeper.ElementAt(0) == "30");
    CHECK(rig.keeper.ElementAt(1) == "40");
    CHECK(rig.shortfall.received == 0);
    CHECK(rig.ref.gotList);

    // A second bang re-announces the same window: nothing slides, nothing
    // changes.
    rig.reset();
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.keeper.Count() == 2);
    CHECK(rig.keeper.ElementAt(0) == "30");
    CHECK(rig.shortfall.received == 0);
    CHECK(rig.ref.gotList);

    // Widened, the window re-opens: the shortfall reports the gap and the
    // reference stays silent until the stream refills it.
    rig.reset();
    rig.object.GetInlet(1)->SetInt(3, YSE::T_GUI);
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.shortfall.received == 1);
    CHECK_FALSE(rig.ref.gotList);
    rig.reset();
    rig.object.GetInlet(0)->SetInt(50, YSE::T_GUI);
    CHECK(rig.shortfall.received == 0);
    CHECK(rig.ref.gotList);
    REQUIRE(rig.keeper.Count() == 3);
    CHECK(rig.keeper.ElementAt(2) == "50");
    CHECK(rig.object.Dropped() == 0);
  }

  // ─── the reference gesture, and refusals ────────────────────────────────────

  TEST_CASE("array.stream: the bound reference re-announces and is never collected; any "
            "other list is simply atoms (#806)") {
    Rig rig("aps806h", "s806h", "s806h 2");
    rig.object.GetInlet(0)->SetInt(10, YSE::T_GUI);
    rig.object.GetInlet(0)->SetInt(20, YSE::T_GUI);
    rig.reset();

    // The family's gesture: the message an .array's reference outlet emits
    // on a bang re-announces — and collects nothing, a reference being an
    // identity, not an element.
    rig.object.GetInlet(0)->SetList("array s806h", YSE::T_GUI);
    CHECK(rig.ref.gotList);
    CHECK(rig.shortfall.received == 0);
    REQUIRE(rig.keeper.Count() == 2);
    CHECK(rig.keeper.ElementAt(0) == "10");
    CHECK(rig.object.Dropped() == 0);

    // Only the bound name can be recognised at all — the bounded compare is
    // the whole of what a message path may do with a name — so a list naming
    // any other array is simply atoms, collected in order.
    // gArrayEndsWriter's rule.
    rig.object.GetInlet(0)->SetList("array elsewhere", YSE::T_GUI);
    REQUIRE(rig.keeper.Count() == 2);
    CHECK(rig.keeper.ElementAt(0) == "array");
    CHECK(rig.keeper.ElementAt(1) == "elsewhere");
    CHECK(rig.object.Dropped() == 0);

    // The reference inlet acknowledges the bound name silently and refuses
    // anything else — gDictSlice's inlet rule.
    rig.reset();
    rig.object.GetInlet(2)->SetList("array s806h", YSE::T_GUI);
    CHECK(rig.object.Dropped() == 0);
    rig.object.GetInlet(2)->SetList("array elsewhere", YSE::T_GUI);
    CHECK(rig.object.Dropped() == 1);
    CHECK_FALSE(rig.ref.gotList);
    CHECK(rig.keeper.Count() == 2);
  }

  TEST_CASE("array.stream: an unnamed object streams into a private array — the shortfall "
            "counts, the reference stays silent (#806)") {
    // No name, no shared store, no reference to pass on — but an answer is a
    // value, not an identity, so the shortfall still leaves.
    MultiSink ref;
    IntSink shortfall;
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("aps806i");
    gArrayStream stream;
    stream.SetParent(&p);
    Wire(stream, 0, ref);
    Wire(stream, 1, shortfall);
    CHECK(stream.Address().empty());

    stream.GetInlet(1)->SetInt(2, YSE::T_GUI);
    stream.GetInlet(0)->SetInt(10, YSE::T_GUI);
    CHECK(shortfall.gotInt);
    CHECK(shortfall.received == 1);
    stream.GetInlet(0)->SetInt(20, YSE::T_GUI);
    CHECK(shortfall.received == 0);
    CHECK_FALSE(ref.gotList);
    CHECK(stream.Dropped() == 0);
  }

  TEST_CASE("array.stream: wired into the family, the full window drives .array.length "
            "through the public patcher API (#806)") {
    // The flow a patch actually wires, end to end: values into the stream,
    // its reference into .array.length — which reports the window's depth
    // only once the window is full, because that is the only time the
    // reference leaves.
    IntSink length;
    YSE::pHandle lengthHandle(&length);
    YSE::patcher p;
    p.create(2);
    p.name("aps806j");
    YSE::pHandle* array = p.CreateObject(YSE::OBJ::G_ARRAY, "s806j");
    YSE::pHandle* stream = p.CreateObject(YSE::OBJ::G_ARRAY_STREAM, "s806j 2");
    YSE::pHandle* len = p.CreateObject(YSE::OBJ::G_ARRAY_LENGTH, "s806j");
    REQUIRE(array != nullptr);
    REQUIRE(stream != nullptr);
    REQUIRE(len != nullptr);
    p.Connect(stream, 0, len, 0);
    p.Connect(len, 0, &lengthHandle, 0);

    stream->SetIntData(0, 10);
    CHECK_FALSE(length.gotInt);
    stream->SetIntData(0, 20);
    CHECK(length.gotInt);
    CHECK(length.received == 2);
  }

  // ─── binding, and the rename hook ───────────────────────────────────────────

  TEST_CASE("array.stream: patcherImplementation::SetName re-anchors the shared base (#806)") {
    // The rename dispatch itself: an object created *inside* a patcher must
    // be re-anchored by the patcher, without anybody calling RefreshBinding
    // by hand. The keeper holds the old-address store; after the rename the
    // stream slides a fresh empty array under the new prefix, so the first
    // value reports a shortfall of one — while the keeper's contents stay
    // put.
    IntSink shortfall;
    YSE::pHandle shortfallHandle(&shortfall);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("aps806k_before");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("s806k");

    YSE::pHandle* stream = p.CreateObject(YSE::OBJ::G_ARRAY_STREAM, "s806k 2");
    REQUIRE(stream != nullptr);
    p.Connect(stream, 1, &shortfallHandle, 0);

    stream->SetIntData(0, 10);
    stream->SetIntData(0, 20);
    CHECK(shortfall.received == 0);
    CHECK(keeper.Count() == 2);

    p.SetName("aps806k_after");
    stream->SetIntData(0, 30);
    // The collect landed — in the new, empty array — and the keeper's
    // contents were not touched.
    CHECK(shortfall.received == 1);
    CHECK(keeper.Count() == 2);
    CHECK(keeper.ElementAt(0) == "10");
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("array.stream: a value arriving over in-patcher delivery lands on T_DSP (#806)") {
    // A .r feeding the value inlet dispatches on T_DSP when the block drains
    // it (issue #225) — "the audio thread streams a value" is the ordinary
    // case, and the whole path is one bounded render, one guard hold and two
    // sends.
    IntSink shortfall;
    YSE::pHandle shortfallHandle(&shortfall);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("aps806l");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("s806l");

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "go806l");
    YSE::pHandle* stream = p.CreateObject(YSE::OBJ::G_ARRAY_STREAM, "s806l 2");
    REQUIRE(recv != nullptr);
    REQUIRE(stream != nullptr);
    p.Connect(recv, 0, stream, 0);
    p.Connect(stream, 1, &shortfallHandle, 0);

    p.PassData(60, "go806l", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    CHECK(shortfall.gotInt);
    CHECK(shortfall.received == 1);
    CHECK(keeper.Count() == 1);
    CHECK(keeper.ElementAt(0) == "60");
  }

  TEST_CASE("array.stream: no message path allocates (#806)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every message path — the collect (int, float, symbol, multi-atom
    // list, the full-window slide included), the bang, the reference
    // gesture, the foreign-list collect, the size stores and refusals, the
    // reference-inlet acknowledgement, the no-window refusal and the
    // unnamed object's private collect — on T_DSP, in-patcher delivery's
    // thread.
    //
    // Every message is built as a std::string before the scope opens, never
    // passed as a literal inside it — inlet::SetList takes a const
    // std::string&, so a literal at the call site materialises a temporary
    // whenever it outgrows the small-string buffer.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string reference = "array probeAPS806";
    const std::string foreign = "array somewhere_else_long";
    const std::string atoms = "10 7.5 c4";
    const std::string symbol = "c4";

    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("aps806m");
    MultiSink ref;
    IntSink shortfall;
    MultiSink privateRef;
    IntSink privateShortfall;
    gArray array;
    gArrayStream stream;
    gArrayStream unnamed;
    gArrayStream bare;
    array.SetParent(&p);
    array.SetParams("probeAPS806");
    stream.SetParent(&p);
    stream.SetParams("probeAPS806 3");
    unnamed.SetParent(&p);
    bare.SetParent(&p);
    Wire(stream, 0, ref);
    Wire(stream, 1, shortfall);
    Wire(unnamed, 0, privateRef);
    Wire(unnamed, 1, privateShortfall);
    unnamed.GetInlet(1)->SetInt(2, YSE::T_GUI);

    // Warm every path — including the sinks' assignments — so first-call
    // machinery is not what the probe catches.
    for (int i = 0; i < 8; i++)
      stream.GetInlet(0)->SetInt(i, YSE::T_GUI);
    stream.GetInlet(0)->SetFloat(7.5f, YSE::T_GUI);
    stream.GetInlet(0)->SetList(symbol, YSE::T_GUI);
    stream.GetInlet(0)->SetList(atoms, YSE::T_GUI);
    stream.GetInlet(0)->SetList(reference, YSE::T_GUI);
    stream.GetInlet(0)->SetList(foreign, YSE::T_GUI);
    stream.GetInlet(0)->SetBang(YSE::T_GUI);
    stream.GetInlet(1)->SetInt(3, YSE::T_GUI);
    stream.GetInlet(1)->SetFloat(3.f, YSE::T_GUI);
    stream.GetInlet(1)->SetInt(0, YSE::T_GUI); // refused: not a window
    stream.GetInlet(2)->SetList(reference, YSE::T_GUI);
    unnamed.GetInlet(0)->SetInt(1, YSE::T_GUI);
    bare.GetInlet(0)->SetInt(1, YSE::T_GUI); // refused: no window

    const std::uint64_t drops = stream.Dropped();
    const std::uint64_t bareDrops = bare.Dropped();
    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      for (int i = 0; i < 8; i++)
        stream.GetInlet(0)->SetInt(i, YSE::T_DSP); // the full-window slide
      stream.GetInlet(0)->SetFloat(7.5f, YSE::T_DSP);
      stream.GetInlet(0)->SetList(symbol, YSE::T_DSP);
      stream.GetInlet(0)->SetList(atoms, YSE::T_DSP);
      stream.GetInlet(0)->SetList(reference, YSE::T_DSP); // gesture: re-announce
      stream.GetInlet(0)->SetList(foreign, YSE::T_DSP); // atoms, collected
      stream.GetInlet(0)->SetBang(YSE::T_DSP);
      stream.GetInlet(1)->SetInt(3, YSE::T_DSP); // stored, silent
      stream.GetInlet(1)->SetFloat(3.f, YSE::T_DSP); // stored, silent
      stream.GetInlet(1)->SetInt(0, YSE::T_DSP); // refused
      stream.GetInlet(2)->SetList(reference, YSE::T_DSP); // acknowledged
      unnamed.GetInlet(0)->SetInt(1, YSE::T_DSP); // private collect
      bare.GetInlet(0)->SetInt(1, YSE::T_DSP); // refused: no window
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing. The window stayed full so the reference left,
    // the private collect counted its shortfall, and exactly the two
    // refusals were counted.
    CHECK(ref.gotList);
    CHECK(ref.listValue == reference);
    CHECK(shortfall.gotInt);
    CHECK(privateShortfall.gotInt);
    CHECK_FALSE(privateRef.gotList);
    CHECK(stream.Dropped() == drops + 1);
    CHECK(bare.Dropped() == bareDrops + 1);
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("array.stream: params survive a DumpJSON / ParseJSON round trip (#806)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* stream = src.CreateObject(YSE::OBJ::G_ARRAY_STREAM, "notes806 4");
    REQUIRE(stream != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);
    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".array.stream");
    CHECK(copy->GetParams() == std::string("notes806 4"));
    CHECK(copy->GetInputs() == 3);
    CHECK(copy->GetOutputs() == 2);
  }

  TEST_CASE("array.stream: a re-parse resets the window along with the name (#806)") {
    // SetParams("") must not keep sliding the window the previous arguments
    // configured — gArrayPositionBase's rule, and the reset drops the
    // binding back to a private array too.
    Rig rig("aps806n", "s806n", "s806n 2");
    rig.object.GetInlet(0)->SetInt(10, YSE::T_GUI);
    CHECK(rig.keeper.Count() == 1);

    rig.object.SetParams("");
    CHECK(rig.object.Address().empty());
    CHECK(rig.object.StoredSize() == 0);
    rig.object.GetInlet(0)->SetInt(20, YSE::T_GUI);
    CHECK(rig.object.Dropped() == 1);
    CHECK(rig.keeper.Count() == 1);
  }

  TEST_CASE("array.stream: carries complete documentation metadata (#806)") {
    gArrayStream stream;
    CHECK_FALSE(stream.GetDescription().empty());
    CHECK(stream.GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    REQUIRE(stream.GetParamDocs().size() == 2);
    CHECK(stream.GetParamDocs()[0].name == "name");
    CHECK(stream.GetParamDocs()[1].name == "size");
  }
}
