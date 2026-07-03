// Tests for patcher GUI objects: gButton, gFloat, gInt, gList, gMessage,
// gSlider, gText, gToggle.  No audio device required.
//
// All inlet-0 inlets are active (CalculateIfReady runs after each set), so
// poking the inlet on a non-DSP object also triggers its Calculate(), which
// for the value-holding objects sends the stored value downstream.

#include <doctest/doctest.h>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/guiObjects/gButton.h"
#include "patcher/guiObjects/gFloat.h"
#include "patcher/guiObjects/gInt.h"
#include "patcher/guiObjects/gList.h"
#include "patcher/guiObjects/gMessage.h"
#include "patcher/guiObjects/gSlider.h"
#include "patcher/guiObjects/gText.h"
#include "patcher/guiObjects/gToggle.h"
#include "patcher/sinks.hpp"

using TestHelpers::BangSink;
using TestHelpers::FloatSink;
using TestHelpers::IntSink;
using TestHelpers::ListSink;
using TestHelpers::MessageSink;

TEST_SUITE("patcher") {

  // ─── gButton ──────────────────────────────────────────────────────────────────

  TEST_CASE("gButton: type name, input/output count, and output type") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_BUTTON);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".b");
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BANG);
  }

  TEST_CASE("gButton: initial GUI value is 'off'") {
    YSE::PATCHER::gButton btn;
    CHECK(btn.GetGuiValue() == "off");
  }

  TEST_CASE("gButton: bang/int/float each set on=true and emit one bang downstream") {
    YSE::PATCHER::gButton btn;
    BangSink sink;
    btn.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(btn.GetOutlet(0), 0);

    btn.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(sink.bangCount == 1);
    CHECK(btn.GetGuiValue() == "on");
    CHECK(btn.GetGuiValue() == "off"); // GUI_VALUE clears on read

    btn.GetInlet(0)->SetInt(42, YSE::T_GUI);
    CHECK(sink.bangCount == 2);
    CHECK(btn.GetGuiValue() == "on");

    btn.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    CHECK(sink.bangCount == 3);
    CHECK(btn.GetGuiValue() == "on");
  }

  // ─── gFloat ───────────────────────────────────────────────────────────────────

  TEST_CASE("gFloat: type name, input/output count, and output type") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_FLOAT);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".f");
    CHECK(h->GetInputs() == 2);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::FLOAT);
  }

  TEST_CASE("gFloat: initial GUI value is 0") {
    YSE::PATCHER::gFloat f;
    CHECK(f.GetGuiValue() == std::to_string(0.f));
  }

  TEST_CASE("gFloat: float on active inlet stores and emits the value") {
    YSE::PATCHER::gFloat f;
    FloatSink sink;
    f.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(f.GetOutlet(0), 0);

    f.GetInlet(0)->SetFloat(2.5f, YSE::T_GUI);
    CHECK(sink.gotFloat);
    CHECK(sink.received == doctest::Approx(2.5f));
    CHECK(f.GetGuiValue() == std::to_string(2.5f));
  }

  TEST_CASE("gFloat: int on active inlet is cast to float") {
    YSE::PATCHER::gFloat f;
    FloatSink sink;
    f.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(f.GetOutlet(0), 0);

    f.GetInlet(0)->SetInt(7, YSE::T_GUI);
    CHECK(sink.received == doctest::Approx(7.0f));
  }

  TEST_CASE("gFloat: inlet 1 stores a value but does not emit; bang on inlet 0 sends it") {
    YSE::PATCHER::gFloat f;
    FloatSink sink;
    f.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(f.GetOutlet(0), 0);

    f.GetInlet(1)->SetFloat(9.0f, YSE::T_GUI);
    CHECK_FALSE(sink.gotFloat);

    f.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(sink.gotFloat);
    CHECK(sink.received == doctest::Approx(9.0f));
  }

  // ─── gInt ─────────────────────────────────────────────────────────────────────

  TEST_CASE("gInt: type name, input/output count, and output type") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_INT);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".i");
    CHECK(h->GetInputs() == 2);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::INT);
  }

  TEST_CASE("gInt: initial GUI value is 0") {
    YSE::PATCHER::gInt i;
    CHECK(i.GetGuiValue() == std::to_string(0));
  }

  TEST_CASE("gInt: int on active inlet stores and emits the value") {
    YSE::PATCHER::gInt i;
    IntSink sink;
    i.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(i.GetOutlet(0), 0);

    i.GetInlet(0)->SetInt(42, YSE::T_GUI);
    CHECK(sink.gotInt);
    CHECK(sink.received == 42);
    CHECK(i.GetGuiValue() == std::to_string(42));
  }

  TEST_CASE("gInt: float on active inlet is truncated to int") {
    YSE::PATCHER::gInt i;
    IntSink sink;
    i.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(i.GetOutlet(0), 0);

    i.GetInlet(0)->SetFloat(3.9f, YSE::T_GUI);
    CHECK(sink.received == 3);
  }

  TEST_CASE("gInt: inlet 1 stores a value but does not emit; bang on inlet 0 sends it") {
    YSE::PATCHER::gInt i;
    IntSink sink;
    i.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(i.GetOutlet(0), 0);

    i.GetInlet(1)->SetInt(99, YSE::T_GUI);
    CHECK_FALSE(sink.gotInt);

    i.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(sink.received == 99);
  }

  // ─── gList ────────────────────────────────────────────────────────────────────

  TEST_CASE("gList: type name, input/output count, and output type") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_LIST);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".l");
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::LIST);
  }

  TEST_CASE("gList: SetParams initialises the stored message; bang emits it") {
    YSE::PATCHER::gList list;
    // Parameters::Set splits on spaces and only the first token reaches a
    // STRING param.  Use a single-token message for the SetParams path.
    list.SetParams("hello");
    ListSink sink;
    list.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(list.GetOutlet(0), 0);

    list.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(sink.gotList);
    CHECK(sink.received == "hello");
    CHECK(list.GetGuiValue() == "hello");
  }

  TEST_CASE("gList: list-in updates the stored message without auto-emitting") {
    YSE::PATCHER::gList list;
    ListSink sink;
    list.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(list.GetOutlet(0), 0);

    list.GetInlet(0)->SetList("alpha beta", YSE::T_GUI);
    CHECK_FALSE(sink.gotList);
    CHECK(list.GetGuiValue() == "alpha beta");

    list.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(sink.received == "alpha beta");
  }

  TEST_CASE("gList: inlet::SetMessage routes to SetMessage and updates the stored value") {
    YSE::PATCHER::gList list;
    list.GetInlet(0)->SetMessage("via message", YSE::T_GUI);
    CHECK(list.GetGuiValue() == "via message");
  }

  // ─── gMessage ─────────────────────────────────────────────────────────────────

  TEST_CASE("gMessage: type name, input/output count, and output type") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_MESSAGE);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".m");
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::LIST);
  }

  TEST_CASE("gMessage: bang dispatches the stored message via SendMessage") {
    YSE::PATCHER::gMessage msg;
    // Parameters::Set splits on spaces; only a single token reaches the
    // message STRING param.  Multi-token messages must arrive via SetList.
    msg.SetParams("payload");
    MessageSink sink;
    msg.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(msg.GetOutlet(0), 0);

    msg.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(sink.gotMessage);
    CHECK(sink.received == "payload");
  }

  TEST_CASE("gMessage: list-in updates the stored message") {
    YSE::PATCHER::gMessage msg;
    MessageSink sink;
    msg.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(msg.GetOutlet(0), 0);

    msg.GetInlet(0)->SetList("new payload", YSE::T_GUI);
    CHECK(msg.GetGuiValue() == "new payload");

    msg.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(sink.received == "new payload");
  }

  TEST_CASE("gMessage: inlet::SetMessage routes through SetMessage handler") {
    YSE::PATCHER::gMessage msg;
    msg.GetInlet(0)->SetMessage("from inlet", YSE::T_GUI);
    CHECK(msg.GetGuiValue() == "from inlet");
  }

  // ─── gSlider ──────────────────────────────────────────────────────────────────

  TEST_CASE("gSlider: type name, input/output count, and output type") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_SLIDER);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".slider");
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::FLOAT);
  }

  TEST_CASE("gSlider: float in range is passed through to outlet") {
    YSE::PATCHER::gSlider slider;
    FloatSink sink;
    slider.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(slider.GetOutlet(0), 0);

    slider.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    CHECK(sink.received == doctest::Approx(0.5f));
    CHECK(slider.GetGuiValue() == std::to_string(0.5f));
  }

  TEST_CASE("gSlider: value below 0 is clamped to 0") {
    YSE::PATCHER::gSlider slider;
    FloatSink sink;
    slider.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(slider.GetOutlet(0), 0);

    slider.GetInlet(0)->SetFloat(-0.5f, YSE::T_GUI);
    CHECK(sink.received == doctest::Approx(0.0f));
  }

  TEST_CASE("gSlider: value above 1 is clamped to 1") {
    YSE::PATCHER::gSlider slider;
    FloatSink sink;
    slider.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(slider.GetOutlet(0), 0);

    slider.GetInlet(0)->SetFloat(1.5f, YSE::T_GUI);
    CHECK(sink.received == doctest::Approx(1.0f));
  }

  TEST_CASE("gSlider: int input is cast to float and clamped") {
    YSE::PATCHER::gSlider slider;
    FloatSink sink;
    slider.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(slider.GetOutlet(0), 0);

    slider.GetInlet(0)->SetInt(5, YSE::T_GUI);
    CHECK(sink.received == doctest::Approx(1.0f));

    slider.GetInlet(0)->SetInt(-3, YSE::T_GUI);
    CHECK(sink.received == doctest::Approx(0.0f));
  }

  TEST_CASE("gSlider: bang is a no-op on value but still triggers Calculate") {
    YSE::PATCHER::gSlider slider;
    FloatSink sink;
    slider.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(slider.GetOutlet(0), 0);

    slider.GetInlet(0)->SetFloat(0.4f, YSE::T_GUI);
    sink.gotFloat = false;
    slider.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(sink.gotFloat);
    CHECK(sink.received == doctest::Approx(0.4f));
  }

  // ─── gText ────────────────────────────────────────────────────────────────────

  TEST_CASE("gText: type name, zero inputs and outputs") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_TEXT);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".text");
    CHECK(h->GetInputs() == 0);
    CHECK(h->GetOutputs() == 0);
  }

  TEST_CASE("gText: SetParams stores its single string parameter") {
    YSE::PATCHER::gText t;
    t.SetParams("descriptive label");
    CHECK(t.GetParams() == "descriptive label");
  }

  // ─── gToggle ──────────────────────────────────────────────────────────────────

  TEST_CASE("gToggle: type name, input/output count, and output type") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_TOGGLE);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".t");
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::INT);
  }

  TEST_CASE("gToggle: initial GUI value is 'off'") {
    YSE::PATCHER::gToggle t;
    CHECK(t.GetGuiValue() == "off");
  }

  TEST_CASE("gToggle: SetValue(non-zero) sets on, SetValue(0) sets off, and each emits the int") {
    YSE::PATCHER::gToggle t;
    IntSink sink;
    t.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(t.GetOutlet(0), 0);

    t.GetInlet(0)->SetInt(1, YSE::T_GUI);
    CHECK(sink.received == 1);
    CHECK(t.GetGuiValue() == "on");

    t.GetInlet(0)->SetInt(0, YSE::T_GUI);
    CHECK(sink.received == 0);
    CHECK(t.GetGuiValue() == "off");

    t.GetInlet(0)->SetInt(7, YSE::T_GUI);
    CHECK(sink.received == 1);
    CHECK(t.GetGuiValue() == "on");
  }

  TEST_CASE("gToggle: bang flips the stored value and emits") {
    YSE::PATCHER::gToggle t;
    IntSink sink;
    t.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(t.GetOutlet(0), 0);

    t.GetInlet(0)->SetBang(YSE::T_GUI); // off -> on
    CHECK(sink.received == 1);
    t.GetInlet(0)->SetBang(YSE::T_GUI); // on -> off
    CHECK(sink.received == 0);
  }

} // TEST_SUITE("patcher")
