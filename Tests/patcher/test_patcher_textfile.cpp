// Tests for .textfile (issue #499) — the patcher's line-oriented text store.
//
// Eight things are worth pinning here, and every one of them is something an
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
//     `query`, `symbol`, `t_symbol`, `read` and `write` do their jobs; `open`,
//     `wclose`, `settitle`, `filetype`, `precision` and `stringout` are consumed
//     and do nothing. All of them are consumed rather than *stored*, because Max
//     dispatches on the selector and so cannot store them either — and `symbol
//     clear` is Max's own escape hatch for storing one anyway.
//   - **the filename is a parameter and the contents are not.** The filename
//     survives a save; the text deliberately does not, which is where this object
//     sides with .capture against .coll, because Max gives `text` no save flag
//     and keeps its contents in a file.
//   - **the contents round-trip through that file, and never on the message
//     path.** Issue #687. A `read` arrives on whichever thread dispatched it, so
//     the proof that matters is not only that the lines come back but that
//     *nothing happens in the handler*: the contents are untouched until the
//     patcher renders a block. And the trip is exact rather than merely equal —
//     whether the last line is still taking appends survives it, because Max's
//     buffer is flat text where a `cr` is a character. Asserted through a real
//     patcherImplementation and a real file on disk, because both halves — the
//     background job and the completion delivered into a dispatch frame — only
//     exist there.
//
// No audio device and no engine of its own, except where a real patcher graph is
// the point.

#include <doctest/doctest.h>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#include "patcher/genericObjects/gTextfile.h"
#include "patcher/inlet.h"
#include "patcher/io/fileScheduler.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"

using TestHelpers::Wire;
using YSE::PATCHER::gTextfile;
using YSE::PATCHER::patcherImplementation;

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
    Recorder file;
    gTextfile obj;

    Rig() {
      Wire(obj, 0, text);
      Wire(obj, 1, lines);
      Wire(obj, 2, file);
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

  // ─── file helpers (issue #687) ────────────────────────────────────────────

  // A path in the system temp directory, deleted first so a leftover from an
  // earlier run cannot make a test pass for the wrong reason.
  std::string TempFile(const char* name) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return path.string();
  }

  void WriteWholeFile(const std::string& path, const std::string& contents) {
    std::ofstream out(path, std::ios::binary);
    out << contents;
  }

  std::string ReadWholeFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }

  void Remove(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
  }

  // Drive a patcher until its file requests have landed. The two halves are
  // deterministic for different reasons: WaitIdle joins the background pool's
  // jobs (so no sleep and no polling), and the single Calculate is the dispatch
  // frame the completions are handed out in — there is deliberately no other way
  // for them to arrive.
  void SettleFiles(patcherImplementation& p) {
    YSE::PATCHER::fileScheduler* io = p.FileIO();
    REQUIRE(io != nullptr);
    io->WaitIdle();
    p.Calculate(YSE::T_DSP);
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("textfile: registered, one inlet and three outlets (#499, #687)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::G_TEXTFILE);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == ".textfile");
    CHECK(obj->GetInputs() == 1);
    // All three of Max's, but not in Max's order: the file outlet was appended
    // rather than inserted in Max's middle position, because #499 shipped this
    // object with two outlets and moving the line count would shift the cords of
    // every patch saved since (#687).
    CHECK(obj->GetOutputs() == 3);
    CHECK(obj->OutputDataType(1) == YSE::OUT_TYPE::INT);
    CHECK(obj->OutputDataType(2) == YSE::OUT_TYPE::BANG);
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

  TEST_CASE("textfile: the six inert words are consumed, not stored (#499, #687)") {
    // Max dispatches on the selector, so a `text` in Max cannot store these
    // either — contents differing from Max's for the same patch is the one thing
    // this object must not produce. There is no window here, there is no file
    // dialog for `filetype` to narrow, and the two attribute names set an
    // attribute in Max. `read` and `write` left this list in #687.
    Rig rig;
    rig.Int(60);
    rig.text.reset();
    rig.lines.reset();
    for (const char* message :
         {"open", "wclose", "settitle Notes", "filetype TEXT", "precision 3", "stringout 1"}) {
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

  // ─── files (issue #687) ─────────────────────────────────────────────────────

  TEST_CASE("textfile: a standalone object consumes read and write without storing them (#687)") {
    // No patcher means no file plumbing, and the honest answer is silence rather
    // than storing the word: Max dispatches on the selector either way, so the
    // contents must not differ from Max's for the same patch.
    Rig rig;
    rig.Int(60);
    rig.text.reset();
    rig.lines.reset();

    rig.List("read notes.txt");
    rig.List("write notes.txt");
    rig.List("read");
    rig.List("write");

    REQUIRE(rig.obj.LineCount() == 1);
    CHECK(rig.obj.LineAt(0) == "60");
    CHECK(rig.text.seen.empty());
    CHECK(rig.lines.seen.empty());
    CHECK(rig.file.seen.empty());
  }

  TEST_CASE("textfile: a write then a read round-trips the contents exactly (#687)") {
    // The acceptance criterion, end to end through a real patcher and a real file
    // on disk: one stored line per line of the file, and what comes back is what
    // went out.
    const std::string path = TempFile("yse_textfile_roundtrip_687.txt");

    Recorder text;
    Recorder count;
    Recorder file;
    YSE::pHandle textHandle(&text);
    YSE::pHandle countHandle(&count);
    YSE::pHandle fileHandle(&file);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* tf = p.CreateObject(YSE::OBJ::G_TEXTFILE, "");
    REQUIRE(tf != nullptr);
    p.Connect(tf, 0, &textHandle, 0);
    p.Connect(tf, 1, &countHandle, 0);
    p.Connect(tf, 2, &fileHandle, 0);

    tf->SetListData(0, "60 100");
    tf->SetListData(0, "cr");
    tf->SetListData(0, "hello there");
    tf->SetListData(0, "cr");
    tf->SetListData(0, "write " + path);
    SettleFiles(p);

    // Plain text, one line per stored line. Every line is closed, so every one
    // carries its newline.
    CHECK(ReadWholeFile(path) == "60 100\nhello there\n");
    // Max has no outlet for a finished write and neither does this.
    CHECK(file.seen.empty());

    tf->SetListData(0, "clear");
    text.reset();
    tf->SetListData(0, "dump");
    CHECK(text.seen.empty());

    tf->SetListData(0, "read " + path);
    SettleFiles(p);
    // The file outlet fires once, and only after the contents are in place.
    REQUIRE(file.seen.size() == 1);
    CHECK(file.seen[0] == "!");

    text.reset();
    count.reset();
    tf->SetListData(0, "query");
    tf->SetListData(0, "dump");
    REQUIRE(count.seen.size() == 1);
    CHECK(count.seen[0] == "i2");
    REQUIRE(text.seen.size() == 2);
    CHECK(text.seen[0] == "s60 100");
    CHECK(text.seen[1] == "shello there");

    Remove(path);
  }

  TEST_CASE("textfile: read does nothing in the message handler (#687)") {
    // The reason the plumbing exists. A `read` may be dispatched on the audio
    // callback, so the handler must not open anything — which is observable: the
    // contents are still empty when the message returns, and only a rendered
    // block puts the file in them.
    const std::string path = TempFile("yse_textfile_deferred_687.txt");
    WriteWholeFile(path, "one\ntwo\n");

    Recorder count;
    Recorder file;
    YSE::pHandle countHandle(&count);
    YSE::pHandle fileHandle(&file);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* tf = p.CreateObject(YSE::OBJ::G_TEXTFILE, "");
    REQUIRE(tf != nullptr);
    p.Connect(tf, 1, &countHandle, 0);
    p.Connect(tf, 2, &fileHandle, 0);

    tf->SetListData(0, "read " + path);
    // Nothing yet: no line, no bang. The request is a claim on a slot and the
    // disk has not been touched on this thread.
    tf->SetListData(0, "query");
    REQUIRE(count.seen.size() == 1);
    CHECK(count.seen[0] == "i0");
    CHECK(file.seen.empty());
    REQUIRE(p.FileIO() != nullptr);
    CHECK(p.FileIO()->PendingCount() == 1);

    count.reset();
    SettleFiles(p);
    REQUIRE(file.seen.size() == 1);
    CHECK(file.seen[0] == "!");
    CHECK(p.FileIO()->PendingCount() == 0);

    tf->SetListData(0, "query");
    REQUIRE(count.seen.size() == 1);
    CHECK(count.seen[0] == "i2");

    Remove(path);
  }

  TEST_CASE("textfile: a read replaces what was held (#687)") {
    // Max's read loads a file into the object; it is not a merge. A patch that
    // reloads a cue sheet has to get the cue sheet, not the cue sheet plus
    // whatever it had been editing.
    const std::string path = TempFile("yse_textfile_replace_687.txt");
    WriteWholeFile(path, "fresh\n");

    Recorder text;
    YSE::pHandle textHandle(&text);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* tf = p.CreateObject(YSE::OBJ::G_TEXTFILE, "");
    REQUIRE(tf != nullptr);
    p.Connect(tf, 0, &textHandle, 0);

    tf->SetListData(0, "gone");
    tf->SetListData(0, "cr");
    tf->SetListData(0, "also gone");
    tf->SetListData(0, "read " + path);
    SettleFiles(p);

    text.reset();
    tf->SetListData(0, "dump");
    REQUIRE(text.seen.size() == 1);
    CHECK(text.seen[0] == "sfresh");

    Remove(path);
  }

  TEST_CASE("textfile: the open last line survives the round trip (#687)") {
    // Max's buffer is flat text where a `cr` is a *character*, so contents whose
    // last line is still taking appends end without a newline — and a file that
    // ends without one leaves its last line open again. This is the difference
    // between a round trip that is exact and one that is merely equal: an append
    // after reloading has to continue the line it was continuing before.
    const std::string path = TempFile("yse_textfile_openline_687.txt");

    Recorder text;
    YSE::pHandle textHandle(&text);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* tf = p.CreateObject(YSE::OBJ::G_TEXTFILE, "");
    REQUIRE(tf != nullptr);
    p.Connect(tf, 0, &textHandle, 0);

    tf->SetListData(0, "closed");
    tf->SetListData(0, "cr");
    tf->SetListData(0, "tail");
    tf->SetListData(0, "write " + path);
    SettleFiles(p);
    // No trailing newline: the second line is still open.
    CHECK(ReadWholeFile(path) == "closed\ntail");

    tf->SetListData(0, "clear");
    tf->SetListData(0, "read " + path);
    SettleFiles(p);

    // The append continues the reloaded line rather than starting a third.
    tf->SetListData(0, "more");
    text.reset();
    tf->SetListData(0, "dump");
    REQUIRE(text.seen.size() == 2);
    CHECK(text.seen[0] == "sclosed");
    CHECK(text.seen[1] == "stail more");

    Remove(path);
  }

  TEST_CASE("textfile: a file ending in a newline closes its last line (#687)") {
    // The other half of the same rule, and the one a hand-written file has: the
    // next append starts a new line rather than joining the last.
    const std::string path = TempFile("yse_textfile_closedline_687.txt");
    WriteWholeFile(path, "alpha\nbeta\n");

    Recorder text;
    YSE::pHandle textHandle(&text);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* tf = p.CreateObject(YSE::OBJ::G_TEXTFILE, "");
    REQUIRE(tf != nullptr);
    p.Connect(tf, 0, &textHandle, 0);

    tf->SetListData(0, "read " + path);
    SettleFiles(p);
    tf->SetListData(0, "gamma");

    text.reset();
    tf->SetListData(0, "dump");
    REQUIRE(text.seen.size() == 3);
    CHECK(text.seen[0] == "salpha");
    CHECK(text.seen[1] == "sbeta");
    CHECK(text.seen[2] == "sgamma");

    Remove(path);
  }

  TEST_CASE("textfile: a blank line in the file is a blank line in the contents (#687)") {
    // What two `cr`s produce has to be what "a\n\n" reads back as, or a write
    // then read would quietly close the gap a patch put there on purpose.
    const std::string path = TempFile("yse_textfile_blank_687.txt");

    Recorder text;
    Recorder count;
    YSE::pHandle textHandle(&text);
    YSE::pHandle countHandle(&count);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* tf = p.CreateObject(YSE::OBJ::G_TEXTFILE, "");
    REQUIRE(tf != nullptr);
    p.Connect(tf, 0, &textHandle, 0);
    p.Connect(tf, 1, &countHandle, 0);

    tf->SetListData(0, "a");
    tf->SetListData(0, "cr");
    tf->SetListData(0, "cr");
    tf->SetListData(0, "write " + path);
    SettleFiles(p);
    CHECK(ReadWholeFile(path) == "a\n\n");

    tf->SetListData(0, "clear");
    tf->SetListData(0, "read " + path);
    SettleFiles(p);

    count.reset();
    text.reset();
    tf->SetListData(0, "query");
    tf->SetListData(0, "dump");
    REQUIRE(count.seen.size() == 1);
    CHECK(count.seen[0] == "i2");
    REQUIRE(text.seen.size() == 2);
    CHECK(text.seen[0] == "sa");
    CHECK(text.seen[1] == "s");

    Remove(path);
  }

  TEST_CASE("textfile: a CRLF file loads the same lines (#687)") {
    // A text file written by another editor on Windows. The terminator's own
    // carriage return is dropped; one in the middle of a line is text.
    const std::string path = TempFile("yse_textfile_crlf_687.txt");
    WriteWholeFile(path, "one\r\ntwo\r\n");

    Recorder text;
    YSE::pHandle textHandle(&text);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* tf = p.CreateObject(YSE::OBJ::G_TEXTFILE, "");
    REQUIRE(tf != nullptr);
    p.Connect(tf, 0, &textHandle, 0);

    tf->SetListData(0, "read " + path);
    SettleFiles(p);

    text.reset();
    tf->SetListData(0, "dump");
    REQUIRE(text.seen.size() == 2);
    CHECK(text.seen[0] == "sone");
    CHECK(text.seen[1] == "stwo");

    // And the write side emits plain newlines whatever it read.
    tf->SetListData(0, "write " + path);
    SettleFiles(p);
    CHECK(ReadWholeFile(path) == "one\ntwo\n");

    Remove(path);
  }

  TEST_CASE("textfile: the bare forms reuse the last name given (#687)") {
    // Max's bare `read` / `write` open a file dialog, which a headless patcher
    // has no equivalent of — so they reuse the last name, which also has to be
    // remembered on a message path without allocating. Each half remembers its
    // own name, `.coll`'s rule: a patch that reads a template and writes a
    // result is reading and writing two different files.
    const std::string path = TempFile("yse_textfile_again_687.txt");

    Recorder count;
    YSE::pHandle countHandle(&count);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* tf = p.CreateObject(YSE::OBJ::G_TEXTFILE, "");
    REQUIRE(tf != nullptr);
    p.Connect(tf, 1, &countHandle, 0);

    tf->SetListData(0, "first");
    tf->SetListData(0, "cr");
    tf->SetListData(0, "write " + path);
    SettleFiles(p);
    CHECK(ReadWholeFile(path) == "first\n");

    // Same name, no argument.
    tf->SetListData(0, "second");
    tf->SetListData(0, "cr");
    tf->SetListData(0, "write");
    SettleFiles(p);
    CHECK(ReadWholeFile(path) == "first\nsecond\n");

    // A bare `read` before any named one has nothing to reuse — the write's name
    // is not the read's.
    tf->SetListData(0, "clear");
    tf->SetListData(0, "read");
    REQUIRE(p.FileIO() != nullptr);
    CHECK(p.FileIO()->PendingCount() == 0);

    tf->SetListData(0, "read " + path);
    SettleFiles(p);
    count.reset();
    tf->SetListData(0, "query");
    REQUIRE(count.seen.size() == 1);
    CHECK(count.seen[0] == "i2");

    // And now the bare form has one.
    tf->SetListData(0, "clear");
    tf->SetListData(0, "read");
    SettleFiles(p);
    count.reset();
    tf->SetListData(0, "query");
    REQUIRE(count.seen.size() == 1);
    CHECK(count.seen[0] == "i2");

    Remove(path);
  }

  TEST_CASE("textfile: a bare read with no name and no argument does nothing (#687)") {
    // Nothing named and no dialog to ask with: the honest behaviour is silence
    // rather than a guess at a filename.
    Recorder file;
    YSE::pHandle fileHandle(&file);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* tf = p.CreateObject(YSE::OBJ::G_TEXTFILE, "");
    REQUIRE(tf != nullptr);
    p.Connect(tf, 2, &fileHandle, 0);

    tf->SetListData(0, "read");
    tf->SetListData(0, "write");
    REQUIRE(p.FileIO() != nullptr);
    CHECK(p.FileIO()->PendingCount() == 0);
    SettleFiles(p);
    CHECK(file.seen.empty());
  }

  TEST_CASE("textfile: a read of a missing file leaves the contents alone (#687)") {
    // A failure is only discoverable on the background pool, so it arrives as a
    // completion rather than as a refusal — and it must not fire the outlet a
    // patch uses to mean "the file is loaded".
    const std::string path = TempFile("yse_textfile_no_such_file_687.txt");

    Recorder text;
    Recorder file;
    YSE::pHandle textHandle(&text);
    YSE::pHandle fileHandle(&file);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* tf = p.CreateObject(YSE::OBJ::G_TEXTFILE, "");
    REQUIRE(tf != nullptr);
    p.Connect(tf, 0, &textHandle, 0);
    p.Connect(tf, 2, &fileHandle, 0);

    tf->SetListData(0, "kept");
    tf->SetListData(0, "read " + path);
    SettleFiles(p);

    CHECK(file.seen.empty());
    text.reset();
    tf->SetListData(0, "dump");
    REQUIRE(text.seen.size() == 1);
    CHECK(text.seen[0] == "skept");
  }

  TEST_CASE("textfile: the filename argument is read when the object is built (#687)") {
    // Max's "names a text file to be read in when the object is loaded", which is
    // the whole point of holding the argument. The read is the same deferred
    // request a `read` message makes, so the lines arrive with the patcher's next
    // block and the file outlet bangs then.
    const std::string path = TempFile("yse_textfile_argument_687.txt");
    WriteWholeFile(path, "from the argument\nsecond line\n");

    Recorder text;
    Recorder file;
    YSE::pHandle textHandle(&text);
    YSE::pHandle fileHandle(&file);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* tf = p.CreateObject(YSE::OBJ::G_TEXTFILE, path);
    REQUIRE(tf != nullptr);
    p.Connect(tf, 0, &textHandle, 0);
    p.Connect(tf, 2, &fileHandle, 0);

    // Claimed at construction, delivered by a block — nothing was opened on the
    // control thread either.
    REQUIRE(p.FileIO() != nullptr);
    CHECK(p.FileIO()->PendingCount() == 1);

    SettleFiles(p);
    REQUIRE(file.seen.size() == 1);
    CHECK(file.seen[0] == "!");

    text.reset();
    tf->SetListData(0, "dump");
    REQUIRE(text.seen.size() == 2);
    CHECK(text.seen[0] == "sfrom the argument");
    CHECK(text.seen[1] == "ssecond line");

    // And it seeded the name, so a bare write saves back over the same file.
    tf->SetListData(0, "third");
    tf->SetListData(0, "cr");
    tf->SetListData(0, "write");
    SettleFiles(p);
    CHECK(ReadWholeFile(path) == "from the argument\nsecond line\nthird\n");

    Remove(path);
  }

  TEST_CASE("textfile: a filename argument naming nothing leaves the object empty (#687)") {
    // A patch saved with a name whose file has since gone. The read fails on the
    // pool, so the object is simply the empty one it would have been.
    const std::string path = TempFile("yse_textfile_argument_missing_687.txt");

    Recorder count;
    Recorder file;
    YSE::pHandle countHandle(&count);
    YSE::pHandle fileHandle(&file);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* tf = p.CreateObject(YSE::OBJ::G_TEXTFILE, path);
    REQUIRE(tf != nullptr);
    p.Connect(tf, 1, &countHandle, 0);
    p.Connect(tf, 2, &fileHandle, 0);

    SettleFiles(p);
    CHECK(file.seen.empty());

    tf->SetListData(0, "query");
    REQUIRE(count.seen.size() == 1);
    CHECK(count.seen[0] == "i0");
  }

  TEST_CASE("textfile: a file with more lines than the table holds keeps the first 256 (#687)") {
    // The bound cannot grow without allocating on whichever thread the message
    // arrived on, so the overflow is dropped — the rule a `cr` past the table
    // already follows.
    const std::string path = TempFile("yse_textfile_overflow_687.txt");
    {
      std::string contents;
      for (int i = 0; i < 300; i++) {
        contents += "line" + std::to_string(i) + "\n";
      }
      WriteWholeFile(path, contents);
    }

    Recorder text;
    Recorder count;
    YSE::pHandle textHandle(&text);
    YSE::pHandle countHandle(&count);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* tf = p.CreateObject(YSE::OBJ::G_TEXTFILE, "");
    REQUIRE(tf != nullptr);
    p.Connect(tf, 0, &textHandle, 0);
    p.Connect(tf, 1, &countHandle, 0);

    tf->SetListData(0, "read " + path);
    SettleFiles(p);

    tf->SetListData(0, "query");
    REQUIRE(count.seen.size() == 1);
    CHECK(count.seen[0] == "i" + std::to_string(gTextfile::MAX_LINES));

    // The first ones, in order — not the last ones.
    tf->SetListData(0, "dump");
    REQUIRE(text.seen.size() == gTextfile::MAX_LINES);
    CHECK(text.seen[0] == "sline0");
    CHECK(text.seen[gTextfile::MAX_LINES - 1] ==
          "sline" + std::to_string(gTextfile::MAX_LINES - 1));

    Remove(path);
  }

  TEST_CASE("textfile: an over-long line is skipped and the rest of the file loads (#687)") {
    // Refused whole rather than truncated, because half a line is a different
    // line — and only that line, so a single bad record does not cost the file.
    const std::string path = TempFile("yse_textfile_longline_687.txt");
    WriteWholeFile(path, "short\n" + std::string(gTextfile::LINE_CAPACITY + 1, 'x') + "\nafter\n");

    Recorder text;
    YSE::pHandle textHandle(&text);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* tf = p.CreateObject(YSE::OBJ::G_TEXTFILE, "");
    REQUIRE(tf != nullptr);
    p.Connect(tf, 0, &textHandle, 0);

    tf->SetListData(0, "read " + path);
    SettleFiles(p);

    text.reset();
    tf->SetListData(0, "dump");
    REQUIRE(text.seen.size() == 2);
    CHECK(text.seen[0] == "sshort");
    CHECK(text.seen[1] == "safter");

    Remove(path);
  }

  TEST_CASE("textfile: a line filled to capacity still round-trips (#687)") {
    // The boundary the test above sits one character past.
    const std::string path = TempFile("yse_textfile_exactline_687.txt");
    const std::string full(gTextfile::LINE_CAPACITY, 'y');
    WriteWholeFile(path, full + "\n");

    Recorder text;
    YSE::pHandle textHandle(&text);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* tf = p.CreateObject(YSE::OBJ::G_TEXTFILE, "");
    REQUIRE(tf != nullptr);
    p.Connect(tf, 0, &textHandle, 0);

    tf->SetListData(0, "read " + path);
    SettleFiles(p);

    text.reset();
    tf->SetListData(0, "dump");
    REQUIRE(text.seen.size() == 1);
    CHECK(text.seen[0] == "s" + full);

    Remove(path);
  }

  TEST_CASE("textfile: deleting the object with a read in flight is safe (#687)") {
    // The lifetime guarantee the scheduler gives by construction: the background
    // job holds no pObject, and delivery re-resolves the target against the
    // block's pinned snapshot, so a live edit that retires the object between the
    // `read` and the block that would deliver it drops the result and frees the
    // slot.
    const std::string path = TempFile("yse_textfile_deleted_687.txt");
    WriteWholeFile(path, "gone\n");

    patcherImplementation p(1, nullptr);
    YSE::pHandle* tf = p.CreateObject(YSE::OBJ::G_TEXTFILE, "");
    REQUIRE(tf != nullptr);
    YSE::PATCHER::fileScheduler* io = p.FileIO();
    REQUIRE(io != nullptr);

    tf->SetListData(0, "read " + path);
    CHECK(io->PendingCount() == 1);

    p.DeleteObject(tf);

    io->WaitIdle();
    p.Calculate(YSE::T_DSP);
    CHECK(io->PendingCount() == 0);

    Remove(path);
  }

  TEST_CASE("textfile: a real graph builds lines, writes them and reads them back (#687)") {
    // The user-visible flow, driven the way a patch would drive it: values arrive
    // through another object, a .trigger ends the line and asks for the write,
    // and the file outlet's bang is what tells the patch the reload has landed —
    // wired into a .table so the reloaded text really is a working message again.
    const std::string path = TempFile("yse_textfile_graph_687.txt");

    Recorder out;
    Recorder file;
    YSE::pHandle outHandle(&out);
    YSE::pHandle fileHandle(&file);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* add = p.CreateObject(YSE::OBJ::G_ADD, "12");
    YSE::pHandle* tf = p.CreateObject(YSE::OBJ::G_TEXTFILE, "");
    YSE::pHandle* table = p.CreateObject(YSE::OBJ::G_TABLE, "8");
    REQUIRE(add != nullptr);
    REQUIRE(tf != nullptr);
    REQUIRE(table != nullptr);
    p.Connect(add, 0, tf, 0);
    p.Connect(tf, 0, table, 0);
    p.Connect(tf, 2, &fileHandle, 0);
    p.Connect(table, 0, &outHandle, 0);

    // `.table`'s set is "set <start> <values...>", so the line is that argument
    // list, built here by a real object on the way in.
    tf->SetListData(0, "0");
    add->SetIntData(0, 11);
    add->SetIntData(0, 21);
    tf->SetListData(0, "cr");
    tf->SetListData(0, "write " + path);
    SettleFiles(p);
    CHECK(ReadWholeFile(path) == "0 23. 33.\n");

    tf->SetListData(0, "clear");
    tf->SetListData(0, "read " + path);
    SettleFiles(p);
    REQUIRE(file.seen.size() == 1);
    CHECK(file.seen[0] == "!");

    // The reloaded line loads the table through its `set`, which is the proof
    // that what came off the disk is a message and not just characters.
    tf->SetListData(0, "line 1");
    table->SetIntData(0, 0);
    table->SetIntData(0, 1);
    REQUIRE(out.seen.size() == 2);
    CHECK(out.seen[0] == "i23");
    CHECK(out.seen[1] == "i33");

    Remove(path);
  }

} // TEST_SUITE
