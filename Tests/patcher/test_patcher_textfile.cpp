// Tests for .textfile (issue #499) — the patcher's line-oriented text store.
//
// Seven things are worth pinning here, and every one of them is something an
// implementation can get wrong without ever crashing:
//
//   - **the line is the unit.** Several messages accumulate on one line
//     separated by single spaces, and only `cr` ends it. An object that made one
//     line per message would pass every count-based test and be .capture with
//     extra steps.
//   - **the separator lives between messages, not after each one.** Max keeps a
//     trailing space and has `cr` and `tab` replace it; keeping it between gives
//     the same visible text with nothing to fix up. The observable consequence is
//     `tab`: 5, tab, 6 must be "5\t6" and never "5\t 6".
//   - **`cr` on nothing leaves a blank line.** A carriage return always goes at
//     the end of the contents, so two in a row separate two full lines and one on
//     a fresh object leaves a blank. This is the corner a "close the current
//     line" implementation silently drops.
//   - **`line` numbers from 1 and prepends `set`.** Max's, and the `set` is not
//     decoration: .table, .funbuff and .bucket all take one, so a `line` wired
//     into a .table has to actually load it. That is asserted end to end.
//   - **a dumped line leaves in the kind it is.** .route's rule — "60" as the int
//     60, "60.5" as that float, anything else as a list — and an empty line still
//     leaves, so a dump sends exactly as many messages as `query` reports.
//   - **the fifteen reserved words.** `clear`, `cr`, `tab`, `dump`, `line`,
//     `query`, `symbol` and `t_symbol` do their jobs; `read`, `write`, `open`,
//     `wclose`, `settitle`, `filetype`, `precision` and `stringout` are consumed
//     and do nothing. All of them are consumed rather than *stored*, because Max
//     dispatches on the selector and so cannot store them either — and `symbol
//     clear` is Max's own escape hatch for storing one anyway.
//   - **the filename is a parameter and the contents are not.** The filename
//     survives a save; the text deliberately does not, which is where this object
//     sides with .capture against .coll, because Max gives `text` no save flag
//     and keeps its contents in a file.
//
// No audio device and no engine of its own, except where a real patcher graph is
// the point.

#include <doctest/doctest.h>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "patcher/genericObjects/gTextfile.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/sinks.hpp"

using YSE::PATCHER::gTextfile;

namespace {

  // Records every value it receives, in order and with its kind. The order and
  // the kinds are half of what this object promises — a dump of three lines has
  // to read back as three sends, and a line holding "60" has to arrive as an int.
  struct Recorder : YSE::PATCHER::pObject {
    // "i60", "f60.5", "sfoo", "!" for a bang — one string per send.
    std::vector<std::string> seen;

    Recorder() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) { seen.emplace_back("!"); });
      inputs.back().RegisterInt(
          [this](int v, int, YSE::THREAD) { seen.push_back("i" + std::to_string(v)); });
      inputs.back().RegisterFloat([this](float v, int, YSE::THREAD) {
        char text[32];
        std::snprintf(text, sizeof(text), "f%.2f", (double)v);
        seen.emplace_back(text);
      });
      inputs.back().RegisterList(
          [this](const std::string& v, int, YSE::THREAD) { seen.push_back("s" + v); });
    }
    const char* Type() const override {
      return "recorder";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

    void reset() {
      seen.clear();
    }
  };

  // A standalone .textfile with a recorder on each outlet. Standalone where the
  // patcher is not the point: a test that needed one could not tell a refused
  // message from a message the patcher never delivered.
  struct Rig {
    Recorder text;
    Recorder lines;
    gTextfile obj;

    Rig() {
      obj.ConnectOutlet(text.GetInlet(0), 0);
      obj.ConnectOutlet(lines.GetInlet(0), 1);
    }

    void List(const std::string& message) {
      obj.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    void Int(int value) {
      obj.GetInlet(0)->SetInt(value, YSE::T_GUI);
    }
    void Float(float value) {
      obj.GetInlet(0)->SetFloat(value, YSE::T_GUI);
    }

    // What a `dump` sends, as the sequence of tagged strings above.
    std::vector<std::string> Dumped() {
      text.reset();
      List("dump");
      return text.seen;
    }

    // What `query` reports.
    int Queried() {
      lines.reset();
      List("query");
      REQUIRE(lines.seen.size() == 1);
      return std::stoi(lines.seen[0].substr(1));
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("textfile: registered, one inlet and two outlets (#499)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::G_TEXTFILE);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == ".textfile");
    CHECK(obj->GetInputs() == 1);
    // Two, not Max's three: Max's middle outlet bangs when a file has finished
    // loading, and `read` does nothing yet. When file I/O lands (#683) that
    // outlet is appended after the count rather than inserted in Max's position,
    // so no saved patch's cords shift.
    CHECK(obj->GetOutputs() == 2);
  }

  TEST_CASE("textfile: appears in the registry's name list (#499)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_TEXTFILE)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("textfile: .text is still the label object, not this one (#499)") {
    // Issue #499 offers renaming the label object to .label as the alternative to
    // the .textfile name. It is deliberately not renamed: the type name is
    // written into every saved patch's JSON.
    CHECK(std::string(YSE::OBJ::G_TEXT) == ".text");
    CHECK(std::string(YSE::OBJ::G_TEXTFILE) == ".textfile");
  }

  TEST_CASE("textfile: the inlet takes int, float and list but not bang (#499)") {
    gTextfile obj;
    const unsigned int accepted = obj.GetInlet(0)->GetAcceptedTypes();
    CHECK((accepted & YSE::PATCHER::IT_INT) != 0);
    CHECK((accepted & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((accepted & YSE::PATCHER::IT_LIST) != 0);
    // Max documents no bang method for `text`, and one that dumped would give the
    // object two spellings of `dump` on the same inlet.
    CHECK((accepted & YSE::PATCHER::IT_BANG) == 0);
  }

  TEST_CASE("textfile: a fresh object is empty and a dump sends nothing (#499)") {
    Rig rig;
    CHECK(rig.obj.LineCount() == 0);
    CHECK_FALSE(rig.obj.LineIsOpen());
    CHECK(rig.Dumped().empty());
    CHECK(rig.Queried() == 0);
  }

  TEST_CASE("textfile: Calculate() sends nothing (#499)") {
    // The rule .route, .sel, .value, .coll, .capture and .table establish: an
    // object driven by its inlet must not re-emit on every DSP tick.
    Rig rig;
    rig.Int(60);
    rig.text.reset();
    rig.obj.Calculate(YSE::T_DSP);
    rig.obj.Calculate(YSE::T_DSP);
    CHECK(rig.text.seen.empty());
  }

  // ─── appending ──────────────────────────────────────────────────────────────

  TEST_CASE("textfile: the first message starts a line and leaves it open (#499)") {
    Rig rig;
    rig.Int(60);
    REQUIRE(rig.obj.LineCount() == 1);
    CHECK(rig.obj.LineAt(0) == "60");
    CHECK(rig.obj.LineIsOpen());
  }

  TEST_CASE("textfile: several messages accumulate on one line, space-separated (#499)") {
    // The whole difference from .capture, which would make three items of this.
    Rig rig;
    rig.Int(60);
    rig.Int(70);
    rig.List("foo");
    CHECK(rig.obj.LineCount() == 1);
    CHECK(rig.obj.LineAt(0) == "60 70 foo");
  }

  TEST_CASE("textfile: a list is stored as it arrived (#499)") {
    // Max: "the message is stored in the text object". It is already the text the
    // patch composed, so its atoms are not re-spelled.
    Rig rig;
    rig.List("60 70 80");
    CHECK(rig.obj.LineAt(0) == "60 70 80");
  }

  TEST_CASE("textfile: numbers are spelled the patcher's one way (#499)") {
    // WriteInt for an int and ExprFormatValue for a float — the fewest digits
    // that read back as the same value, so an int stays an int and 0.5f does not
    // become 0.50000000.
    Rig rig;
    rig.Int(60);
    rig.Float(60.5f);
    rig.Float(0.5f);
    CHECK(rig.obj.LineAt(0) == "60 60.5 0.5");
  }

  TEST_CASE("textfile: cr ends the line and the next message starts a new one (#499)") {
    Rig rig;
    rig.Int(60);
    rig.List("cr");
    CHECK(rig.obj.LineCount() == 1);
    CHECK_FALSE(rig.obj.LineIsOpen());
    rig.Int(70);
    REQUIRE(rig.obj.LineCount() == 2);
    CHECK(rig.obj.LineAt(0) == "60");
    CHECK(rig.obj.LineAt(1) == "70");
  }

  TEST_CASE("textfile: a cr with no line open leaves a blank line (#499)") {
    // The corner a "close the current line" implementation silently drops. Max
    // puts a carriage return at the end of the *contents*, so a second one lands
    // after the first and leaves an empty line between two full ones.
    Rig rig;
    rig.Int(60);
    rig.List("cr");
    rig.List("cr");
    rig.Int(70);
    REQUIRE(rig.obj.LineCount() == 3);
    CHECK(rig.obj.LineAt(0) == "60");
    CHECK(rig.obj.LineAt(1).empty());
    CHECK(rig.obj.LineAt(2) == "70");
  }

  TEST_CASE("textfile: a cr on a fresh object leaves one blank line (#499)") {
    Rig rig;
    rig.List("cr");
    REQUIRE(rig.obj.LineCount() == 1);
    CHECK(rig.obj.LineAt(0).empty());
    CHECK(rig.Queried() == 1);
  }

  TEST_CASE("textfile: tab writes a tab and no space after it (#499)") {
    // Max: "if the last character in text is a space, the tab stop replaces that
    // space". The space is never written here, so the pending separator is
    // cleared instead — the same rule with nothing to undo. "5\t 6" is the bug
    // this pins.
    Rig rig;
    rig.Int(5);
    rig.List("tab");
    rig.Int(6);
    CHECK(rig.obj.LineAt(0) == "5\t6");
  }

  TEST_CASE("textfile: a tab on a fresh object opens a line (#499)") {
    Rig rig;
    rig.List("tab");
    REQUIRE(rig.obj.LineCount() == 1);
    CHECK(rig.obj.LineAt(0) == "\t");
  }

  TEST_CASE("textfile: clear erases the contents (#499)") {
    Rig rig;
    rig.List("60 70");
    rig.List("cr");
    rig.Int(80);
    REQUIRE(rig.obj.LineCount() == 2);
    rig.List("clear");
    CHECK(rig.obj.LineCount() == 0);
    CHECK_FALSE(rig.obj.LineIsOpen());
    CHECK(rig.Dumped().empty());
    // And the object still works afterwards.
    rig.Int(90);
    CHECK(rig.obj.LineAt(0) == "90");
  }

  // ─── output ─────────────────────────────────────────────────────────────────

  TEST_CASE("textfile: dump sends every line in order, bare (#499)") {
    Rig rig;
    rig.List("60 70");
    rig.List("cr");
    rig.List("foo bar");
    auto dumped = rig.Dumped();
    REQUIRE(dumped.size() == 2);
    CHECK(dumped[0] == "s60 70");
    CHECK(dumped[1] == "sfoo bar");
  }

  TEST_CASE("textfile: a dumped line leaves in the kind it is (#499)") {
    // .route's rule, which .coll and .capture already follow.
    Rig rig;
    rig.Int(60);
    rig.List("cr");
    rig.Float(60.5f);
    rig.List("cr");
    rig.List("foo");
    auto dumped = rig.Dumped();
    REQUIRE(dumped.size() == 3);
    CHECK(dumped[0] == "i60");
    CHECK(dumped[1] == "f60.50");
    CHECK(dumped[2] == "sfoo");
  }

  TEST_CASE("textfile: an empty line still leaves, so dump and query agree (#499)") {
    Rig rig;
    rig.Int(60);
    rig.List("cr");
    rig.List("cr");
    rig.Int(70);
    CHECK(rig.Queried() == 3);
    auto dumped = rig.Dumped();
    REQUIRE(dumped.size() == 3);
    CHECK(dumped[1] == "s");
  }

  TEST_CASE("textfile: line sends the contents preceded by set, numbering from 1 (#499)") {
    Rig rig;
    rig.List("60 70");
    rig.List("cr");
    rig.List("foo");

    rig.text.reset();
    rig.List("line 1");
    REQUIRE(rig.text.seen.size() == 1);
    CHECK(rig.text.seen[0] == "sset 60 70");

    rig.text.reset();
    rig.List("line 2");
    REQUIRE(rig.text.seen.size() == 1);
    CHECK(rig.text.seen[0] == "sset foo");
  }

  TEST_CASE("textfile: a line number below 1 is converted to line 1 (#499)") {
    // Max: "any line number message less than 1 is converted to line 1."
    Rig rig;
    rig.List("first");
    rig.List("cr");
    rig.List("second");

    for (const char* message : {"line 0", "line -5"}) {
      rig.text.reset();
      rig.List(message);
      REQUIRE(rig.text.seen.size() == 1);
      CHECK(rig.text.seen[0] == "sset first");
    }
  }

  TEST_CASE("textfile: a nonexistent line sends nothing (#499)") {
    // Max: "if a nonexistent line number is requested, nothing is sent out."
    Rig rig;
    rig.List("only");
    rig.text.reset();
    rig.List("line 2");
    rig.List("line 99");
    CHECK(rig.text.seen.empty());
    // And a bare `line` is still the `line` selector — consumed, not stored.
    rig.List("line");
    CHECK(rig.text.seen.empty());
    CHECK(rig.obj.LineAt(0) == "only");
  }

  TEST_CASE("textfile: line on an empty line sends set alone (#499)") {
    Rig rig;
    rig.List("cr");
    rig.text.reset();
    rig.List("line 1");
    REQUIRE(rig.text.seen.size() == 1);
    CHECK(rig.text.seen[0] == "sset");
  }

  TEST_CASE("textfile: query reports the line count out the second outlet (#499)") {
    Rig rig;
    CHECK(rig.Queried() == 0);
    rig.Int(1);
    CHECK(rig.Queried() == 1);
    rig.List("cr");
    rig.Int(2);
    CHECK(rig.Queried() == 2);
    // query sends nothing out the text outlet.
    rig.text.reset();
    rig.List("query");
    CHECK(rig.text.seen.empty());
  }

  // ─── reserved words ─────────────────────────────────────────────────────────

  TEST_CASE("textfile: symbol stores a word that would otherwise be a command (#499)") {
    // Max's own escape hatch: "symbol clear stores the word clear ... rather than
    // erasing the contents."
    Rig rig;
    rig.Int(60);
    rig.List("symbol clear");
    REQUIRE(rig.obj.LineCount() == 1);
    CHECK(rig.obj.LineAt(0) == "60 clear");

    rig.List("t_symbol dump");
    CHECK(rig.obj.LineAt(0) == "60 clear dump");
  }

  TEST_CASE("textfile: the eight inert words are consumed, not stored (#499)") {
    // Max dispatches on the selector, so a `text` in Max cannot store these
    // either — contents differing from Max's for the same patch is the one thing
    // this object must not produce. There is no window here, the two attribute
    // names set an attribute in Max, and file I/O is issue #683.
    Rig rig;
    rig.Int(60);
    rig.text.reset();
    rig.lines.reset();
    for (const char* message : {"read notes.txt", "write notes.txt", "open", "wclose",
                                "settitle Notes", "filetype TEXT", "precision 3", "stringout 1"}) {
      rig.List(message);
    }
    // Nothing stored and nothing sent.
    REQUIRE(rig.obj.LineCount() == 1);
    CHECK(rig.obj.LineAt(0) == "60");
    CHECK(rig.text.seen.empty());
    CHECK(rig.lines.seen.empty());
  }

  TEST_CASE("textfile: a word that is not a command is stored (#499)") {
    // Max's `anything`. The reserved list is exactly the reserved list — a
    // leading `lines` or `crumb` is data.
    Rig rig;
    rig.List("crumb 1");
    rig.List("lines");
    CHECK(rig.obj.LineAt(0) == "crumb 1 lines");
  }

  // ─── bounds ─────────────────────────────────────────────────────────────────

  TEST_CASE("textfile: an append past the line capacity is refused whole (#499)") {
    // Refused rather than truncated: half a line is a different line, and it is
    // refused *silently* because the inlet may be the audio thread.
    Rig rig;
    const std::string full(gTextfile::LINE_CAPACITY, 'x');
    rig.List(full);
    REQUIRE(rig.obj.LineAt(0).size() == gTextfile::LINE_CAPACITY);

    rig.List("y");
    CHECK(rig.obj.LineAt(0).size() == gTextfile::LINE_CAPACITY);
    CHECK(rig.obj.LineCount() == 1);

    // A single message longer than the capacity never lands at all, and does not
    // leave an empty line behind for `query` and `dump` to report either.
    rig.List("clear");
    rig.List(std::string(gTextfile::LINE_CAPACITY + 1, 'z'));
    CHECK(rig.obj.LineCount() == 0);
    CHECK(rig.Queried() == 0);
  }

  TEST_CASE("textfile: a line past the table is refused rather than growing it (#499)") {
    Rig rig;
    for (std::size_t i = 0; i < gTextfile::MAX_LINES; i++) {
      rig.Int((int)i);
      rig.List("cr");
    }
    REQUIRE(rig.obj.LineCount() == gTextfile::MAX_LINES);

    // The table is allocated whole at construction, so growing it would allocate
    // on whichever thread the message arrived on.
    rig.Int(999);
    CHECK(rig.obj.LineCount() == gTextfile::MAX_LINES);
    CHECK(rig.obj.LineAt(gTextfile::MAX_LINES - 1) == "255");
  }

  // ─── what a save carries ────────────────────────────────────────────────────

  TEST_CASE("textfile: the filename survives a DumpJSON / ParseJSON round trip (#499)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* obj = src.CreateObject(YSE::OBJ::G_TEXTFILE, "mydata.txt");
    REQUIRE(obj != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".textfile") != std::string::npos);
    CHECK(json.find("mydata.txt") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".textfile");
    CHECK(std::string(copy->GetParams()) == "mydata.txt");
  }

  TEST_CASE("textfile: the contents deliberately do not survive a save (#499)") {
    // The family's rule is to save exactly where Max has a save flag: `coll` has
    // "save data with patcher", `table` and `funbuff` have `embed`, and `text`
    // has none of them, because Max keeps a text's contents in a *file* reached
    // by read / write. When that lands here (#683) they will live there too.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* obj = src.CreateObject(YSE::OBJ::G_TEXTFILE);
    REQUIRE(obj != nullptr);
    obj->SetListData(0, "60 70");
    obj->SetListData(0, "cr");
    obj->SetIntData(0, 80);

    const std::string json = src.DumpJSON();
    CHECK(json.find("\"state\"") == std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);

    Recorder out;
    Recorder count;
    YSE::pHandle outHandle(&out);
    YSE::pHandle countHandle(&count);
    loaded.Connect(copy, 0, &outHandle, 0);
    loaded.Connect(copy, 1, &countHandle, 0);

    copy->SetListData(0, "dump");
    CHECK(out.seen.empty());
    copy->SetListData(0, "query");
    REQUIRE(count.seen.size() == 1);
    CHECK(count.seen[0] == "i0");
  }

  // ─── end to end, through a real patcher graph ───────────────────────────────

  TEST_CASE("textfile: a real graph builds lines and reads them back (#499)") {
    // The use case the object exists for, run through a real graph rather than a
    // directly wired outlet: notes are transposed on the way in, several land on
    // one line, a .trigger ends the line and starts the next, and only afterwards
    // is the object asked for its contents.
    Recorder text;
    Recorder count;
    YSE::pHandle textHandle(&text);
    YSE::pHandle countHandle(&count);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* add = p.CreateObject(YSE::OBJ::G_ADD, "12");
    YSE::pHandle* tf = p.CreateObject(YSE::OBJ::G_TEXTFILE, "notes.txt");
    REQUIRE(add != nullptr);
    REQUIRE(tf != nullptr);
    p.Connect(add, 0, tf, 0);
    p.Connect(tf, 0, &textHandle, 0);
    p.Connect(tf, 1, &countHandle, 0);

    add->SetIntData(0, 60);
    add->SetIntData(0, 62);
    tf->SetListData(0, "cr");
    add->SetIntData(0, 64);

    // Nothing has left the object yet: appending is silent.
    CHECK(text.seen.empty());

    tf->SetListData(0, "query");
    REQUIRE(count.seen.size() == 1);
    CHECK(count.seen[0] == "i2");

    tf->SetListData(0, "dump");
    REQUIRE(text.seen.size() == 2);
    // `.+` answers in floats whatever it was given, so the line carries what the
    // wire carried.
    CHECK(text.seen[0] == "s72. 74.");
    CHECK(text.seen[1] == "f76.00");
  }

  TEST_CASE("textfile: a line message loads a real .table through its set (#499)") {
    // Why the `set` prefix is ported rather than dropped as decoration: Max's
    // "can be sent to any other object for which that particular set message is
    // appropriate", and .table's set is exactly that. This is the whole round
    // trip — text built by messages, stored as a line, and handed to another
    // object as a working message.
    Recorder out;
    YSE::pHandle outHandle(&out);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* tf = p.CreateObject(YSE::OBJ::G_TEXTFILE);
    YSE::pHandle* table = p.CreateObject(YSE::OBJ::G_TABLE, "8");
    REQUIRE(tf != nullptr);
    REQUIRE(table != nullptr);
    p.Connect(tf, 0, table, 0);
    p.Connect(table, 0, &outHandle, 0);

    // `.table`'s set is "set <start> <values...>", so the line is the argument
    // list without the word.
    tf->SetListData(0, "0 11 22 33");
    tf->SetListData(0, "line 1");

    // The table now holds what the text described.
    table->SetIntData(0, 0);
    table->SetIntData(0, 2);
    REQUIRE(out.seen.size() == 2);
    CHECK(out.seen[0] == "i11");
    CHECK(out.seen[1] == "i33");
  }

  TEST_CASE("textfile: a .trigger drives cr and dump round a real graph (#499)") {
    // The two messages arriving the way a patch would send them: .trigger fires
    // right to left, so `cr` reaches the object before `dump` does and the dump
    // therefore sees a closed line.
    Recorder text;
    YSE::pHandle textHandle(&text);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* tf = p.CreateObject(YSE::OBJ::G_TEXTFILE);
    YSE::pHandle* trig = p.CreateObject(YSE::OBJ::G_TRIGGER, "dump cr");
    REQUIRE(tf != nullptr);
    REQUIRE(trig != nullptr);
    p.Connect(trig, 0, tf, 0);
    p.Connect(trig, 1, tf, 0);
    p.Connect(tf, 0, &textHandle, 0);

    tf->SetListData(0, "10 20 30");

    trig->SetBang(0);
    REQUIRE(text.seen.size() == 1);
    CHECK(text.seen[0] == "s10 20 30");

    // The `cr` really did land: the next message starts a second line.
    text.reset();
    tf->SetIntData(0, 40);
    tf->SetListData(0, "dump");
    REQUIRE(text.seen.size() == 2);
    CHECK(text.seen[1] == "i40");
  }

} // TEST_SUITE
