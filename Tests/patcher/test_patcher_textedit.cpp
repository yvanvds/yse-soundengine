// Tests for `.textedit` (issue #560) — the patcher's first genuinely mutable
// string value, and the object the rest of the GUI family routed around.
//
// Five claims, and the cases are organised around them:
//
//   - **the value is a string, and it is editable at run time.** Every other
//     control's string is an immutable creation argument that a change replaces
//     the whole object over (#556, #557). Here a message rewrites it in place,
//     from whichever thread the message arrived on, and a poll reads it back.
//
//   - **nothing is a keyword.** Not on the inlet and not in the arguments: any
//     word this object reserved would be a word the field could not contain.
//     That is what makes Max's `set`, `clear` and `outputmode` unportable, and
//     it is why #551's `set <index> <value>` cell write is carved out for a
//     one-cell text control — pinned here, since the carve-out lives in
//     pObject.h and the object is the only thing that can demonstrate it.
//
//   - **the round trip is exact.** `GuiValueIsSettable()` promises inlet 0 takes
//     back what `GetGuiValue()` produced; this object is the identity function
//     on its own state, so the promise is tested over strings that would break
//     a control with reserved words, not just over well-behaved ones.
//
//   - **there is no symbol/list output mode**, because the patcher has no atom
//     types (`.tosymbol`'s finding, #490). The end-to-end case builds the
//     pairing that replaces it — `.textedit` into `.tosymbol` — through the
//     public handle API, so the claim is demonstrated rather than asserted.
//
//   - **nothing on a message path allocates**, which is the whole point of the
//     fixed cell and the try-lock guard.
//
// Plus the capacity refusal, the JSON round trip and the doc metadata.
//
// No audio device required.

#include <doctest/doctest.h>
#include <memory>
#include <string>

#include "patcher/guiObjects/gTextEdit.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"

using TestHelpers::MultiSink;
using YSE::PATCHER::gTextEdit;
using YSE::PATCHER::Register;

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("textedit: one hot inlet, one list outlet, registered (#560)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_TEXTEDIT);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".textedit"));
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::LIST);

    CHECK(YSE::patcher::IsValidObject(YSE::OBJ::G_TEXTEDIT));

    bool found = false;
    for (const auto& n : Register().AllNames()) {
      if (n == YSE::OBJ::G_TEXTEDIT) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("textedit: a bare object holds nothing (#560)") {
    gTextEdit field;
    CHECK(field.Text().empty());
    CHECK(field.GetGuiValue().empty());
  }

  // ─── the creation argument ──────────────────────────────────────────────────

  TEST_CASE("textedit: the argument is the initial text, joined with spaces (#560)") {
    gTextEdit field;
    field.SetParams("some/default/path.wav");
    CHECK(field.Text() == "some/default/path.wav");

    gTextEdit sentence;
    sentence.SetParams("hello world again");
    CHECK(sentence.Text() == "hello world again");
  }

  TEST_CASE("textedit: no word in the argument is a keyword (#560)") {
    // The one-position trap `multi` (#556) and `momentary` (#557) carry does not
    // exist here, and that is deliberate: a text field whose first word is
    // special is a text field that cannot hold that word.
    gTextEdit looksLikeAMode;
    looksLikeAMode.SetParams("list");
    CHECK(looksLikeAMode.Text() == "list");

    gTextEdit looksLikeACellWrite;
    looksLikeACellWrite.SetParams("set 0 hello");
    CHECK(looksLikeACellWrite.Text() == "set 0 hello");

    gTextEdit looksLikeMax;
    looksLikeMax.SetParams("clear");
    CHECK(looksLikeMax.Text() == "clear");
  }

  TEST_CASE("textedit: SetParams(\"\") empties the field (#560)") {
    gTextEdit field;
    field.SetParams("something");
    REQUIRE(field.Text() == "something");
    field.SetParams("");
    CHECK(field.Text().empty());
  }

  // ─── the grammar ────────────────────────────────────────────────────────────

  TEST_CASE("textedit: a list is the text, verbatim, and goes straight out (#560)") {
    MultiSink sink;
    gTextEdit field;
    TestHelpers::Wire(field, 0, sink);

    const std::string plain = "hello world";
    field.GetInlet(0)->SetList(plain, YSE::T_GUI);
    CHECK(field.Text() == plain);
    CHECK(sink.gotList);
    CHECK(sink.listValue == plain);

    // Every message Max would have read as a command is text here, and the
    // outlet says so. This is the "nothing is a keyword" claim, at the inlet.
    const char* const notKeywords[] = {"set 0 hello", "set 1",    "clear", "settext solo",
                                       "text solo",   "bang",     "list",  "outputmode 1",
                                       "symbol",      "set hello"};
    for (const char* candidate : notKeywords) {
      CAPTURE(candidate);
      const std::string typed(candidate);
      sink.reset();
      field.GetInlet(0)->SetList(typed, YSE::T_GUI);
      CHECK(field.Text() == typed);
      CHECK(sink.listValue == typed);
    }
  }

  TEST_CASE("textedit: an int and a float become the text that spells them (#560)") {
    // What a `.f` or a `.i` wired into the field puts in it — and a float keeps
    // its decimal point, so it still reads back as the float it was.
    MultiSink sink;
    gTextEdit field;
    TestHelpers::Wire(field, 0, sink);

    field.GetInlet(0)->SetInt(440, YSE::T_GUI);
    CHECK(field.Text() == "440");
    CHECK(sink.listValue == "440");

    field.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    CHECK(field.Text() == "0.5");
    CHECK(sink.listValue == "0.5");

    // Text, not a number: the outlet is a list outlet and the object never
    // re-types what it holds. A patch that wants the number puts a .fromsymbol
    // after it.
    CHECK_FALSE(sink.gotFloat);
    CHECK_FALSE(sink.gotInt);
  }

  TEST_CASE("textedit: a bang re-sends without changing the text (#560)") {
    MultiSink sink;
    gTextEdit field;
    TestHelpers::Wire(field, 0, sink);

    const std::string typed = "voice 3 freq";
    field.GetInlet(0)->SetList(typed, YSE::T_GUI);
    sink.reset();

    field.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(field.Text() == typed);
    CHECK(sink.gotList);
    CHECK(sink.listValue == typed);
  }

  TEST_CASE("textedit: an empty list is the clear (#560)") {
    // Max's `clear` message is not ported — it would eat the word — and this is
    // what replaces it. Still emits: inlet 0 is hot.
    MultiSink sink;
    gTextEdit field;
    field.SetParams("something");
    TestHelpers::Wire(field, 0, sink);
    REQUIRE(field.Text() == "something");

    const std::string empty;
    field.GetInlet(0)->SetList(empty, YSE::T_GUI);
    CHECK(field.Text().empty());
    CHECK(sink.gotList);
    CHECK(sink.listValue.empty());
  }

  TEST_CASE("textedit: text past the capacity is refused and what is stored is kept (#560)") {
    // Half a string is a different string, and a path that may be the audio
    // thread cannot grow the cell. The hot inlet still emits — what is emitted
    // is what is actually held, which is how a patch finds out.
    MultiSink sink;
    gTextEdit field;
    TestHelpers::Wire(field, 0, sink);

    const std::string atCapacity(gTextEdit::TEXT_CAPACITY, 'a');
    field.GetInlet(0)->SetList(atCapacity, YSE::T_GUI);
    REQUIRE(field.Text() == atCapacity);

    const std::string tooLong(gTextEdit::TEXT_CAPACITY + 1, 'b');
    sink.reset();
    field.GetInlet(0)->SetList(tooLong, YSE::T_GUI);
    CHECK(field.Text() == atCapacity);
    CHECK(sink.listValue == atCapacity);
  }

  // ─── the GUI value protocol (issue #551) ────────────────────────────────────

  TEST_CASE("textedit: it is the scalar one-cell case, and settable (#560)") {
    gTextEdit field;
    field.SetParams("a name");
    CHECK(field.GuiValueIsSettable());
    CHECK(field.GetGuiValueCount() == 1u);

    // Cell 0 of a scalar control is exactly the whole state, which the base
    // supplies rather than the object restating it.
    CHECK(field.GetGuiValueAt(0) == "a name");
    // Past the end is "", never the whole state again.
    CHECK(field.GetGuiValueAt(1).empty());
    CHECK(field.GetGuiValueAt(0xFFFFFFFFu).empty());

    // The poll is a look, not a consume — unlike `.b` and a momentary
    // `.textbutton`, whose polls clear what they report.
    CHECK(field.GetGuiValue() == "a name");
    CHECK(field.GetGuiValue() == "a name");
  }

  TEST_CASE("textedit: its own GetGuiValue round-trips, for any text at all (#560)") {
    // The unconditional promise GuiValueIsSettable() makes. This object is the
    // identity function on its own state, so the interesting cases are the ones
    // that would break a control with reserved words — which is exactly why the
    // `set <index> <value>` cell write is carved out for it in pObject.h.
    MultiSink sink;
    gTextEdit field;
    TestHelpers::Wire(field, 0, sink);

    const char* const payloads[] = {"hello world", "set 0 something else", "clear", "0 1 2",
                                    "set 0 set 0 set"};
    for (const char* payload : payloads) {
      CAPTURE(payload);
      const std::string typed(payload);
      field.GetInlet(0)->SetList(typed, YSE::T_GUI);
      const std::string stored = field.GetGuiValue();
      REQUIRE(stored == typed);

      const std::string other = "something quite different";
      field.GetInlet(0)->SetList(other, YSE::T_GUI);
      REQUIRE(field.GetGuiValue() != stored);

      // ... and back in through inlet 0, verbatim.
      sink.reset();
      field.GetInlet(0)->SetList(stored, YSE::T_GUI);
      CHECK(field.GetGuiValue() == stored);
      // A restore emits, because inlet 0 is hot — the call `.matrixctrl` made,
      // and what carries a restored value on into the rest of the patch.
      CHECK(sink.gotList);
      CHECK(sink.listValue == stored);
    }
  }

  // ─── a host, through the public API ─────────────────────────────────────────

  TEST_CASE("textedit: a host drives a real patch through pHandle (#560)") {
    // Issue #560's use case as a host builds it, with nothing but the public
    // API: a text field the user types a bus address into, feeding a `.tosymbol`
    // that collapses it into the single token a `.route` or a `.forward` takes
    // as a name. That pairing is *why* Max's symbol/list output mode is not
    // ported — the patcher has no atom types, so "as a symbol" is a token
    // collapse and `.tosymbol` is the object that performs it.
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* field = p.CreateObject(YSE::OBJ::G_TEXTEDIT, "default/path");
    YSE::pHandle* asTyped = p.CreateObject(YSE::OBJ::G_LIST);
    YSE::pHandle* collapse = p.CreateObject(YSE::OBJ::G_TOSYMBOL, "/");
    YSE::pHandle* asSymbol = p.CreateObject(YSE::OBJ::G_LIST);
    REQUIRE(field != nullptr);
    REQUIRE(asTyped != nullptr);
    REQUIRE(collapse != nullptr);
    REQUIRE(asSymbol != nullptr);
    p.Connect(field, 0, asTyped, 0);
    p.Connect(field, 0, collapse, 0);
    p.Connect(collapse, 0, asSymbol, 0);

    // The creation argument is what the field comes up holding.
    CHECK(field->GetGuiValue() == "default/path");

    // The user types. The text leaves as text on one branch and as one token on
    // the other, which is the whole of the output-mode question.
    field->SetListData(0, "voice 3 freq");
    CHECK(field->GetGuiValue() == "voice 3 freq");
    CHECK(asTyped->GetGuiValue() == "voice 3 freq");
    CHECK(asSymbol->GetGuiValue() == "voice/3/freq");

    // A number arriving from elsewhere in the patch fills the field with the
    // text that spells it.
    field->SetFloatData(0, 0.5f);
    CHECK(field->GetGuiValue() == "0.5");
    CHECK(asTyped->GetGuiValue() == "0.5");

    // A bang re-sends what is held, unchanged.
    field->SetListData(0, "master out");
    field->SetBang(0);
    CHECK(field->GetGuiValue() == "master out");
    CHECK(asSymbol->GetGuiValue() == "master/out");

    // And what a host polls to draw it, and what `.preset` stores.
    CHECK(field->GuiValueIsSettable());
    CHECK(field->GetGuiValueCount() == 1u);
    CHECK(field->GetGuiValueAt(0) == "master out");
  }

  TEST_CASE("textedit: a stored GUI value restores the text on a fresh patch (#560)") {
    // What `.preset` will do: read the string out of one patcher and push it
    // into the equivalent object in another. The text deliberately contains the
    // protocol's own keyword, since that is the case the carve-out exists for.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* from = src.CreateObject(YSE::OBJ::G_TEXTEDIT);
    REQUIRE(from != nullptr);
    from->SetListData(0, "set 0 the user really typed this");
    const std::string stored = from->GetGuiValue();
    REQUIRE(stored == "set 0 the user really typed this");

    YSE::patcher dst;
    dst.create(2);
    YSE::pHandle* to = dst.CreateObject(YSE::OBJ::G_TEXTEDIT);
    REQUIRE(to != nullptr);
    REQUIRE(to->GetGuiValue() != stored);

    to->SetListData(0, stored);
    CHECK(to->GetGuiValue() == stored);
  }

  TEST_CASE("textedit: params survive a DumpJSON / ParseJSON round trip (#560)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* original = src.CreateObject(YSE::OBJ::G_TEXTEDIT, "some default text");
    REQUIRE(original != nullptr);
    // Live text is run-time state and is deliberately not saved with the patch.
    original->SetListData(0, "typed after loading");
    const std::string json = src.DumpJSON();
    CHECK(json.find(".textedit") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* restored = loaded.GetHandleFromList(0);
    REQUIRE(restored != nullptr);
    CHECK(std::string(restored->Type()) == std::string(".textedit"));
    CHECK(restored->GetParams() == std::string("some default text"));

    // A reload brings back the field the patch was written with, not what was
    // typed into it — and it has to still *work*, not merely still be a string.
    CHECK(restored->GetGuiValue() == "some default text");
    restored->SetListData(0, "and now something else");
    CHECK(restored->GetGuiValue() == "and now something else");
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category and the parameter, which is what a binding
  // generator and a saved patch both key on.

  TEST_CASE("textedit: documents itself as GUI with one param (#560)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(YSE::OBJ::G_TEXTEDIT));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::GUI);
    CHECK_FALSE(obj->GetDescription().empty());

    const auto& docs = obj->GetParamDocs();
    REQUIRE(docs.size() == 1);
    CHECK(docs[0].name == "text");
  }

  // ─── real time ──────────────────────────────────────────────────────────────

  TEST_CASE("textedit: no message path allocates (#560)") {
    // The claim the fixed cell and the try-lock guard exist for. A store is a
    // memcpy into storage built at construction; an emit copies out into a
    // buffer reserved at construction and hands the outlet the
    // `const std::string&` it wants. The counter is read inside the scope and
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
    // Every one of them is deliberately longer than *both* buffers, so the
    // object is driven from a genuinely heap-backed input throughout — which is
    // the case that matters here, since a field holding only SSO-sized text
    // would never exercise the cell.
    //
    // `GetGuiValue()` is *not* in the probe, and that is by design rather than
    // by omission: the protocol in pObject.h specifies it as "one call, one
    // allocation", it returns a `std::string` by value, and it is a host-thread
    // call. Nothing the audio thread reaches goes through it.
    const std::string warmA = "a first message far longer than any small-string buffer";
    const std::string warmB = "a second message, also comfortably past both of them";
    const std::string typed = "voice 3 freq on the master bus, which is a long name";
    const std::string keywordish = "set 0 a message that only looks like a cell write";
    const std::string tooLong(gTextEdit::TEXT_CAPACITY + 1, 'z'); // refused, and must not allocate

    MultiSink sink;
    gTextEdit field;
    field.SetParams("an initial text that is itself past both small-string buffers");
    REQUIRE(field.Text().size() > 22u);
    TestHelpers::Wire(field, 0, sink);

    // Warm every path, so the sink's own buffer and any first-call machinery are
    // not what the probe catches.
    field.GetInlet(0)->SetList(warmA, YSE::T_GUI);
    field.GetInlet(0)->SetList(warmB, YSE::T_GUI);
    field.GetInlet(0)->SetInt(1, YSE::T_GUI);
    field.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    field.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(sink.gotList);

    // Size the sink's buffer independently of the object, so a long input string
    // is not what grows it inside the probe.
    const std::string sinkWarm(gTextEdit::TEXT_CAPACITY, 'x');
    sink.GetInlet(0)->SetList(sinkWarm, YSE::T_GUI);

    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      field.GetInlet(0)->SetList(typed, YSE::T_GUI);
      field.GetInlet(0)->SetList(keywordish, YSE::T_GUI);
      field.GetInlet(0)->SetList(tooLong, YSE::T_GUI);
      field.GetInlet(0)->SetInt(440, YSE::T_GUI);
      field.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
      field.GetInlet(0)->SetBang(YSE::T_GUI);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);
    // The sends really did happen, so the zero above is not a vacuous pass — and
    // the over-long message really was refused rather than stored.
    CHECK(field.Text() == "0.5");
    CHECK(sink.listValue == "0.5");
  }

} // TEST_SUITE("patcher")
