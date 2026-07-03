// Failsafe test for issue #102: every patcher object registered in
// pRegistry must have documentation metadata — description, category,
// label/range strings on every construction-time inlet/outlet, and a
// ParamDoc for every ADD_PARAM. If anyone adds a new object to pRegistry
// without populating ADD_DESCRIPTION / ADD_CATEGORY / INLET_DOC /
// OUTLET_DOC / PARAM_DOC, this test fails by naming the offending type.
//
// The test runs purely in memory: it just constructs each object via the
// registry, never wires it into a running patcher graph, so no audio
// device or graph state is required.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include "patcher/pObject.h"
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

} // TEST_SUITE
