// Tests for `.polypressure` (issue #749) — the MIDI Polyphonic Key Pressure
// message generator, and specifically the fact that it is itself.
//
// `PATCHER_CLASS(className, name)` makes `name` the object's `Type()` return
// value, and `mMidiPolyPressure.h` passed `M_CONTROL` — `.controlchange`'s
// constant — instead of its own. The registry key was right, so
// `CreateObject(".polypressure", ...)` built the right class, but that
// instance then answered `".controlchange"` when asked what it was.
//
// What is being pinned:
//
//   - **the object reports its own name**, from the registry and from a live
//     handle in a real patch;
//   - **a save and a load leave it a `.polypressure`**. This is where the bug
//     did its damage rather than merely misinformed: `Type()` is what
//     `DumpJSON` writes and what `ParseJSON` looks up, so the patch was
//     *stored* as a `.controlchange` and came back as one — a 0xB0 status byte
//     instead of 0xA0, one inlet instead of two, and the second inlet's
//     pressure silently reinterpreted as a controller number. The reload is
//     therefore read back through the bytes the object emits, not through its
//     name alone: a name assertion alone would pass on an object that had been
//     rebuilt as the wrong class but relabelled;
//   - **two objects of different types stay two different objects** across the
//     round trip. Before the fix a patch holding a `.polypressure` and a
//     `.controlchange` dumped two `.controlchange` entries and reloaded as two
//     control changes, which is the failure a patch author would actually hit.
//
// The registry-wide version of the first claim — every registered name builds
// an object whose `Type()` is that name — lives in test_doc_coverage.cpp with
// the other failsafes that iterate the whole registry, so the next copy-paste
// is caught without anyone writing a file like this one.
//
// No audio device and no MIDI hardware required: the object opens nothing, it
// only emits bytes. It is nonetheless compiled behind `#if YSE_WINDOWS` with
// the six senders it belongs to — see mMidiPolyPressure.h, and issue #746 for
// the sweep that lifts that guard off the whole family.

#include <doctest/doctest.h>
#include <memory>
#include <string>

#include "headers/defines.hpp"

#if YSE_WINDOWS

#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObject.h"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "sinks.hpp"

using YSE::PATCHER::Register;

namespace {

  // The three wire bytes as a std::string, so an expectation reads as the
  // message it is. Built from ints because the status byte has its top bit set
  // and a string literal cannot carry that legibly.
  std::string Bytes(int status, int a, int b) {
    std::string s(3, '\0');
    s[0] = (char)status;
    s[1] = (char)a;
    s[2] = (char)b;
    return s;
  }

  // The first handle in `patch` whose name is `type`, or nullptr. Used instead
  // of a fixed index so a two-object case does not depend on the order
  // `ParseJSON` happens to rebuild in.
  YSE::pHandle* FindByName(YSE::patcher& patch, const char* type) {
    for (int i = 0; i < patch.Objects(); i++) {
      YSE::pHandle* handle = patch.GetHandleFromList(i);
      if (handle != nullptr && handle->GetName() == std::string(type)) return handle;
    }
    return nullptr;
  }

} // namespace

#endif // YSE_WINDOWS

TEST_SUITE("patcher") {

#if YSE_WINDOWS

  TEST_CASE("polypressure: the registry builds an object that answers to its own name (#749)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(YSE::OBJ::M_POLYPRESS));
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == std::string(YSE::OBJ::M_POLYPRESS));

    // The shape that tells it apart from the object it used to name itself
    // after: `.controlchange` has one inlet and stores its controller as a
    // parameter, this one has two inlets.
    CHECK(obj->NumInputs() == 2);
    CHECK(obj->NumOutputs() == 1);
  }

  TEST_CASE("polypressure: a live object in a real patch reports .polypressure (#749)") {
    YSE::patcher patch;
    patch.create(2);

    YSE::pHandle* poly = patch.CreateObject(YSE::OBJ::M_POLYPRESS, "");
    REQUIRE(poly != nullptr);
    CHECK(poly->GetName() == std::string(".polypressure"));
    CHECK(poly->GetInputs() == 2);

    patch.DeleteObject(poly);
  }

  TEST_CASE("polypressure: it is still a .polypressure after a DumpJSON round trip (#749)") {
    // The regression the issue is about. What `DumpJSON` writes is `Type()`,
    // so the stored patch named the wrong object and `ParseJSON` obligingly
    // built that one.
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::M_POLYPRESS, "3") != nullptr);

    const std::string json = src.DumpJSON();
    CHECK(json.find(".polypressure") != std::string::npos);
    CHECK(json.find(".controlchange") == std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* obj = loaded.GetHandleFromList(0);
    REQUIRE(obj != nullptr);
    CHECK(obj->GetName() == std::string(".polypressure"));
    CHECK(std::string(obj->GetParams()) == "3");
    // Two inlets: a `.controlchange` came back with one, and the pressure a
    // patch sent to inlet 1 went nowhere.
    REQUIRE(obj->GetInputs() == 2);

    // Read the reloaded object back through the bytes it emits rather than its
    // name: what has to survive a save and a load is a patch that still sends
    // key pressure on the same channel.
    TestHelpers::ListSink sink;
    YSE::pHandle sinkHandle(&sink);
    loaded.Connect(obj, 0, &sinkHandle, 0);

    obj->SetIntData(1, 90); // pressure — stored
    obj->SetIntData(0, 64); // pitch — fires
    REQUIRE(sink.gotList);
    CHECK(sink.received == Bytes(0xA3, 64, 90));

    loaded.DeleteObject(obj);
  }

  TEST_CASE("polypressure: a patch keeps it and .controlchange apart across a round trip (#749)") {
    // The user-visible shape of the bug: both objects stored under the same
    // name, so a patch that had one of each reloaded with two of the same.
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::M_POLYPRESS, "1") != nullptr);
    REQUIRE(src.CreateObject(YSE::OBJ::M_CONTROL, "2 7") != nullptr);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(src.DumpJSON());
    REQUIRE(loaded.Objects() == 2);

    YSE::pHandle* poly = FindByName(loaded, ".polypressure");
    YSE::pHandle* control = FindByName(loaded, ".controlchange");
    REQUIRE(poly != nullptr);
    REQUIRE(control != nullptr);

    TestHelpers::ListSink polySink;
    TestHelpers::ListSink controlSink;
    YSE::pHandle polySinkHandle(&polySink);
    YSE::pHandle controlSinkHandle(&controlSink);
    loaded.Connect(poly, 0, &polySinkHandle, 0);
    loaded.Connect(control, 0, &controlSinkHandle, 0);

    poly->SetIntData(1, 20);
    poly->SetIntData(0, 60);
    control->SetIntData(0, 100);

    REQUIRE(polySink.gotList);
    REQUIRE(controlSink.gotList);
    CHECK(polySink.received == Bytes(0xA1, 60, 20)); // status, pitch, pressure
    CHECK(controlSink.received == Bytes(0xB2, 7, 100)); // status, controller, value

    loaded.DeleteObject(poly);
    loaded.DeleteObject(control);
  }

#endif // YSE_WINDOWS

} // TEST_SUITE
