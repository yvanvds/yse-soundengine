// Tests for the labelled switch family — `.led` and `.textbutton` (issue #557),
// the patcher's first control that can say what it is.
//
// Five claims, and the cases are organised around them:
//
//   - **one implementation, two names.** The value model is a named on/off in
//     both, and only the widget a host draws differs. That is asserted rather
//     than assumed: the shared cases run over both type names and require
//     identical answers, so a divergence cannot hide in one of them.
//
//   - **the label is a creation argument and nothing else changes it.** Which
//     is what lets every path read it with no synchronisation: registering the
//     list parameter and the parse callbacks makes `ParamsNeedRebuild()` true,
//     so a live SetParams replaces the object through the #234 graph swap
//     instead of rewriting a string under the audio thread. Max's `settext` /
//     `text` are refused for the same reason, and that refusal is pinned here.
//
//   - **`momentary` is the value mode, offered on both names.** Latching is
//     `.t`'s model, momentary is `.b`'s, and the momentary poll reports *and
//     clears* in one step — the lost-press fix of issue #197.
//
//   - **the object is settable, where `.b` and `.t` are not.** The GUI cell is
//     "0"/"1" rather than "on"/"off", so the string a host polls is the string
//     inlet 0 takes back. Both halves of issue #551's write contract hold, in
//     both modes.
//
//   - **nothing on a message path allocates.**
//
// Plus the JSON round trip, the doc metadata and a host driving a real patch
// through pHandle.
//
// No audio device required.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include <vector>

#include "patcher/guiObjects/gLabelSwitch.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"

using TestHelpers::MultiSink;
using TestHelpers::OrderSink;
using YSE::PATCHER::gLabelSwitchBase;
using YSE::PATCHER::gLed;
using YSE::PATCHER::gTextButton;
using YSE::PATCHER::Register;

namespace {

  // The two names, so the shared cases can be run over both and a divergence
  // between the renderings cannot hide in one.
  const char* const kNames[] = {YSE::OBJ::G_LED, YSE::OBJ::G_TEXTBUTTON};

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("labelswitch: both renderings are the same object (#557)") {
    YSE::patcher p;
    p.create(2);

    for (const char* name : kNames) {
      CAPTURE(name);
      YSE::pHandle* h = p.CreateObject(name, "mute");
      REQUIRE(h != nullptr);
      CHECK(std::string(h->Type()) == std::string(name));

      // One hot inlet, two outlets: the state and the label.
      CHECK(h->GetInputs() == 1);
      CHECK(h->GetOutputs() == 2);
      CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::INT);
      CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::LIST);

      // Same value model, driven the same way, answering the same thing — and
      // the answer is a *number*, which is what buys the settable promise.
      h->SetIntData(0, 1);
      CHECK(h->GetGuiValueCount() == 1u);
      CHECK(h->GuiValueIsSettable());
      CHECK(h->GetGuiValue() == "1");

      CHECK(YSE::patcher::IsValidObject(name));
    }

    auto names = Register().AllNames();
    for (const char* name : kNames) {
      CAPTURE(name);
      bool found = false;
      for (const auto& n : names) {
        if (n == name) found = true;
      }
      CHECK(found);
    }
  }

  TEST_CASE("labelswitch: a bare object is unlabelled, latching and off (#557)") {
    gLed led;
    gTextButton button;
    gLabelSwitchBase* controls[] = {&led, &button};

    for (gLabelSwitchBase* control : controls) {
      CAPTURE(control->Type());
      CHECK(control->Label().empty());
      CHECK_FALSE(control->Momentary());
      CHECK_FALSE(control->IsOn());
      // The label is a LIST parameter with parse callbacks behind it, so a live
      // re-parse takes the #234 structural route rather than rewriting the
      // string underneath a reader.
      CHECK(control->ParamsNeedRebuild());
    }
  }

  // ─── the label ──────────────────────────────────────────────────────────────

  TEST_CASE("labelswitch: every argument after the keyword is the label (#557)") {
    // A label is prose, not a token: `.textbutton filter cutoff` is one control
    // called "filter cutoff" rather than two arguments.
    gTextButton button;
    button.SetParams("filter cutoff");
    CHECK(button.Label() == "filter cutoff");
    CHECK_FALSE(button.Momentary());

    gLed led;
    led.SetParams("clip");
    CHECK(led.Label() == "clip");
  }

  TEST_CASE("labelswitch: \"momentary\" is a leading keyword and not a label (#557)") {
    gTextButton button;
    button.SetParams("momentary play");
    CHECK(button.Momentary());
    CHECK(button.Label() == "play");

    // Offered on both names, not one each: one press against one latch is a
    // property of the value, not of the drawing.
    gLed led;
    led.SetParams("momentary clip");
    CHECK(led.Momentary());
    CHECK(led.Label() == "clip");

    // The keyword is only the keyword in first position: a control genuinely
    // labelled "momentary" is one word away from working.
    gTextButton second;
    second.SetParams("play momentary");
    CHECK_FALSE(second.Momentary());
    CHECK(second.Label() == "play momentary");

    // And the keyword alone is a momentary control with no label.
    gTextButton bare;
    bare.SetParams("momentary");
    CHECK(bare.Momentary());
    CHECK(bare.Label().empty());
  }

  TEST_CASE("labelswitch: SetParams(\"\") returns the object to the no-argument shape (#557)") {
    gTextButton button;
    button.SetParams("momentary play");
    REQUIRE(button.Momentary());
    REQUIRE(button.Label() == "play");

    button.SetParams("");
    CHECK_FALSE(button.Momentary());
    CHECK(button.Label().empty());
    CHECK_FALSE(button.IsOn());
  }

  // ─── latching (the default) ─────────────────────────────────────────────────

  TEST_CASE("labelswitch: an int is the state, and it emits state and label (#557)") {
    MultiSink stateSink, labelSink;
    gLed led;
    led.SetParams("clip");
    TestHelpers::Wire(led, 0, stateSink);
    TestHelpers::Wire(led, 1, labelSink);

    led.GetInlet(0)->SetInt(1, YSE::T_GUI);
    CHECK(stateSink.intValue == 1);
    CHECK(labelSink.listValue == "clip");
    CHECK(led.IsOn());

    led.GetInlet(0)->SetInt(0, YSE::T_GUI);
    CHECK(stateSink.intValue == 0);
    CHECK_FALSE(led.IsOn());

    // Anything but 0 is on, which is how the whole patcher reads a switch.
    led.GetInlet(0)->SetFloat(-2.5f, YSE::T_GUI);
    CHECK(stateSink.intValue == 1);
    CHECK(led.IsOn());
  }

  TEST_CASE("labelswitch: a bang flips a latching control (#557)") {
    MultiSink stateSink;
    gTextButton button;
    button.SetParams("mute");
    TestHelpers::Wire(button, 0, stateSink);

    button.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(stateSink.intValue == 1);
    CHECK(button.IsOn());

    button.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(stateSink.intValue == 0);
    CHECK_FALSE(button.IsOn());
  }

  TEST_CASE("labelswitch: a latching poll is a look, not a consume (#557)") {
    // The half of issue #197 that does *not* apply here: a latch holds state, so
    // reading it twice has to answer twice.
    gLed led;
    led.SetParams("clip");
    led.GetInlet(0)->SetInt(1, YSE::T_GUI);
    CHECK(led.GetGuiValue() == "1");
    CHECK(led.GetGuiValue() == "1");
    CHECK(led.IsOn());
  }

  TEST_CASE("labelswitch: the two outlets fire right to left (#557)") {
    // `.trigger`'s ordering guarantee, and it is load-bearing: the label has to
    // be in hand by the time the state lands on a hot inlet downstream.
    std::vector<char> log;
    OrderSink stateSink, labelSink;
    stateSink.log = &log;
    stateSink.tag = 's';
    labelSink.log = &log;
    labelSink.tag = 'l';

    {
      gTextButton button;
      button.SetParams("mute");
      TestHelpers::Wire(button, 0, stateSink);
      TestHelpers::Wire(button, 1, labelSink);

      button.GetInlet(0)->SetInt(1, YSE::T_GUI);
    }

    REQUIRE(log.size() == 2);
    CHECK(log[0] == 'l');
    CHECK(log[1] == 's');
  }

  // ─── momentary ──────────────────────────────────────────────────────────────

  TEST_CASE("labelswitch: momentary treats any input as a press (#557)") {
    // `.b`'s reading of an int, and the only one available to a control that
    // holds an event rather than a state: "the user clicked" cannot be spelled 0.
    MultiSink stateSink, labelSink;
    gTextButton button;
    button.SetParams("momentary play");
    TestHelpers::Wire(button, 0, stateSink);
    TestHelpers::Wire(button, 1, labelSink);

    button.GetInlet(0)->SetInt(0, YSE::T_GUI);
    CHECK(stateSink.intValue == 1);
    CHECK(labelSink.listValue == "play");
    CHECK(button.IsOn());

    // A press is idempotent — several can arrive between two frames.
    button.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(button.IsOn());
    button.GetInlet(0)->SetFloat(0.f, YSE::T_GUI);
    CHECK(button.IsOn());
  }

  TEST_CASE("labelswitch: the momentary poll reports and clears in one step (#557)") {
    // Issue #197's lost press: written as a load and then a store, a press that
    // landed between the two would be silently dropped. exchange() is the fix,
    // and a destructive poll is what the #551 protocol explicitly permits.
    gLed led;
    led.SetParams("momentary clip");

    CHECK(led.GetGuiValue() == "0");
    led.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(led.IsOn());
    CHECK(led.GetGuiValue() == "1");
    // Consumed: the same press is not reported twice.
    CHECK(led.GetGuiValue() == "0");
    CHECK_FALSE(led.IsOn());
  }

  TEST_CASE("labelswitch: a whole-state write can clear a momentary control (#557)") {
    // The deliberate asymmetry: an int is *the user pressing*, a list is *a
    // host restoring the flag*, and only the second can be asked to write a 0.
    // #556 draws the same line between "the user clicked item N" and "here is
    // the whole mask".
    MultiSink stateSink;
    gTextButton button;
    button.SetParams("momentary play");
    TestHelpers::Wire(button, 0, stateSink);

    button.GetInlet(0)->SetInt(0, YSE::T_GUI);
    REQUIRE(button.IsOn());

    const std::string clear = "0";
    button.GetInlet(0)->SetList(clear, YSE::T_GUI);
    CHECK_FALSE(button.IsOn());
    CHECK(stateSink.intValue == 0);

    const std::string arm = "1";
    button.GetInlet(0)->SetList(arm, YSE::T_GUI);
    CHECK(button.IsOn());
    CHECK(stateSink.intValue == 1);
  }

  // ─── the GUI value protocol (issue #551) ────────────────────────────────────

  TEST_CASE("labelswitch: it is the scalar one-cell case (#557)") {
    gLed led;
    led.SetParams("clip");
    CHECK(led.GuiValueIsSettable());
    CHECK(led.GetGuiValueCount() == 1u);

    led.GetInlet(0)->SetInt(1, YSE::T_GUI);
    // Cell 0 of a scalar control is exactly the whole state, which the base
    // supplies rather than the object restating it.
    CHECK(led.GetGuiValueAt(0) == "1");
    // Past the end is "", never the whole state again.
    CHECK(led.GetGuiValueAt(1).empty());
    CHECK(led.GetGuiValueAt(0xFFFFFFFFu).empty());
  }

  TEST_CASE("labelswitch: \"set 0 <value>\" is the cell write (#557)") {
    gTextButton button;
    button.SetParams("mute");

    const std::string cellOn = "set 0 1";
    const std::string cellOff = "set 0 0";
    const std::string noSuchCell = "set 1 1";
    const std::string maxLegacy = "set 1"; // Max's silent set: no index, dropped

    button.GetInlet(0)->SetList(cellOn, YSE::T_GUI);
    CHECK(button.IsOn());
    // Absolute, not a toggle: repeating it does not turn it off.
    button.GetInlet(0)->SetList(cellOn, YSE::T_GUI);
    CHECK(button.IsOn());
    button.GetInlet(0)->SetList(cellOff, YSE::T_GUI);
    CHECK_FALSE(button.IsOn());

    // There is one cell, so any other index addresses nothing.
    button.GetInlet(0)->SetList(cellOn, YSE::T_GUI);
    button.GetInlet(0)->SetList(noSuchCell, YSE::T_GUI);
    CHECK(button.IsOn());

    // And Max's legacy `set <value>` carries no index, so it is a cell write
    // with no value: dropped, rather than silently misread as `set 1 <nothing>`
    // or as the whole-state write "1".
    button.GetInlet(0)->SetList(cellOff, YSE::T_GUI);
    REQUIRE_FALSE(button.IsOn());
    button.GetInlet(0)->SetList(maxLegacy, YSE::T_GUI);
    CHECK_FALSE(button.IsOn());
  }

  TEST_CASE("labelswitch: its own GetGuiValue round-trips through inlet 0 (#557)") {
    // The unconditional promise GuiValueIsSettable() makes — and the reason the
    // cell is "0"/"1" rather than `.t`'s "on"/"off", which could not come back
    // in through an inlet that takes a number.
    gTextButton latching;
    latching.SetParams("mute");
    latching.GetInlet(0)->SetInt(1, YSE::T_GUI);
    const std::string stored = latching.GetGuiValue();
    REQUIRE(stored == "1");
    latching.GetInlet(0)->SetInt(0, YSE::T_GUI);
    REQUIRE(latching.GetGuiValue() != stored);
    latching.GetInlet(0)->SetList(stored, YSE::T_GUI);
    CHECK(latching.GetGuiValue() == stored);

    // Momentary too: the string says "a press is pending", and writing it back
    // arms one. A stored event is an event, which is why restoring it replays
    // the press — inlet 0 is hot and everything on it emits.
    MultiSink stateSink;
    gTextButton pressed;
    pressed.SetParams("momentary play");
    TestHelpers::Wire(pressed, 0, stateSink);
    pressed.GetInlet(0)->SetBang(YSE::T_GUI);
    const std::string press = pressed.GetGuiValue();
    REQUIRE(press == "1");
    REQUIRE_FALSE(pressed.IsOn()); // the poll consumed it

    stateSink.reset();
    pressed.GetInlet(0)->SetList(press, YSE::T_GUI);
    CHECK(pressed.IsOn());
    CHECK(stateSink.gotInt);
    CHECK(stateSink.intValue == 1);
  }

  TEST_CASE("labelswitch: Max's settext / text are refused and still re-send (#557)") {
    // The label is a creation parameter: a string written from a message path is
    // exactly the write `Parameters` itself refuses. The hot-inlet rule still
    // applies — a message that addresses nothing changes nothing and emits.
    MultiSink stateSink, labelSink;
    gTextButton button;
    button.SetParams("mute");
    TestHelpers::Wire(button, 0, stateSink);
    TestHelpers::Wire(button, 1, labelSink);

    button.GetInlet(0)->SetInt(1, YSE::T_GUI);
    REQUIRE(labelSink.listValue == "mute");

    const std::string settext = "settext solo";
    const std::string text = "text solo";
    stateSink.reset();
    labelSink.reset();

    button.GetInlet(0)->SetList(settext, YSE::T_GUI);
    CHECK(button.Label() == "mute");
    CHECK(labelSink.listValue == "mute");
    CHECK(stateSink.intValue == 1); // unchanged, and re-sent

    button.GetInlet(0)->SetList(text, YSE::T_GUI);
    CHECK(button.Label() == "mute");
    CHECK(button.IsOn());
  }

  TEST_CASE("labelswitch: an unlabelled control sends an empty list (#557)") {
    // Honest rather than clever: there is no name to send, and a control with no
    // label is a `.t` a host draws differently.
    MultiSink stateSink, labelSink;
    gLed led;
    TestHelpers::Wire(led, 0, stateSink);
    TestHelpers::Wire(led, 1, labelSink);

    led.GetInlet(0)->SetInt(1, YSE::T_GUI);
    CHECK(stateSink.gotInt);
    CHECK(stateSink.intValue == 1);
    CHECK(labelSink.gotList);
    CHECK(labelSink.listValue.empty());
  }

  // ─── a host, through the public API ─────────────────────────────────────────

  TEST_CASE("labelswitch: a host drives a real patch through pHandle (#557)") {
    // The issue's use case as a host builds it: a labelled mute and a labelled
    // play button, wired to readouts, driven through the public handle API and
    // read back through the readouts' own handles. No internal pokes.
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* mute = p.CreateObject(YSE::OBJ::G_LED, "mute");
    YSE::pHandle* stateOut = p.CreateObject(YSE::OBJ::G_INT);
    YSE::pHandle* labelOut = p.CreateObject(YSE::OBJ::G_LIST);
    REQUIRE(mute != nullptr);
    REQUIRE(stateOut != nullptr);
    REQUIRE(labelOut != nullptr);
    p.Connect(mute, 0, stateOut, 0);
    p.Connect(mute, 1, labelOut, 0);

    // A click.
    mute->SetIntData(0, 1);
    CHECK(stateOut->GetGuiValue() == "1");
    // The name the host draws it with, straight out of the patch — which is the
    // whole of issue #557: `.t` could not have said this.
    CHECK(labelOut->GetGuiValue() == "mute");

    // A bang flips a latching control.
    mute->SetBang(0);
    CHECK(stateOut->GetGuiValue() == "0");

    // What a host polls to draw it, and what `.preset` stores.
    CHECK(mute->GuiValueIsSettable());
    CHECK(mute->GetGuiValueCount() == 1u);
    CHECK(mute->GetGuiValue() == "0");

    // And a momentary text button in the same patch, driven the same way.
    YSE::pHandle* play = p.CreateObject(YSE::OBJ::G_TEXTBUTTON, "momentary play");
    YSE::pHandle* playState = p.CreateObject(YSE::OBJ::G_INT);
    YSE::pHandle* playLabel = p.CreateObject(YSE::OBJ::G_LIST);
    REQUIRE(play != nullptr);
    REQUIRE(playState != nullptr);
    REQUIRE(playLabel != nullptr);
    p.Connect(play, 0, playState, 0);
    p.Connect(play, 1, playLabel, 0);

    play->SetBang(0);
    CHECK(playState->GetGuiValue() == "1");
    CHECK(playLabel->GetGuiValue() == "play");
    // The poll consumes the press, so the next frame sees the button unlit.
    CHECK(play->GetGuiValue() == "1");
    CHECK(play->GetGuiValue() == "0");
  }

  TEST_CASE("labelswitch: a stored GUI value restores the state on a fresh patch (#557)") {
    // What `.preset` will do: read the string out of one patcher and push it
    // into the equivalent object in another. Nothing but the public API — and
    // it only works because this family answers a number where `.b` and `.t`
    // answer a word.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* from = src.CreateObject(YSE::OBJ::G_TEXTBUTTON, "mute");
    REQUIRE(from != nullptr);
    from->SetIntData(0, 1);
    const std::string stored = from->GetGuiValue();
    REQUIRE(stored == "1");

    YSE::patcher dst;
    dst.create(2);
    YSE::pHandle* to = dst.CreateObject(YSE::OBJ::G_TEXTBUTTON, "mute");
    REQUIRE(to != nullptr);
    REQUIRE(to->GetGuiValue() != stored);

    to->SetListData(0, stored);
    CHECK(to->GetGuiValue() == stored);
  }

  TEST_CASE("labelswitch: params survive a DumpJSON / ParseJSON round trip (#557)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_TEXTBUTTON, "momentary start playback") != nullptr);
    REQUIRE(src.CreateObject(YSE::OBJ::G_LED, "clip") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".textbutton") != std::string::npos);
    CHECK(json.find(".led") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 2);

    YSE::pHandle* button = loaded.GetHandleFromList(0);
    YSE::pHandle* led = loaded.GetHandleFromList(1);
    REQUIRE(button != nullptr);
    REQUIRE(led != nullptr);
    if (std::string(button->Type()) != std::string(".textbutton")) {
      YSE::pHandle* swap = button;
      button = led;
      led = swap;
    }
    CHECK(std::string(button->Type()) == std::string(".textbutton"));
    CHECK(button->GetParams() == std::string("momentary start playback"));
    CHECK(std::string(led->Type()) == std::string(".led"));
    CHECK(led->GetParams() == std::string("clip"));

    // The label and the mode have to still *work*, not merely still be a
    // string. The live state is run-time state and is deliberately not saved —
    // a reload brings back the control the patch was written with, and off.
    CHECK(button->GetGuiValue() == "0");
    button->SetIntData(0, 0); // momentary: any input is a press
    CHECK(button->GetGuiValue() == "1");

    CHECK(led->GetGuiValue() == "0");
    led->SetIntData(0, 0); // latching: 0 is off
    CHECK(led->GetGuiValue() == "0");
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category and the parameter, which is what a binding
  // generator and a saved patch both key on.

  TEST_CASE("labelswitch: both document themselves as GUI with one param (#557)") {
    std::string firstDescription;
    for (const char* name : kNames) {
      CAPTURE(name);
      std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(name));
      REQUIRE(obj != nullptr);
      CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::GUI);

      const auto& docs = obj->GetParamDocs();
      REQUIRE(docs.size() == 1);
      CHECK(docs[0].name == "label");

      // The two share an implementation but not a description: each says what a
      // host is expected to draw, which is the only thing that differs.
      if (firstDescription.empty()) {
        firstDescription = obj->GetDescription();
      } else {
        CHECK(obj->GetDescription() != firstDescription);
      }
    }
  }

  // ─── real time ──────────────────────────────────────────────────────────────

  TEST_CASE("labelswitch: no message path allocates (#557)") {
    // Outlet 1 sends the stored label itself rather than a copy, which is only
    // sound because the label is a creation parameter and cannot change under a
    // send; the list handler compares its keyword and walks its numbers in place
    // rather than through a substr. The counter is read inside the scope and
    // asserted outside it, since doctest's own machinery allocates on first use.
    //
    // **The messages are built as strings before the scope opens, never passed
    // as literals inside it**, and that is not tidiness. `inlet::SetList` takes
    // a `const std::string&`, so a literal at the call site materialises a
    // temporary — a heap allocation whenever the text is longer than the
    // implementation's small-string buffer, and that buffer is *not* the same
    // width everywhere: 15 characters on libstdc++, 22 on libc++. A
    // 17-character list literal therefore costs nothing on the Windows/libc++
    // build and one allocation on the Linux/libstdc++ one, which is a probe
    // that passes locally and fails in CI while the object under test is
    // innocent. Hoisting the strings removes the test rig from the measurement.
    //
    // One of them is deliberately longer than *both* buffers, so the object is
    // driven from a genuinely heap-backed input on every platform.
    const std::string warmCell = "set 0 1";
    const std::string warmState = "0";
    const std::string cellWrite = "set 0 0";
    const std::string wholeState = "1";
    const std::string refused = "settext something-much-longer"; // past both buffers

    MultiSink stateSink, labelSink;
    gTextButton button;
    // A label longer than both small-string buffers, so the send path is
    // measured carrying a heap-backed string rather than an SSO one.
    button.SetParams("mute the whole master bus right now");
    REQUIRE(button.Label().size() > 22u);
    TestHelpers::Wire(button, 0, stateSink);
    TestHelpers::Wire(button, 1, labelSink);

    // Warm every path, so the sinks' own buffers and any first-call machinery
    // are not what the probe catches.
    button.GetInlet(0)->SetInt(1, YSE::T_GUI);
    button.GetInlet(0)->SetFloat(0.f, YSE::T_GUI);
    button.GetInlet(0)->SetBang(YSE::T_GUI);
    button.GetInlet(0)->SetList(warmCell, YSE::T_GUI);
    button.GetInlet(0)->SetList(warmState, YSE::T_GUI);
    REQUIRE(labelSink.gotList);
    REQUIRE(stateSink.gotInt);

    // Size the sink buffers independently of the object, so a long input string
    // is not what grows them inside the probe.
    const std::string sinkWarm(64, 'x');
    labelSink.GetInlet(0)->SetList(sinkWarm, YSE::T_GUI);
    stateSink.GetInlet(0)->SetList(sinkWarm, YSE::T_GUI);

    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      button.GetInlet(0)->SetInt(0, YSE::T_GUI);
      button.GetInlet(0)->SetFloat(2.4f, YSE::T_GUI);
      button.GetInlet(0)->SetBang(YSE::T_GUI);
      button.GetInlet(0)->SetList(cellWrite, YSE::T_GUI);
      button.GetInlet(0)->SetList(wholeState, YSE::T_GUI);
      button.GetInlet(0)->SetList(refused, YSE::T_GUI);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);
    // The sends really did happen, so the zero above is not a vacuous pass.
    CHECK(button.IsOn());
    CHECK(stateSink.intValue == 1);
    CHECK(labelSink.listValue == button.Label());
  }

  TEST_CASE("labelswitch: a momentary control allocates nothing either (#557)") {
    // The mode with the read-modify-write in it: the press path and the
    // consuming poll, neither of which may allocate.
    const std::string warmState = "1";
    const std::string clear = "0";

    MultiSink stateSink, labelSink;
    gLed led;
    led.SetParams("momentary clip indicator on the master bus");
    REQUIRE(led.Momentary());
    TestHelpers::Wire(led, 0, stateSink);
    TestHelpers::Wire(led, 1, labelSink);

    led.GetInlet(0)->SetBang(YSE::T_GUI);
    led.GetInlet(0)->SetList(warmState, YSE::T_GUI);
    REQUIRE(labelSink.gotList);
    const std::string sinkWarm(64, 'x');
    labelSink.GetInlet(0)->SetList(sinkWarm, YSE::T_GUI);
    stateSink.GetInlet(0)->SetList(sinkWarm, YSE::T_GUI);

    std::string polled;
    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      led.GetInlet(0)->SetBang(YSE::T_GUI);
      led.GetInlet(0)->SetInt(0, YSE::T_GUI);
      polled = led.GetGuiValue();
      led.GetInlet(0)->SetList(clear, YSE::T_GUI);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);
    CHECK(polled == "1");
    CHECK_FALSE(led.IsOn());
    CHECK(labelSink.listValue == led.Label());
  }

} // TEST_SUITE("patcher")
