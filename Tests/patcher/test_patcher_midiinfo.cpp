// Tests for `.midiinfo` (issue #536) — the patcher's MIDI port directory.
//
// What is being pinned:
//
//   - **the report itself**, which is the object: a bang sends the port count
//     out the right outlet first, then an index and a name for each port, in
//     index order and index before name, so a patch can pair a device's name
//     with the bare number every other MIDI object takes as its `port`
//     argument;
//   - **the count is the machine's**, not a number the object made up: it is
//     asserted against `MIDI::deviceManager` directly, so the test says
//     something real on a workstation with ports and stays honest on CI with
//     none (where the assertion becomes "count 0, and nothing follows");
//   - **the two directions are separate sets**: the `direction` creation
//     argument picks one, the `input` / `output` messages switch it live, and
//     each reports its own count — input port 1 and output port 1 are
//     unrelated devices;
//   - **enumeration is a control-thread act**: a standalone object that never
//     joined a patcher has asked the platform nothing and reports 0, which is
//     the observable half of "the ports are read in SetParent";
//   - **the re-entrancy guard**: a patch that wires an outlet back into the
//     inlet is refused and counted rather than allowed to recurse without
//     bound;
//   - **the parameter surviving a DumpJSON / ParseJSON round trip**, read back
//     through the report rather than through an accessor;
//   - **complete documentation metadata**, repeated from the registry-wide
//     doc-coverage test so a regression names this object directly.
//
// The end-to-end sections drive the real machinery: a registry-built object in
// a real `patcherImplementation`, with real cords to taps on all three outlets
// writing into one shared log, so the *order* of the sends is an assertion
// rather than an inference.
//
// No audio device and no MIDI hardware required. The whole TU sits behind
// YSE_ENABLE_MIDI_DEVICE with the object it tests (see mMidiInfo.h) — with no
// backend there are no ports to enumerate and the object is not registered.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include <vector>

#include "headers/defines.hpp"

#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObject.h"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"

#if YSE_ENABLE_MIDI_DEVICE
#include "midi/midiDeviceManager.h"
#include "patcher/inlet.h"
#include "patcher/midi/mMidiInfo.h"
#include "patcher/patcherImplementation.h"
#include "sinks.hpp"
#endif

using YSE::PATCHER::Register;

#if YSE_ENABLE_MIDI_DEVICE

namespace {

  using YSE::PATCHER::mMidiInfo;
  using YSE::PATCHER::patcherImplementation;

  // How many ports this machine has, as the object's own snapshot would see
  // them: the backend's count, capped at the table size. 0 on CI.
  int ExpectedCount(bool input) {
    const unsigned int available = input ? YSE::MIDI::DeviceManager().getNumMidiInDevices()
                                         : YSE::MIDI::DeviceManager().getNumMidiOutDevices();
    if (available > static_cast<unsigned int>(mMidiInfo::PORTS_MAX)) return mMidiInfo::PORTS_MAX;
    return static_cast<int>(available);
  }

  // Records every send tagged with the outlet that delivered it, into a log
  // shared by all the taps of one rig — which is what makes "count first, then
  // index, then name" an assertion rather than an inference.
  struct Tap : YSE::PATCHER::pObject {
    std::vector<std::string>* log = nullptr;
    std::string tag;

    Tap() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterInt([this](int v, int, YSE::THREAD) {
        if (log) log->push_back(tag + ":" + std::to_string(v));
      });
      inputs.back().RegisterList([this](const std::string& v, int, YSE::THREAD) {
        if (log) log->push_back(tag + ":" + v);
      });
    }
    const char* Type() const override {
      return "midiinfo_tap";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // A registry-built `.midiinfo` in a real patcher, with one Tap on each outlet
  // writing into one shared log.
  struct Rig {
    patcherImplementation patch{1, nullptr};
    std::vector<std::string> log;
    std::vector<std::unique_ptr<Tap>> taps;
    std::vector<std::unique_ptr<YSE::pHandle>> tapHandles;
    YSE::pHandle* object = nullptr;

    // `roundTrip` builds the object in a scratch patcher, dumps that to JSON
    // and loads it into this one, so the object under test is the one that came
    // back from storage. A round-trip case can then make exactly the assertions
    // a direct one makes, which is what "the parameter survived" has to mean.
    explicit Rig(const std::string& args, bool roundTrip = false) {
      if (roundTrip) {
        patcherImplementation src{1, nullptr};
        REQUIRE(src.CreateObject(YSE::OBJ::M_MIDIINFO, args) != nullptr);
        const std::string json = src.DumpJSON();
        patch.ParseJSON(json);
        REQUIRE(patch.Objects() == 1);
        object = patch.GetHandleFromList(0);
      } else {
        object = patch.CreateObject(YSE::OBJ::M_MIDIINFO, args);
      }
      REQUIRE(object != nullptr);
      log.reserve(128);
      for (int i = 0; i < object->GetOutputs(); i++) {
        auto tap = std::make_unique<Tap>();
        tap->log = &log;
        tap->tag = "o" + std::to_string(i);
        auto handle = std::make_unique<YSE::pHandle>(tap.get());
        patch.Connect(object, i, handle.get(), 0);
        taps.push_back(std::move(tap));
        tapHandles.push_back(std::move(handle));
      }
    }

    void Bang() {
      log.clear();
      object->SetBang(0);
    }
  };

} // namespace

#endif // YSE_ENABLE_MIDI_DEVICE

TEST_SUITE("patcher") {

  // ─── registration and shape ───────────────────────────────────────────────

  TEST_CASE("midiinfo: registered exactly where the backend is (#536)") {
    auto names = Register().AllNames();
    bool registered = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::M_MIDIINFO)) registered = true;
    }

#if YSE_ENABLE_MIDI_DEVICE
    CHECK(registered);
    std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(YSE::OBJ::M_MIDIINFO));
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == std::string(YSE::OBJ::M_MIDIINFO));
    // One inlet for the bang; name, index and count leaving.
    CHECK(obj->NumInputs() == 1);
    CHECK(obj->NumOutputs() == 3);
    CHECK(obj->GetOutputType(0) == YSE::OUT_TYPE::LIST);
    CHECK(obj->GetOutputType(1) == YSE::OUT_TYPE::INT);
    CHECK(obj->GetOutputType(2) == YSE::OUT_TYPE::INT);
    // Control-rate: the object is driven by its inlet, not by the render pass,
    // so it must not ask to be polled and must not claim to be a DSP object.
    CHECK_FALSE(obj->IsDSPObject());
    CHECK_FALSE(obj->WantsBlockPoll());
#else
    // Without a backend there is nothing to enumerate, so the object is not
    // built and not registered — the same carve-out `.midiout` and the input
    // family already have.
    CHECK_FALSE(registered);
#endif
  }

  TEST_CASE("midiinfo: documents itself (#536)") {
#if YSE_ENABLE_MIDI_DEVICE
    std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(YSE::OBJ::M_MIDIINFO));
    REQUIRE(obj != nullptr);

    CHECK_FALSE(obj->GetDescription().empty());
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MIDI);
    for (int i = 0; i < obj->NumInputs(); i++) {
      CHECK_FALSE(obj->GetInlet(i)->GetDocLabel().empty());
      CHECK_FALSE(obj->GetInlet(i)->GetDocDescription().empty());
    }
    for (int i = 0; i < obj->NumOutputs(); i++) {
      CHECK_FALSE(obj->GetOutlet(i)->GetDocLabel().empty());
      CHECK_FALSE(obj->GetOutlet(i)->GetDocDescription().empty());
    }
    // One creation argument — the direction — and it must be documented.
    REQUIRE(obj->GetParamDocs().size() == 1);
    CHECK(obj->GetParamDocs()[0].name == "direction");
    CHECK_FALSE(obj->GetParamDocs()[0].doc.empty());
#endif
  }

#if YSE_ENABLE_MIDI_DEVICE

  // ─── the report, end to end through a real patcher ────────────────────────

  TEST_CASE("midiinfo: a bang reports the count, then an index and a name each") {
    Rig rig("");
    const int expected = ExpectedCount(true);

    rig.Bang();

    // The count comes first, alone, and it is the machine's own.
    REQUIRE(rig.log.size() == static_cast<std::size_t>(1 + 2 * expected));
    CHECK(rig.log[0] == "o2:" + std::to_string(expected));

    // Then one index / name pair per port, in index order, index first — so
    // whatever the name outlet triggers already knows which port is being
    // named.
    for (int i = 0; i < expected; i++) {
      CAPTURE(i);
      CHECK(rig.log[1 + 2 * i] == "o1:" + std::to_string(i));
      const std::string& named = rig.log[2 + 2 * i];
      CHECK(named.rfind("o0:", 0) == 0);
      // A real port always has a name; this is the half of the assertion that
      // only runs on a machine with hardware.
      CHECK(named.size() > 3);
    }
  }

  TEST_CASE("midiinfo: a machine with no ports sends a count of 0 and nothing else") {
    // Not a hypothetical: this is what CI sees. Written so it asserts the same
    // property either way — the log is exactly as long as the count says.
    Rig rig("");
    rig.Bang();
    REQUIRE_FALSE(rig.log.empty());
    const int reported = std::stoi(rig.log[0].substr(3));
    CHECK(rig.log.size() == static_cast<std::size_t>(1 + 2 * reported));
    if (reported == 0) CHECK(rig.log.size() == 1);
  }

  TEST_CASE("midiinfo: banging twice reports the same list") {
    // The snapshot is taken once, on the control thread, and read from then on
    // — so two bangs cannot disagree.
    Rig rig("");
    rig.Bang();
    const std::vector<std::string> first = rig.log;
    rig.Bang();
    CHECK(rig.log == first);
  }

  // ─── input ports and output ports are separate sets ───────────────────────

  TEST_CASE("midiinfo: the direction argument picks which set is reported") {
    Rig in("input");
    in.Bang();
    REQUIRE_FALSE(in.log.empty());
    CHECK(in.log[0] == "o2:" + std::to_string(ExpectedCount(true)));

    Rig out("output");
    out.Bang();
    REQUIRE_FALSE(out.log.empty());
    CHECK(out.log[0] == "o2:" + std::to_string(ExpectedCount(false)));
  }

  TEST_CASE("midiinfo: an unrecognised direction argument reads as input") {
    Rig rig("sideways");
    rig.Bang();
    REQUIRE_FALSE(rig.log.empty());
    CHECK(rig.log[0] == "o2:" + std::to_string(ExpectedCount(true)));
  }

  TEST_CASE("midiinfo: the input / output messages switch direction live") {
    Rig rig("");
    rig.log.clear();
    rig.object->SetListData(0, "output");
    rig.Bang();
    REQUIRE_FALSE(rig.log.empty());
    CHECK(rig.log[0] == "o2:" + std::to_string(ExpectedCount(false)));

    rig.object->SetListData(0, "input");
    rig.Bang();
    REQUIRE_FALSE(rig.log.empty());
    CHECK(rig.log[0] == "o2:" + std::to_string(ExpectedCount(true)));

    // Switching costs no re-enumeration: both sets were snapshotted together,
    // so an unknown message leaves the direction where it was.
    rig.object->SetListData(0, "sideways");
    rig.Bang();
    REQUIRE_FALSE(rig.log.empty());
    CHECK(rig.log[0] == "o2:" + std::to_string(ExpectedCount(true)));
  }

  // ─── storage ──────────────────────────────────────────────────────────────

  TEST_CASE("midiinfo: the direction survives a DumpJSON / ParseJSON round trip") {
    Rig rig("output", /*roundTrip=*/true);
    CHECK(rig.object->GetParams() == "output");
    CHECK(rig.object->GetName() == std::string(YSE::OBJ::M_MIDIINFO));

    // Read back through behaviour, not through an accessor: the reloaded object
    // must report the *output* ports.
    rig.Bang();
    REQUIRE_FALSE(rig.log.empty());
    CHECK(rig.log[0] == "o2:" + std::to_string(ExpectedCount(false)));
  }

  // ─── the snapshot is taken on joining a patcher ───────────────────────────

  TEST_CASE("midiinfo: a standalone object has asked the platform nothing") {
    // Enumeration allocates and talks to the platform's MIDI service, so it
    // happens once, in SetParent, on the control thread. An object that never
    // joined a patcher therefore reports an empty machine — the observable half
    // of that contract.
    mMidiInfo object;
    CHECK(object.PortCount() == 0);
    CHECK(object.PortName(0).empty());
    CHECK(object.Direction() == mMidiInfo::DIR_INPUT);
  }

  TEST_CASE("midiinfo: joining a patcher is what reads the ports") {
    patcherImplementation patch{1, nullptr};
    mMidiInfo object;
    object.SetParent(&patch);

    CHECK(object.PortCount() == ExpectedCount(true));
    for (int i = 0; i < object.PortCount(); i++) {
      CAPTURE(i);
      CHECK_FALSE(object.PortName(i).empty());
    }
    // Out of range in both directions, rather than reading past the table.
    CHECK(object.PortName(-1).empty());
    CHECK(object.PortName(mMidiInfo::PORTS_MAX).empty());
    CHECK(object.PortName(object.PortCount()).empty());
  }

  TEST_CASE("midiinfo: both directions are snapshotted together") {
    patcherImplementation patch{1, nullptr};
    mMidiInfo object;
    object.SetParent(&patch);

    CHECK(object.PortCount() == ExpectedCount(true));
    object.SetMessage("output", 0.f);
    CHECK(object.Direction() == mMidiInfo::DIR_OUTPUT);
    CHECK(object.PortCount() == ExpectedCount(false));
    object.SetMessage("input", 0.f);
    CHECK(object.Direction() == mMidiInfo::DIR_INPUT);
    CHECK(object.PortCount() == ExpectedCount(true));
  }

  // ─── the re-entrancy guard ────────────────────────────────────────────────

  TEST_CASE("midiinfo: an outlet wired back into the inlet is refused, not recursed") {
    // The reporting walk uses one scratch string and would otherwise recurse
    // without bound. A patch can wire this by accident — the count outlet into
    // a counter and back — so the object has to survive it rather than the
    // patch having to avoid it.
    struct Rebang : YSE::PATCHER::pObject {
      YSE::PATCHER::pObject* target = nullptr;
      int hits = 0;

      Rebang() : pObject(false) {
        inputs.emplace_back(this, true, 0);
        inputs.back().RegisterInt([this](int, int, YSE::THREAD thread) {
          hits++;
          if (target != nullptr) target->GetInlet(0)->SetBang(thread);
        });
      }
      const char* Type() const override {
        return "midiinfo_rebang";
      }
      void Calculate(YSE::THREAD) override {}
      void SetMessage(const std::string&, float) override {}
    };

    patcherImplementation patch{1, nullptr};
    // The sink is declared before the object that sends to it, so the sender
    // dies first — sinks.hpp's teardown rule.
    Rebang loop;
    mMidiInfo object;
    object.SetParent(&patch);
    loop.target = &object;
    TestHelpers::Wire(object, 2, loop, 0);

    CHECK(object.Dropped() == 0);
    object.GetInlet(0)->SetBang(YSE::T_GUI);
    // The count reached the loop object exactly once, and the bang it sent
    // straight back was refused rather than starting the walk over.
    CHECK(loop.hits == 1);
    CHECK(object.Dropped() == 1);

    // And the object still works afterwards: the guard was released, not stuck.
    object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(loop.hits == 2);
    CHECK(object.Dropped() == 2);
  }

#endif // YSE_ENABLE_MIDI_DEVICE

} // TEST_SUITE
