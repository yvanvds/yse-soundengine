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
    // ANY since #933: the text leaves as the bang, int, float or list it spells.
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::ANY);
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

  // ─── a message box's text, typed at the receiving inlet (#933) ───────────────

  // A hot inlet with every typed handler and no command channel, counting its
  // calculates so a fire on an ignored message is visible.
  struct TypedProbe : TestHelpers::SinkBase {
    struct Seen {
      bool gotBang = false;
      bool gotInt = false;
      bool gotFloat = false;
      bool gotList = false;
      int intValue = 0;
      float floatValue = 0.f;
      std::string listValue;
      void reset() {
        *this = Seen();
      }
    } seen;
    int calculated = 0;
    // Only the float handler, as `.mtof`-like objects that take no words.
    explicit TypedProbe(bool floatOnly = false) : SinkBase(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterFloat([this](float v, int, YSE::THREAD) {
        seen.gotFloat = true;
        seen.floatValue = v;
      });
      if (floatOnly) return;
      inputs.back().RegisterBang([this](int, YSE::THREAD) { seen.gotBang = true; });
      inputs.back().RegisterInt([this](int v, int, YSE::THREAD) {
        seen.gotInt = true;
        seen.intValue = v;
      });
      inputs.back().RegisterList([this](const std::string& v, int, YSE::THREAD) {
        seen.gotList = true;
        seen.listValue = v;
      });
    }
    const char* Type() const override {
      return "typed_probe";
    }
    void Calculate(YSE::THREAD) override {
      calculated++;
    }
    void SetMessage(const std::string&, float) override {}
  };

  // A command-channel object with a list handler and nothing else — the shape
  // of `.midiout`, whose list handler must never see a word command.
  struct CommandProbe : TestHelpers::SinkBase {
    std::string command;
    std::string list;
    CommandProbe() : SinkBase(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterList([this](const std::string& v, int, YSE::THREAD) { list = v; });
    }
    const char* Type() const override {
      return "command_probe";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string& message, float) override {
      command = message;
    }
    bool HandlesMessages() const override {
      return true;
    }
  };

  TEST_CASE("message text: an inlet reads it as the typed message it spells (#933)") {
    TypedProbe probe;
    auto send = [&probe](const std::string& text) {
      probe.seen.reset();
      probe.calculated = 0;
      probe.GetInlet(0)->SetMessage(text, YSE::T_GUI);
    };

    send("60");
    CHECK(probe.seen.gotInt);
    CHECK(probe.seen.intValue == 60);
    CHECK_FALSE(probe.seen.gotFloat);
    CHECK(probe.calculated == 1);

    send("0.5");
    CHECK(probe.seen.gotFloat);
    CHECK(probe.seen.floatValue == doctest::Approx(0.5f));
    CHECK_FALSE(probe.seen.gotInt);

    send("bang");
    CHECK(probe.seen.gotBang);
    CHECK_FALSE(probe.seen.gotList);

    send("1 2 3");
    CHECK(probe.seen.gotList);
    CHECK(probe.seen.listValue == "1 2 3");

    // A word with no command channel to take it: the list, as Max's anything
    // reaches a list-reading object.
    send("note 60 100");
    CHECK(probe.seen.gotList);
    CHECK(probe.seen.listValue == "note 60 100");
    CHECK(probe.calculated == 1);

    // Nothing to deliver: nothing handled, and no calculate on the stale value.
    send("");
    CHECK_FALSE(probe.seen.gotBang);
    CHECK_FALSE(probe.seen.gotList);
    CHECK(probe.calculated == 0);
  }

  TEST_CASE("message text: an ignored message does not fire a hot inlet (#933)") {
    // The float-only hot inlet of the issue's `.mtof`: a word reaches no
    // handler, so the object must not calculate with what it already held.
    TypedProbe probe(true);
    probe.GetInlet(0)->SetMessage("hello", YSE::T_GUI);
    probe.GetInlet(0)->SetMessage("60", YSE::T_GUI); // an int, and no int handler
    probe.GetInlet(0)->SetMessage("bang", YSE::T_GUI);
    CHECK_FALSE(probe.seen.gotFloat);
    CHECK(probe.calculated == 0);

    probe.GetInlet(0)->SetMessage("60.", YSE::T_GUI);
    CHECK(probe.seen.floatValue == doctest::Approx(60.f));
    CHECK(probe.calculated == 1);
  }

  TEST_CASE("message text: a word command takes the command channel first (#933)") {
    CommandProbe probe;
    probe.GetInlet(0)->SetMessage("allnotesoff", YSE::T_GUI);
    CHECK(probe.command == "allnotesoff");
    CHECK(probe.list.empty());

    // A number-led list is a list, even to an object with a command channel.
    probe.command.clear();
    probe.GetInlet(0)->SetMessage("144 60 100", YSE::T_GUI);
    CHECK(probe.list == "144 60 100");
    CHECK(probe.command.empty());

    // A typed message the inlet has no handler for still reaches the command
    // channel, so an object that relied on it keeps what it took before.
    probe.GetInlet(0)->SetMessage("60", YSE::T_GUI);
    CHECK(probe.command == "60");
  }

  TEST_CASE("message box: its text reaches ordinary objects in a real patch (#933)") {
    YSE::patcher p;
    p.create(2);

    SUBCASE(".m 60 into .mtof sends middle C's frequency") {
      YSE::pHandle* msg = p.CreateObject(YSE::OBJ::G_MESSAGE, "60");
      YSE::pHandle* mtof = p.CreateObject(YSE::OBJ::MIDITOFREQUENCY);
      YSE::pHandle* out = p.CreateObject(YSE::OBJ::G_FLOAT);
      REQUIRE(msg != nullptr);
      REQUIRE(mtof != nullptr);
      REQUIRE(out != nullptr);
      p.Connect(msg, 0, mtof, 0);
      p.Connect(mtof, 0, out, 0);

      msg->SetBang(0);
      CHECK(std::stof(out->GetGuiValue()) == doctest::Approx(261.6256f).epsilon(0.001));
    }

    SUBCASE(".m 60 into .i stores and sends 60") {
      YSE::pHandle* msg = p.CreateObject(YSE::OBJ::G_MESSAGE, "60");
      YSE::pHandle* box = p.CreateObject(YSE::OBJ::G_INT);
      YSE::pHandle* out = p.CreateObject(YSE::OBJ::G_INT);
      p.Connect(msg, 0, box, 0);
      p.Connect(box, 0, out, 0);

      msg->SetBang(0);
      CHECK(box->GetGuiValue() == "60");
      CHECK(out->GetGuiValue() == "60");
    }

    SUBCASE(".m 2.5 into .+ 1 adds") {
      YSE::pHandle* msg = p.CreateObject(YSE::OBJ::G_MESSAGE, "2.5");
      YSE::pHandle* add = p.CreateObject(YSE::OBJ::G_ADD, "1");
      YSE::pHandle* out = p.CreateObject(YSE::OBJ::G_FLOAT);
      p.Connect(msg, 0, add, 0);
      p.Connect(add, 0, out, 0);

      msg->SetBang(0);
      CHECK(std::stof(out->GetGuiValue()) == doctest::Approx(3.5f));
    }

    SUBCASE(".m note 60 into .route note sends 60") {
      YSE::pHandle* msg = p.CreateObject(YSE::OBJ::G_MESSAGE);
      YSE::pHandle* route = p.CreateObject(YSE::OBJ::G_ROUTE, "note");
      YSE::pHandle* out = p.CreateObject(YSE::OBJ::G_INT);
      p.Connect(msg, 0, route, 0);
      p.Connect(route, 0, out, 0);

      msg->SetListData(0, "note 60");
      msg->SetBang(0);
      CHECK(out->GetGuiValue() == "60");
    }

    SUBCASE(".m bang into .i re-sends what .i holds") {
      YSE::pHandle* msg = p.CreateObject(YSE::OBJ::G_MESSAGE, "bang");
      YSE::pHandle* box = p.CreateObject(YSE::OBJ::G_INT, "7");
      YSE::pHandle* out = p.CreateObject(YSE::OBJ::G_INT);
      p.Connect(msg, 0, box, 0);
      p.Connect(box, 0, out, 0);

      msg->SetBang(0);
      CHECK(out->GetGuiValue() == "7");
    }

    SUBCASE("a word into .mtof does not fire it with the stale note") {
      YSE::pHandle* msg = p.CreateObject(YSE::OBJ::G_MESSAGE, "hello");
      YSE::pHandle* mtof = p.CreateObject(YSE::OBJ::MIDITOFREQUENCY);
      YSE::pHandle* out = p.CreateObject(YSE::OBJ::G_FLOAT, "-1");
      p.Connect(msg, 0, mtof, 0);
      p.Connect(mtof, 0, out, 0);

      msg->SetBang(0);
      CHECK(std::stof(out->GetGuiValue()) == doctest::Approx(-1.f));
    }

    SUBCASE(".m 60 into .l still stores the text") {
      YSE::pHandle* msg = p.CreateObject(YSE::OBJ::G_MESSAGE, "60");
      YSE::pHandle* list = p.CreateObject(YSE::OBJ::G_LIST);
      p.Connect(msg, 0, list, 0);

      msg->SetBang(0);
      CHECK(list->GetGuiValue() == "60");
    }
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
