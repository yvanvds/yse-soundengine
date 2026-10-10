// Tests for the patcher bitwise operator family (issue #438):
// .& .| .<< .>>
//
// All four share gBitwiseBase: inlet 0 is the left operand and fires the
// evaluation, inlet 1 stores the right operand, both operands are ints (floats
// truncate towards zero), and the result leaves outlet 0 as an int.
//
// No audio device required.

#include <doctest/doctest.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/math/gBitwise.h"
#include "patcher/sinks.hpp"

using TestHelpers::IntSink;

namespace {

  // Drives one operator through a right-then-left pair and returns what the
  // sink saw. Left is applied last because inlet 0 is the hot inlet.
  template <typename OpType> int Evaluate(int left, int right) {
    OpType op;
    IntSink sink;
    op.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(op.GetOutlet(0), 0);

    op.GetInlet(1)->SetInt(right, YSE::T_GUI);
    op.GetInlet(0)->SetInt(left, YSE::T_GUI);
    return sink.received;
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("bitwise: all four operators are creatable through the registry (#438)") {
    const std::vector<const char*> types = {
        YSE::OBJ::G_BITAND,
        YSE::OBJ::G_BITOR,
        YSE::OBJ::G_SHIFTLEFT,
        YSE::OBJ::G_SHIFTRIGHT,
    };

    YSE::patcher p;
    p.create(2);
    for (const char* type : types) {
      CAPTURE(type);
      YSE::pHandle* h = p.CreateObject(type);
      REQUIRE(h != nullptr);
      CHECK(std::string(h->Type()) == std::string(type));
      CHECK(h->GetInputs() == 2);
      CHECK(h->GetOutputs() == 1);
      CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::INT);
    }
  }

  TEST_CASE("bitwise: all four operators are listed by pRegistry::AllNames (#438)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    for (const char* type : {".&", ".|", ".<<", ".>>"}) {
      CAPTURE(type);
      CHECK(std::find(names.begin(), names.end(), std::string(type)) != names.end());
    }
  }

  // ─── behaviour ──────────────────────────────────────────────────────────────

  TEST_CASE("bitwise: .& masks bits") {
    CHECK(Evaluate<YSE::PATCHER::gBitAnd>(0b1100, 0b1010) == 0b1000);
    CHECK(Evaluate<YSE::PATCHER::gBitAnd>(0xFF, 0x0F) == 0x0F);
    CHECK(Evaluate<YSE::PATCHER::gBitAnd>(5, 0) == 0);
    CHECK(Evaluate<YSE::PATCHER::gBitAnd>(-1, 0x55) == 0x55);
  }

  TEST_CASE("bitwise: .| sets bits") {
    CHECK(Evaluate<YSE::PATCHER::gBitOr>(0b1100, 0b1010) == 0b1110);
    CHECK(Evaluate<YSE::PATCHER::gBitOr>(0, 0) == 0);
    CHECK(Evaluate<YSE::PATCHER::gBitOr>(8, 1) == 9);
  }

  TEST_CASE("bitwise: .<< shifts left") {
    CHECK(Evaluate<YSE::PATCHER::gShiftLeft>(1, 0) == 1);
    CHECK(Evaluate<YSE::PATCHER::gShiftLeft>(1, 3) == 8);
    CHECK(Evaluate<YSE::PATCHER::gShiftLeft>(-1, 1) == -2);
  }

  TEST_CASE("bitwise: .>> shifts right arithmetically") {
    CHECK(Evaluate<YSE::PATCHER::gShiftRight>(8, 0) == 8);
    CHECK(Evaluate<YSE::PATCHER::gShiftRight>(8, 3) == 1);
    CHECK(Evaluate<YSE::PATCHER::gShiftRight>(7, 1) == 3);
    CHECK(Evaluate<YSE::PATCHER::gShiftRight>(-8, 1) == -4);
  }

  // Shift counts outside 0..31 would be undefined behaviour if handed straight
  // to the C++ operator, so gBitwise clamps them; this pins the clamp.
  TEST_CASE("bitwise: out-of-range shift counts are clamped, not UB (#438)") {
    CHECK(Evaluate<YSE::PATCHER::gShiftLeft>(5, -3) == 5); // negative: no shift
    CHECK(Evaluate<YSE::PATCHER::gShiftLeft>(5, 32) == 0); // everything shifted out
    CHECK(Evaluate<YSE::PATCHER::gShiftLeft>(5, 9999) == 0);
    CHECK(Evaluate<YSE::PATCHER::gShiftRight>(5, -3) == 5); // negative: no shift
    CHECK(Evaluate<YSE::PATCHER::gShiftRight>(5, 32) == 0); // positive saturates at 0
    CHECK(Evaluate<YSE::PATCHER::gShiftRight>(-5, 32) == -1); // negative saturates at -1
    CHECK(Evaluate<YSE::PATCHER::gShiftRight>(-5, 9999) == -1);
  }

  // Shifting a 1 into the sign bit is UB on a signed int; gBitwise routes the
  // shift through unsigned so the two's-complement result comes back.
  TEST_CASE("bitwise: .<< into the sign bit wraps rather than trapping (#438)") {
    CHECK(Evaluate<YSE::PATCHER::gShiftLeft>(1, 31) == (int)0x80000000u);
    CHECK(Evaluate<YSE::PATCHER::gShiftLeft>(3, 31) == (int)0x80000000u);
  }

  // ─── inlet semantics ────────────────────────────────────────────────────────

  TEST_CASE("bitwise: inlet 1 stores silently, inlet 0 fires") {
    YSE::PATCHER::gBitAnd op;
    IntSink sink;
    op.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(op.GetOutlet(0), 0);

    op.GetInlet(1)->SetInt(0x0F, YSE::T_GUI);
    CHECK_FALSE(sink.gotInt); // right inlet must not produce output

    op.GetInlet(0)->SetInt(0xF3, YSE::T_GUI);
    CHECK(sink.gotInt);
    CHECK(sink.received == 0x03);

    // Re-firing the left inlet re-evaluates against the stored right operand.
    op.GetInlet(0)->SetInt(0xFC, YSE::T_GUI);
    CHECK(sink.received == 0x0C);
  }

  TEST_CASE("bitwise: floats truncate towards zero on both inlets (#438)") {
    YSE::PATCHER::gBitOr op;
    IntSink sink;
    op.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(op.GetOutlet(0), 0);

    op.GetInlet(1)->SetFloat(2.9f, YSE::T_GUI); // -> 2
    op.GetInlet(0)->SetFloat(9.7f, YSE::T_GUI); // -> 9
    CHECK(sink.received == 11);

    op.GetInlet(0)->SetFloat(-3.8f, YSE::T_GUI); // -> -3
    CHECK(sink.received == (-3 | 2));
  }

  TEST_CASE("bitwise: the right operand defaults to 0 when never set") {
    YSE::PATCHER::gBitAnd op;
    IntSink sink;
    op.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(op.GetOutlet(0), 0);

    op.GetInlet(0)->SetInt(0xFF, YSE::T_GUI);
    CHECK(sink.received == 0);
  }

  // ─── params / persistence ───────────────────────────────────────────────────

  TEST_CASE("bitwise: the right operand can be set as a creation parameter (#438)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_SHIFTLEFT, "4");
    REQUIRE(h != nullptr);
    CHECK(h->GetParams() == "4");
  }

  TEST_CASE("bitwise: params survive a DumpJSON / ParseJSON round trip (#438)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_BITAND, "255") != nullptr);
    REQUIRE(src.CreateObject(YSE::OBJ::G_SHIFTRIGHT, "3") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".&") != std::string::npos);
    CHECK(json.find(".>>") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 2);

    std::vector<std::string> restored;
    for (int i = 0; i < 2; ++i) {
      YSE::pHandle* h = loaded.GetHandleFromList(i);
      REQUIRE(h != nullptr);
      restored.push_back(std::string(h->Type()) + " " + h->GetParams());
    }
    CHECK(std::find(restored.begin(), restored.end(), std::string(".& 255")) != restored.end());
    CHECK(std::find(restored.begin(), restored.end(), std::string(".>> 3")) != restored.end());
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category and the int outlet type specifically, which
  // is what a binding generator keys on.

  TEST_CASE("bitwise: every operator documents itself as MATH with an int outlet (#438)") {
    for (const char* type : {".&", ".|", ".<<", ".>>"}) {
      CAPTURE(type);
      std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(type));
      REQUIRE(obj != nullptr);
      CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MATH);
      CHECK(obj->GetOutputType(0) == YSE::OUT_TYPE::INT);
      CHECK_FALSE(obj->GetDescription().empty());
      REQUIRE(obj->GetParamDocs().size() == 1);
      CHECK(obj->GetParamDocs()[0].name == "right");
    }
  }

} // TEST_SUITE("patcher")
