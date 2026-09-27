// Registry-wide failsafes: properties every patcher object registered in
// pRegistry must have, checked by iterating the registry rather than by
// naming objects one at a time, so a new object cannot be added without
// them.
//
// Issue #102 — documentation metadata: a description, a category,
// label/range strings on every construction-time inlet/outlet, and a
// ParamDoc for every ADD_PARAM. If anyone adds a new object to pRegistry
// without populating ADD_DESCRIPTION / ADD_CATEGORY / INLET_DOC /
// OUTLET_DOC / PARAM_DOC, that test fails by naming the offending type.
//
// Issue #749 — self-identification: the object the registry builds for a
// name must answer that same name from `Type()`. The two come from
// different places (the registry key and the second PATCHER_CLASS
// argument), so a copy-paste can silently disagree — `.polypressure`
// answered `.controlchange` for exactly that reason, which made it save
// and reload as another object.
//
// Both run purely in memory: they just construct each object via the
// registry, never wiring it into a running patcher graph, so no audio
// device or graph state is required.

#include <doctest/doctest.h>
#include <memory>
#include <set>
#include <string>
#include "patcher/pObject.h"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/pEnums.h"
#include "patcher/parameters.h"

using YSE::PATCHER::pCategory;
using YSE::PATCHER::pObject;
using YSE::PATCHER::Register;

TEST_SUITE("patcher") {

  TEST_CASE("doc coverage: every registered object documents itself") {
    auto names = Register().AllNames();

    // Sanity check — guards against a registry that silently went empty
    // (e.g. on a platform where the conditionally compiled MIDI block
    // is excluded). Any non-zero count is acceptable; the per-object
    // assertions below carry the real work.
    REQUIRE(names.size() > 0);

    for (const auto& name : names) {
      CAPTURE(name);
      std::unique_ptr<pObject> obj(Register().Get(name));
      REQUIRE(obj != nullptr);

      REQUIRE_FALSE(obj->GetDescription().empty());
      REQUIRE(obj->GetCategory() != pCategory::UNSET);

      for (int i = 0; i < obj->NumInputs(); ++i) {
        CAPTURE(i);
        auto* port = obj->GetInlet(i);
        REQUIRE(port != nullptr);
        REQUIRE_FALSE(port->GetDocLabel().empty());
        REQUIRE_FALSE(port->GetDocDescription().empty());
      }

      for (int i = 0; i < obj->NumOutputs(); ++i) {
        CAPTURE(i);
        auto* port = obj->GetOutlet(i);
        REQUIRE(port != nullptr);
        REQUIRE_FALSE(port->GetDocLabel().empty());
        REQUIRE_FALSE(port->GetDocDescription().empty());
      }

      const auto& paramDocs = obj->GetParamDocs();
      for (const auto& p : paramDocs) {
        CAPTURE(p.name);
        REQUIRE_FALSE(p.name.empty());
        REQUIRE_FALSE(p.doc.empty());
      }
    }
  }

  TEST_CASE("doc coverage: no registered object falls back to GENERIC (#870)") {
    // The category is the page an object lands on in the reference and the
    // palette group an editor shows it in. GENERIC used to hold 133 objects —
    // every array, dict, list, string, routing and I/O object — which made
    // both useless. It is now the fallback for an object that genuinely fits
    // no other category; if a new object really is one, name it here rather
    // than dropping it into GENERIC by default.
    const std::set<std::string> genuinelyGeneric = {};

    for (const auto& name : Register().AllNames()) {
      CAPTURE(name);
      std::unique_ptr<pObject> obj(Register().Get(name));
      REQUIRE(obj != nullptr);
      if (genuinelyGeneric.count(name) != 0) continue;
      CHECK(obj->GetCategory() != pCategory::GENERIC);
    }
  }

  TEST_CASE("registry: device-port MIDI objects are flagged, the rest are not (#870)") {
    // RequiresMidiDevice() is what the metadata JSON's "requires_midi_device"
    // and the docs' platform note are built from. It must name exactly the
    // objects registered inside the YSE_ENABLE_MIDI_DEVICE guard.
    auto& reg = Register();
    CHECK_FALSE(reg.RequiresMidiDevice(YSE::OBJ::M_NOTEON)); // a formatter
    CHECK_FALSE(reg.RequiresMidiDevice(YSE::OBJ::M_PARSE));
    CHECK_FALSE(reg.RequiresMidiDevice(YSE::OBJ::M_SXFORMAT));
    CHECK_FALSE(reg.RequiresMidiDevice("no_such_object_for_test"));
#if YSE_ENABLE_MIDI_DEVICE
    for (const char* type :
         {YSE::OBJ::M_OUT, YSE::OBJ::M_IN, YSE::OBJ::M_NOTEIN, YSE::OBJ::M_SYSEXIN,
          YSE::OBJ::M_XMIDIIN, YSE::OBJ::M_RPNIN, YSE::OBJ::M_MIDIINFO}) {
      CAPTURE(type);
      CHECK(reg.IsValidObject(type));
      CHECK(reg.RequiresMidiDevice(type));
    }
#else
    // Without the backend the flagged objects are not registered at all, so
    // nothing the live registry holds can carry the flag.
    for (const auto& name : reg.AllNames()) {
      CAPTURE(name);
      CHECK_FALSE(reg.RequiresMidiDevice(name));
    }
#endif
  }

  TEST_CASE("registry: every registered object reports its own name (#749)") {
    // `Type()` is what DumpJSON writes and what ParseJSON looks up, so a name
    // that disagrees with the registry key does not merely mislabel the object
    // — it turns it into a different one across a save and a load.
    auto names = Register().AllNames();
    REQUIRE(names.size() > 0);

    for (const auto& name : names) {
      CAPTURE(name);
      std::unique_ptr<pObject> obj(Register().Get(name));
      REQUIRE(obj != nullptr);
      CHECK(std::string(obj->Type()) == name);
    }
  }

  TEST_CASE("registry: the MIDI senders are registered on every platform (#746)") {
    // These eleven format MIDI bytes onto a list outlet and open no device, so
    // there is no platform they cannot run on. They nonetheless sat behind a
    // bare `#if YSE_WINDOWS` until #746, which meant `.noteon` existed on
    // Windows and nowhere else and a patch quietly lost objects when it moved
    // between machines.
    //
    // No `#if` here on purpose: that is the whole assertion. On Windows this
    // passes either way, so it is the Linux and Android runs that hold the
    // line — if the guard comes back, this is what fails there.
    const char* const senders[] = {
        YSE::OBJ::M_CHANPRESS, YSE::OBJ::M_CONTROL,    YSE::OBJ::M_NOTEOFF,  YSE::OBJ::M_NOTEON,
        YSE::OBJ::M_POLYPRESS, YSE::OBJ::M_PROGCHANGE, YSE::OBJ::M_BENDOUT,  YSE::OBJ::M_XBENDOUT,
        YSE::OBJ::M_XBENDOUT2, YSE::OBJ::M_XCTLOUT,    YSE::OBJ::M_XNOTEOUT,
    };

    auto names = Register().AllNames();
    for (const char* type : senders) {
      CAPTURE(type);
      bool registered = false;
      for (const auto& name : names) {
        if (name == std::string(type)) {
          registered = true;
          break;
        }
      }
      CHECK(registered);

      std::unique_ptr<pObject> obj(Register().Get(type));
      REQUIRE(obj != nullptr);
      CHECK(std::string(obj->Type()) == std::string(type));
    }
  }

} // TEST_SUITE
