// Tests for .qlist (issue #500) — the patcher's cue list: a stored sequence of
// messages played back in time.
//
// This is the first store in the family that *plays*, so the suite has two
// halves and they are not interchangeable:
//
//   - **standalone cases** pin the grammar. What a cue line is, where it gets
//     split, what `next` / `fwd` / `rewind` / `set` / `append` / `insert` /
//     `tempo` do, and what leaves outlet 0 in what kind. A standalone rig is
//     right here because a refused store and a store the patcher never
//     delivered look identical through a patcher.
//
//   - **patcher cases** pin the clock and the remote half, which cannot exist
//     standalone at all: automatic playback runs on the patcher's deferred
//     scheduler, so only a real patcherImplementation with real Calculate
//     blocks can show that a 100 ms cue actually waits 100 ms, that `tempo 2`
//     halves it, that `stop` cancels it, and that a symbol line reaches a real
//     `.r`. Deadlines are asserted through messageScheduler::BlocksForMillis at
//     the live SAMPLERATE rather than hard-coded block counts, so the suite is
//     valid at any negotiated rate.
//
// Six rules carry the file, each of them something a plausible implementation
// gets backwards without ever crashing:
//
//   - **a line of numbers followed by a message is two lines.** Max's own
//     words, and the reason the cursor can be a single index. An object that
//     stored `500 foo 1` as one entry passes every count test and then cannot
//     say what `next` should stop on.
//   - **a numeric line does two jobs.** It goes out outlet 0 *and* its leading
//     number is the wait. Drop either half and one of the two playback modes
//     silently stops working — outputting-only breaks `bang`, waiting-only
//     breaks the classic outlet-into-a-delay idiom.
//   - **`next` walks past symbol lines and stops on a numeric one.** Not "one
//     entry per next": a run of symbol lines is one `next`.
//   - **`bang` rewinds.** Max: "it begins sending messages from the first
//     line." A bang that resumed would make `stop` + `bang` a continue.
//   - **tempo divides.** 2 is twice as fast, i.e. half the wait. The sign of
//     this is a coin flip if you do not read the reference.
//   - **the contents are saved.** Max: "the qlist object saves its cue-list
//     with the patcher." This is where .qlist sides with .coll against
//     .textfile, whose contents live in a file — the family rule is save iff
//     Max gives the object a save flag, and this one has the plainest.
//   - **the cue list round-trips through a file, and never on the message
//     path.** Issue #689. A `read` arrives on whichever thread dispatched it, so
//     the proof that matters is not only that the cues come back but that
//     *nothing happens in the handler*: the list is untouched until the patcher
//     renders a block. Asserted through a real patcherImplementation and a real
//     file on disk, because both halves — the background job and the completion
//     delivered into a dispatch frame — only exist there.
//
// No audio device required.

#include <doctest/doctest.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#include "headers/constants.hpp"
#include "patcher/genericObjects/gQlist.h"
#include "patcher/inlet.h"
#include "patcher/io/fileScheduler.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"
#include "patcher/time/messageScheduler.h"

using YSE::PATCHER::gQlist;
using YSE::PATCHER::messageScheduler;
using YSE::PATCHER::patcherImplementation;

namespace {

  // Records every value it receives, in order and with its kind. Both matter
  // here: a cue list is a *sequence*, and a cue holding "60" has to arrive as
  // an int rather than as a one-element list.
  struct Recorder : YSE::PATCHER::pObject {
    // "i60", "f60.50", "s60 100", "!" for a bang — one string per send.
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
      return "qlist_recorder";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

    void reset() {
      seen.clear();
    }
  };

  // A standalone .qlist with a recorder on each outlet.
  struct Rig {
    Recorder data;
    Recorder end;
    Recorder file;
    gQlist obj;

    Rig() {
      obj.ConnectOutlet(data.GetInlet(0), 0);
      obj.ConnectOutlet(end.GetInlet(0), 1);
      obj.ConnectOutlet(file.GetInlet(0), 2);
    }

    void List(const std::string& message) {
      obj.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    void Bang() {
      obj.GetInlet(0)->SetBang(YSE::T_GUI);
    }
    void reset() {
      data.reset();
      end.reset();
      file.reset();
    }
  };

  // The whole cue list as one string, so a split assertion reads as the list it
  // produced rather than as a pile of index lookups.
  std::string Contents(const gQlist& obj) {
    std::string all;
    for (std::size_t i = 0; i < obj.Count(); i++) {
      if (i > 0) all += " | ";
      all += obj.EntryAt(i);
    }
    return all;
  }

  std::string Joined(const std::vector<std::string>& seen) {
    std::string all;
    for (std::size_t i = 0; i < seen.size(); i++) {
      if (i > 0) all += ",";
      all += seen[i];
    }
    return all;
  }

  // ─── file helpers (issue #689) ────────────────────────────────────────────

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

  // ─── the object exists ──────────────────────────────────────────────────────

  TEST_CASE("qlist: creatable through the registry (#500)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_QLIST);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".qlist");
  }

  TEST_CASE("qlist: listed by pRegistry::AllNames (#500)") {
    auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".qlist")) != names.end());
  }

  TEST_CASE("qlist: one inlet, three outlets (#500, #689)") {
    // Max's three: the numeric cues, the end bang, and the bang for a file that
    // has been read. The third arrived with #689 and was *appended* — which the
    // family's rule demands and which here also happens to be Max's own
    // position for it, so no patch saved against the two-outlet object has to
    // be rewired.
    gQlist obj;
    CHECK(obj.NumInputs() == 1);
    CHECK(obj.NumOutputs() == 3);
    CHECK(obj.GetCategory() == YSE::PATCHER::pCategory::TIME);
  }

  TEST_CASE("qlist: a fresh object is empty and not playing (#500)") {
    gQlist obj;
    CHECK(obj.Count() == 0);
    CHECK(obj.Position() == 0);
    CHECK(obj.Tempo() == doctest::Approx(1.f));
    CHECK_FALSE(obj.IsPlaying());
  }

  // ─── writing the list ───────────────────────────────────────────────────────

  TEST_CASE("qlist: 'set' stores cue lines separated by semicolons (#500)") {
    // Max: "each line of the cue-list is a message ending in a semicolon."
    Rig rig;
    rig.List("set foo 1; 500; bar 2");
    CHECK(Contents(rig.obj) == "foo 1 | 500 | bar 2");
  }

  TEST_CASE("qlist: a line of numbers then a message is stored as two lines (#500)") {
    // Max: "lines which begin with a numerical value (or values) but have a
    // message and arguments after the number(s), are treated as two separate
    // lines". Taken literally, at store time — which is what lets the cursor be
    // a single index into a flat table.
    Rig rig;
    rig.List("set 500 foo 1 2");
    CHECK(Contents(rig.obj) == "500 | foo 1 2");
  }

  TEST_CASE("qlist: the whole leading run of numbers splits off, not just the first (#500)") {
    // "a numerical value (or list of numerical values)".
    Rig rig;
    rig.List("set 500 250 60.5 foo bar");
    CHECK(Contents(rig.obj) == "500 250 60.5 | foo bar");
  }

  TEST_CASE("qlist: an all-numeric line and an all-symbol line stay whole (#500)") {
    Rig rig;
    rig.List("set 60 100; foo 1 2");
    CHECK(Contents(rig.obj) == "60 100 | foo 1 2");
  }

  TEST_CASE("qlist: 'clear' empties the list, and a bare 'set' is the same thing (#500)") {
    // Max: "sending a set message with no arguments is the same as sending a
    // clear message."
    Rig rig;
    rig.List("set 1; 2; 3");
    REQUIRE(rig.obj.Count() == 3);

    rig.List("clear");
    CHECK(rig.obj.Count() == 0);
    CHECK(rig.obj.Position() == 0);

    rig.List("set 1; 2");
    REQUIRE(rig.obj.Count() == 2);
    rig.List("set");
    CHECK(rig.obj.Count() == 0);
  }

  TEST_CASE("qlist: 'set' replaces rather than adds (#500)") {
    // "It completely clears any previous cue-list contents."
    Rig rig;
    rig.List("set 1; 2");
    rig.List("set 9");
    CHECK(Contents(rig.obj) == "9");
  }

  TEST_CASE("qlist: 'insert' appends a new entry — Max's own name for it (#500)") {
    // Max: "the word insert followed by any arguments will append those
    // arguments to the qlist object's cue-list as a new entry in the list." Its
    // name is a Max idiosyncrasy and it is reproduced rather than corrected, so
    // a patch brought across builds the same list.
    Rig rig;
    rig.List("set foo 1");
    rig.List("insert bar 2");
    CHECK(Contents(rig.obj) == "foo 1 | bar 2");
  }

  TEST_CASE("qlist: 'append' glues onto the last entry (#500)") {
    // Max: "append those arguments to the last entry".
    Rig rig;
    rig.List("set foo 1");
    rig.List("append 2 3");
    CHECK(Contents(rig.obj) == "foo 1 2 3");
  }

  TEST_CASE("qlist: 'append' onto a numeric entry re-splits it (#500)") {
    // The corner that makes append more than string concatenation: numbers
    // followed by a message are two lines however they came to be written that
    // way, so the joined line goes back through the same splitter.
    Rig rig;
    rig.List("set 500");
    REQUIRE(rig.obj.Count() == 1);
    rig.List("append foo 1");
    CHECK(Contents(rig.obj) == "500 | foo 1");
  }

  TEST_CASE("qlist: 'append' on an empty list is an insert (#500)") {
    Rig rig;
    rig.List("append foo 1");
    CHECK(Contents(rig.obj) == "foo 1");
  }

  TEST_CASE("qlist: 'insert' may itself carry semicolons (#500)") {
    Rig rig;
    rig.List("insert foo 1; 500; bar");
    CHECK(Contents(rig.obj) == "foo 1 | 500 | bar");
  }

  // ─── stepping it by hand ────────────────────────────────────────────────────

  TEST_CASE("qlist: 'next' sends symbol lines past and stops on a numeric one (#500)") {
    // Max: "it will remotely send all lines beginning with a symbol, and stop
    // after it encounters and outputs a line beginning with a numerical value."
    // A run of symbol lines is *one* next, not one next each — the rule an
    // "advance one entry" implementation gets wrong.
    Rig rig;
    rig.List("set foo 1; bar 2; 500; baz 3");
    rig.List("next");
    CHECK(Joined(rig.data.seen) == "i500");
    CHECK(rig.obj.Position() == 3);
  }

  TEST_CASE("qlist: 'next' stops on each numeric line in turn (#500)") {
    Rig rig;
    rig.List("set 100; 200; 300");
    rig.List("next");
    rig.List("next");
    rig.List("next");
    CHECK(Joined(rig.data.seen) == "i100,i200,i300");
  }

  TEST_CASE("qlist: 'next 1' skips symbol lines instead of sending them (#500)") {
    // "If the word next is followed by a non-zero argument, it will ignore
    // lines beginning with symbols and only output the next line beginning with
    // a numerical value." Standalone, so nothing could receive them anyway —
    // what this pins is that the cursor still walks past them.
    Rig rig;
    rig.List("set foo 1; 500");
    rig.List("next 1");
    CHECK(Joined(rig.data.seen) == "i500");
    CHECK(rig.obj.Position() == 2);
  }

  TEST_CASE("qlist: 'fwd n' outputs the next n numeric lines (#500)") {
    // Max: "fwd 2 will output the next two lines in the cue-list which begin
    // with numerical values. Lines beginning with symbols will be ignored."
    Rig rig;
    rig.List("set 100; foo; 200; bar; 300");
    rig.List("fwd 2");
    CHECK(Joined(rig.data.seen) == "i100,i200");
  }

  TEST_CASE("qlist: a bare 'fwd' moves nothing (#500)") {
    // Max documents fwd as "followed by a number"; without one there is no
    // distance to travel, so it is consumed rather than guessed at.
    Rig rig;
    rig.List("set 100; 200");
    rig.List("fwd");
    CHECK(rig.data.seen.empty());
    CHECK(rig.obj.Position() == 0);
  }

  TEST_CASE("qlist: 'rewind' puts the cursor back at the first line (#500)") {
    Rig rig;
    rig.List("set 100; 200");
    rig.List("next");
    rig.List("next");
    REQUIRE(rig.obj.Position() == 2);

    rig.reset();
    rig.List("rewind");
    CHECK(rig.obj.Position() == 0);
    rig.List("next");
    CHECK(Joined(rig.data.seen) == "i100");
  }

  // ─── the end outlet ─────────────────────────────────────────────────────────

  TEST_CASE("qlist: running off the end bangs outlet 1 (#500)") {
    // Max: "a bang is sent when a cue list has reached the end, and there are no
    // more lines to send or output."
    Rig rig;
    rig.List("set 100");
    rig.List("next");
    CHECK(rig.end.seen.empty());

    rig.List("next");
    CHECK(Joined(rig.end.seen) == "!");
    CHECK(Joined(rig.data.seen) == "i100");
  }

  TEST_CASE("qlist: an empty list bangs the end outlet at once (#500)") {
    // It has reached its end before it starts.
    Rig rig;
    rig.List("next");
    CHECK(Joined(rig.end.seen) == "!");
  }

  TEST_CASE("qlist: 'fwd' past the end bangs once, not once per remaining step (#500)") {
    Rig rig;
    rig.List("set 100");
    rig.List("fwd 5");
    CHECK(Joined(rig.data.seen) == "i100");
    CHECK(Joined(rig.end.seen) == "!");
  }

  // ─── what leaves outlet 0 ───────────────────────────────────────────────────

  TEST_CASE("qlist: a numeric cue leaves in the kind it is (#500)") {
    // `.route`'s rule, shared with .coll and .textfile: "60" as the int 60,
    // "60.5" as that float, and a longer line as a list. A one-element list
    // where an int belongs silently does nothing at a downstream .+.
    Rig rig;
    rig.List("set 60; 60.5; 60 100");
    rig.List("next");
    rig.List("next");
    rig.List("next");
    CHECK(Joined(rig.data.seen) == "i60,f60.50,s60 100");
  }

  TEST_CASE("qlist: a negative cue is still a cue (#500)") {
    Rig rig;
    rig.List("set -20");
    rig.List("next");
    CHECK(Joined(rig.data.seen) == "i-20");
  }

  // ─── tempo ──────────────────────────────────────────────────────────────────

  TEST_CASE("qlist: 'tempo' is stored, and a non-positive one is refused (#500)") {
    // A tempo of zero or less cannot scale a duration into anything a clock can
    // wait for, so the previous one is kept rather than stored and divided by.
    Rig rig;
    CHECK(rig.obj.Tempo() == doctest::Approx(1.f));

    rig.List("tempo 2");
    CHECK(rig.obj.Tempo() == doctest::Approx(2.f));

    rig.List("tempo 0");
    CHECK(rig.obj.Tempo() == doctest::Approx(2.f));

    rig.List("tempo -1");
    CHECK(rig.obj.Tempo() == doctest::Approx(2.f));

    rig.List("tempo 0.5");
    CHECK(rig.obj.Tempo() == doctest::Approx(0.5f));

    rig.List("tempo notanumber");
    CHECK(rig.obj.Tempo() == doctest::Approx(0.5f));
  }

  // ─── the inert and the unknown ──────────────────────────────────────────────

  TEST_CASE("qlist: 'open' and 'wclose' are consumed and do nothing (#500)") {
    // The patcher has no editing window. They are consumed rather than stored,
    // because Max dispatches on the selector and so cannot store them either.
    Rig rig;
    rig.List("set 100");
    for (const char* word : {"open", "wclose"}) {
      rig.List(word);
    }
    CHECK(Contents(rig.obj) == "100");
    CHECK(rig.data.seen.empty());
    CHECK(rig.end.seen.empty());
  }

  TEST_CASE("qlist: a message that is not a command does nothing (#500)") {
    // A command inlet: cue text is written with set / append / insert, never by
    // being sent bare, which is Max. Storing it would give the object two
    // spellings of insert and make a stray message corrupt a cue list.
    Rig rig;
    rig.List("set 100");
    rig.List("hello world");
    rig.List("60 100");
    CHECK(Contents(rig.obj) == "100");
    CHECK(rig.data.seen.empty());
  }

  TEST_CASE("qlist: Calculate sends nothing (#500)") {
    // An emitting Calculate() would restart the sequence on every DSP tick.
    Rig rig;
    rig.List("set 100; foo");
    for (int i = 0; i < 8; i++)
      rig.obj.Calculate(YSE::T_DSP);
    CHECK(rig.data.seen.empty());
    CHECK(rig.end.seen.empty());
    CHECK(rig.obj.Position() == 0);
  }

  // ─── bounds ─────────────────────────────────────────────────────────────────

  TEST_CASE("qlist: a cue line past the capacity is refused whole (#500)") {
    // Refused rather than truncated: half a cue is a different cue, and the
    // inlet may be the audio thread.
    Rig rig;
    rig.List("set " + std::string(gQlist::ENTRY_CAPACITY + 4, 'x'));
    CHECK(rig.obj.Count() == 0);

    rig.List("set " + std::string(gQlist::ENTRY_CAPACITY, 'x'));
    CHECK(rig.obj.Count() == 1);
  }

  TEST_CASE("qlist: cue lines past the table size are refused (#500)") {
    Rig rig;
    std::string message = "set 1";
    for (std::size_t i = 1; i < gQlist::MAX_ENTRIES + 20; i++)
      message += "; 1";
    rig.List(message);
    CHECK(rig.obj.Count() == gQlist::MAX_ENTRIES);
  }

  TEST_CASE("qlist: a split line at the table boundary is refused whole (#500)") {
    // Both halves or neither: leaving the numbers behind without the message
    // they timed is a different cue list rather than a shorter one.
    Rig rig;
    std::string message = "set foo";
    // One slot short of full, so a two-entry line has room for exactly half
    // of itself — which is the case that has to be refused.
    for (std::size_t i = 1; i < gQlist::MAX_ENTRIES - 1; i++)
      message += "; foo";
    rig.List(message);
    REQUIRE(rig.obj.Count() == gQlist::MAX_ENTRIES - 1);

    rig.List("insert 500 bar");
    CHECK(rig.obj.Count() == gQlist::MAX_ENTRIES - 1);

    // A line that needs only the one free slot still lands.
    rig.List("insert 500");
    CHECK(rig.obj.Count() == gQlist::MAX_ENTRIES);
  }

  // ─── persistence ────────────────────────────────────────────────────────────

  TEST_CASE("qlist: the cue list survives a DumpJSON / ParseJSON round trip (#500)") {
    // Max: "the qlist object saves its cue-list with the patcher." This is the
    // family rule — save iff Max gives the object a save flag — landing on the
    // opposite side from .textfile, whose contents live in a file.
    Recorder data;
    YSE::pHandle dataHandle(&data);

    YSE::patcher src;
    src.create(2);
    YSE::pHandle* qlist = src.CreateObject(YSE::OBJ::G_QLIST);
    REQUIRE(qlist != nullptr);
    qlist->SetListData(0, "set 100; foo 1; 250 bar 2");

    const std::string json = src.DumpJSON();
    CHECK(json.find(".qlist") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".qlist");
    loaded.Connect(copy, 0, &dataHandle, 0);

    // Read back through the outlet rather than through an accessor: what has to
    // survive is the cue list a patch can play, not a member variable. The
    // stored line "250 bar 2" was split when it was stored, so the reloaded
    // list stops on 250 before ever reaching bar.
    copy->SetListData(0, "next");
    CHECK(Joined(data.seen) == "i100");
    data.reset();
    copy->SetListData(0, "next");
    CHECK(Joined(data.seen) == "i250");
  }

  TEST_CASE("qlist: an empty cue list writes no state key (#500)") {
    // .coll's rule: a per-object state key is written only when the object has
    // state of its own, so every other object's serialised form is untouched.
    YSE::patcher p;
    p.create(2);
    REQUIRE(p.CreateObject(YSE::OBJ::G_QLIST) != nullptr);
    CHECK(p.DumpJSON().find("\"state\"") == std::string::npos);
  }

  TEST_CASE("qlist: the cursor is run-time state and does not survive a save (#500)") {
    // `.coll`'s pointer rule. A reloaded patch that resumed a sequence
    // mid-flight by itself would be a surprise rather than a feature. Asserted
    // through the outlet, which is where it is visible: the reloaded object's
    // first `next` has to give the *first* cue, not the second.
    Recorder data;
    YSE::pHandle dataHandle(&data);

    YSE::patcher src;
    src.create(2);
    YSE::pHandle* qlist = src.CreateObject(YSE::OBJ::G_QLIST);
    REQUIRE(qlist != nullptr);
    qlist->SetListData(0, "set 100; 200");
    qlist->SetListData(0, "next");

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(src.DumpJSON());
    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    loaded.Connect(copy, 0, &dataHandle, 0);

    copy->SetListData(0, "next");
    CHECK(Joined(data.seen) == "i100");
  }

  TEST_CASE("qlist: the tempo is run-time state and does not survive a save (#500)") {
    // Max documents no stored tempo — only the cue list is saved — so an object
    // that has only ever been given one still writes no state key at all.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* qlist = p.CreateObject(YSE::OBJ::G_QLIST);
    REQUIRE(qlist != nullptr);
    qlist->SetListData(0, "tempo 4");
    CHECK(p.DumpJSON().find("\"state\"") == std::string::npos);
  }

  // ─── the clock, end to end through a real patcher ───────────────────────────

  TEST_CASE("qlist: a bang plays the list, waiting out each numeric cue (#500)") {
    // The property the object exists for, and one no standalone test can show:
    // automatic playback runs on the patcher's deferred scheduler, so the wait
    // is real Calculate blocks. Max: "it begins sending messages from the first
    // line, until a line begins with a number, at which point qlist will use
    // that number as a delay time in milliseconds before continuing."
    patcherImplementation p(1, nullptr);
    YSE::pHandle* qlist = p.CreateObject(YSE::OBJ::G_QLIST, "");
    REQUIRE(qlist != nullptr);

    Recorder data;
    Recorder end;
    YSE::pHandle dataHandle(&data);
    YSE::pHandle endHandle(&end);
    p.Connect(qlist, 0, &dataHandle, 0);
    p.Connect(qlist, 1, &endHandle, 0);

    qlist->SetListData(0, "set 100; 200");
    qlist->SetBang(0);

    // The first cue goes out in the arming dispatch itself; the second is
    // waiting on the clock.
    CHECK(Joined(data.seen) == "i100");
    CHECK(p.Scheduler()->PendingCount() == 1);

    const std::uint64_t first = messageScheduler::BlocksForMillis(100);
    for (std::uint64_t block = 1; block < first; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(Joined(data.seen) == "i100");

    p.Calculate(YSE::T_DSP);
    CHECK(Joined(data.seen) == "i100,i200");
    CHECK(end.seen.empty());

    // And the end bang, one wait later.
    const std::uint64_t second = messageScheduler::BlocksForMillis(200);
    for (std::uint64_t block = 1; block <= second; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(Joined(end.seen) == "!");
    // Nothing left armed: the walk finished rather than stalling on a step
    // whose delivery never came.
    CHECK(p.Scheduler()->PendingCount() == 0);
  }

  TEST_CASE("qlist: 'tempo 2' halves every wait (#500)") {
    // Max: "a tempo of 0.5 plays back the cue list at half speed, whereas a
    // tempo of 2. plays it back twice as fast" — so the tempo divides. The sign
    // of this is a coin flip if you do not read the reference, and the test is
    // what pins it.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* qlist = p.CreateObject(YSE::OBJ::G_QLIST, "");
    REQUIRE(qlist != nullptr);

    Recorder data;
    YSE::pHandle dataHandle(&data);
    p.Connect(qlist, 0, &dataHandle, 0);

    qlist->SetListData(0, "set 200; 1");
    qlist->SetListData(0, "tempo 2");
    qlist->SetBang(0);
    REQUIRE(Joined(data.seen) == "i200");

    // 200 ms at double speed is 100 ms of clock.
    const std::uint64_t scaled = messageScheduler::BlocksForMillis(100);
    for (std::uint64_t block = 1; block < scaled; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(Joined(data.seen) == "i200");
    p.Calculate(YSE::T_DSP);
    CHECK(Joined(data.seen) == "i200,i1");
  }

  TEST_CASE("qlist: 'stop' cancels the pending step (#500)") {
    // Max: "stop a qlist which is in the middle of playback as a result of a
    // bang message."
    patcherImplementation p(1, nullptr);
    YSE::pHandle* qlist = p.CreateObject(YSE::OBJ::G_QLIST, "");
    REQUIRE(qlist != nullptr);

    Recorder data;
    YSE::pHandle dataHandle(&data);
    p.Connect(qlist, 0, &dataHandle, 0);

    qlist->SetListData(0, "set 50; 60");
    qlist->SetBang(0);
    REQUIRE(p.Scheduler()->PendingCount() == 1);

    qlist->SetListData(0, "stop");
    CHECK(p.Scheduler()->PendingCount() == 0);

    const std::uint64_t due = messageScheduler::BlocksForMillis(50);
    for (std::uint64_t block = 1; block <= due + 4; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(Joined(data.seen) == "i50");

    // The cursor stayed put, so a `next` carries on from where the clock left
    // off rather than from the top.
    data.reset();
    qlist->SetListData(0, "next");
    CHECK(Joined(data.seen) == "i60");
  }

  TEST_CASE("qlist: a bang restarts from the first line rather than resuming (#500)") {
    // Max: "it begins sending messages from the first line." A bang that
    // resumed would quietly turn stop-then-bang into a continue.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* qlist = p.CreateObject(YSE::OBJ::G_QLIST, "");
    REQUIRE(qlist != nullptr);

    Recorder data;
    YSE::pHandle dataHandle(&data);
    p.Connect(qlist, 0, &dataHandle, 0);

    qlist->SetListData(0, "set 30; 40");
    qlist->SetBang(0);
    qlist->SetListData(0, "stop");
    REQUIRE(Joined(data.seen) == "i30");

    data.reset();
    qlist->SetBang(0);
    CHECK(Joined(data.seen) == "i30");
  }

  TEST_CASE("qlist: symbol cues reach a real .r while the clock plays (#500)") {
    // The remote half, and the one that cannot exist standalone: a cue line
    // beginning with a symbol goes to the receiver of that name. This also
    // covers the tag asymmetry the object documents — a resumed step is on the
    // audio callback, so it addresses the receiver with T_DSP semantics, and a
    // step that took the control-thread reading of its tag would take the
    // patcher's mutex there instead.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* qlist = p.CreateObject(YSE::OBJ::G_QLIST, "");
    REQUIRE(qlist != nullptr);
    YSE::pHandle* receive = p.CreateObject(YSE::OBJ::G_RECEIVE, "cue");
    REQUIRE(receive != nullptr);

    Recorder heard;
    YSE::pHandle heardHandle(&heard);
    p.Connect(receive, 0, &heardHandle, 0);

    // The first cue waits, then the symbol line fires from the resumed step.
    qlist->SetListData(0, "set 40; cue 60 100");
    qlist->SetBang(0);
    CHECK(heard.seen.empty());

    const std::uint64_t due = messageScheduler::BlocksForMillis(40);
    for (std::uint64_t block = 1; block <= due; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(Joined(heard.seen) == "s60 100");
  }

  TEST_CASE("qlist: a single-value cue reaches a .r in the kind it was written (#500)") {
    // `.route`'s rule on the remote side: a cue that wrote an int has to arrive
    // as one, or everything downstream that expects a number sees a
    // one-element list and quietly does nothing.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* qlist = p.CreateObject(YSE::OBJ::G_QLIST, "");
    REQUIRE(qlist != nullptr);
    YSE::pHandle* receive = p.CreateObject(YSE::OBJ::G_RECEIVE, "gain");
    REQUIRE(receive != nullptr);

    Recorder heard;
    YSE::pHandle heardHandle(&heard);
    p.Connect(receive, 0, &heardHandle, 0);

    qlist->SetListData(0, "set gain 7; 5; gain 0.5; 5; gain");
    qlist->SetBang(0);
    // The control-thread step queues its remote send; one block delivers it.
    p.Calculate(YSE::T_DSP);
    CHECK(Joined(heard.seen) == "i7");

    const std::uint64_t due = messageScheduler::BlocksForMillis(5);
    for (std::uint64_t block = 1; block <= due; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(Joined(heard.seen) == "i7,f0.50");

    for (std::uint64_t block = 1; block <= due; ++block)
      p.Calculate(YSE::T_DSP);
    // A bare name is a bang at the far end.
    CHECK(Joined(heard.seen) == "i7,f0.50,!");
  }

  TEST_CASE("qlist: 'clear' during playback ends the walk (#500)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* qlist = p.CreateObject(YSE::OBJ::G_QLIST, "");
    REQUIRE(qlist != nullptr);

    Recorder data;
    YSE::pHandle dataHandle(&data);
    p.Connect(qlist, 0, &dataHandle, 0);

    qlist->SetListData(0, "set 40; 50");
    qlist->SetBang(0);
    REQUIRE(p.Scheduler()->PendingCount() == 1);

    qlist->SetListData(0, "clear");
    CHECK(p.Scheduler()->PendingCount() == 0);

    const std::uint64_t due = messageScheduler::BlocksForMillis(40);
    for (std::uint64_t block = 1; block <= due + 4; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(Joined(data.seen) == "i40");
  }

  TEST_CASE("qlist: a cue list of zero delays advances one entry per block (#500)") {
    // The scheduler's one-block floor, and why a patch cannot write a list that
    // locks the audio thread up: a zero wait is still a wait.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* qlist = p.CreateObject(YSE::OBJ::G_QLIST, "");
    REQUIRE(qlist != nullptr);

    Recorder data;
    YSE::pHandle dataHandle(&data);
    p.Connect(qlist, 0, &dataHandle, 0);

    qlist->SetListData(0, "set 0; 0; 0");
    qlist->SetBang(0);
    CHECK(Joined(data.seen) == "i0");
    p.Calculate(YSE::T_DSP);
    CHECK(Joined(data.seen) == "i0,i0");
    p.Calculate(YSE::T_DSP);
    CHECK(Joined(data.seen) == "i0,i0,i0");
  }

  TEST_CASE("qlist: deleting the object drops its pending step safely (#500)") {
    // Armed possibly long before it is due — far outside the reclaimer's
    // two-block grace — so delivery must re-resolve the target and find
    // nothing. An ASan build trips here if the retired object is touched.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* qlist = p.CreateObject(YSE::OBJ::G_QLIST, "");
    REQUIRE(qlist != nullptr);

    Recorder data;
    YSE::pHandle dataHandle(&data);
    p.Connect(qlist, 0, &dataHandle, 0);

    qlist->SetListData(0, "set 50; 60");
    qlist->SetBang(0);
    REQUIRE(p.Scheduler()->PendingCount() == 1);
    p.DeleteObject(qlist);

    const std::uint64_t due = messageScheduler::BlocksForMillis(50);
    for (std::uint64_t block = 1; block <= due + 2; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(Joined(data.seen) == "i50");
    CHECK(p.Scheduler()->PendingCount() == 0);
  }

  TEST_CASE("qlist: standalone, playback falls back to no waits (#500)") {
    // A standalone object has no dispatch to defer into, so a bang plays the
    // whole list at once rather than abandoning it half-played — `.bondo`'s
    // fallback, for `.bondo`'s reason.
    Rig rig;
    rig.List("set 100; 200; 300");
    rig.Bang();
    CHECK(Joined(rig.data.seen) == "i100,i200,i300");
    CHECK(Joined(rig.end.seen) == "!");
    CHECK_FALSE(rig.obj.IsPlaying());
  }

  // ─── cue-list files (issue #689) ────────────────────────────────────────────

  TEST_CASE("qlist: a standalone object consumes read and write without doing anything (#689)") {
    // No patcher means no file plumbing, and the honest answer is silence: the
    // words are still consumed, because Max dispatches on the selector, but
    // nothing is remembered and nothing is asked for.
    Rig rig;
    rig.List("set 100; foo 1");
    rig.reset();

    rig.List("read cues.txt");
    rig.List("write cues.txt");
    rig.List("read");
    rig.List("write");

    CHECK(Contents(rig.obj) == "100 | foo 1");
    CHECK(rig.data.seen.empty());
    CHECK(rig.end.seen.empty());
    CHECK(rig.file.seen.empty());
    CHECK(rig.obj.ReadFile().empty());
    CHECK(rig.obj.WriteFile().empty());
  }

  TEST_CASE("qlist: a write then a read round-trips the cue list (#689)") {
    // The acceptance criterion, end to end through a real patcher, a real file
    // on disk and real receivers: Max's format out, the same cue list back, and
    // both kinds of line surviving it. Asserted through the outlets and the
    // `.r`s rather than through an accessor, because what has to come back is
    // the cue list a patch can *play*.
    const std::string path = TempFile("yse_qlist_roundtrip_689.txt");

    Recorder data;
    Recorder end;
    Recorder file;
    Recorder cue;
    Recorder gain;
    YSE::pHandle dataHandle(&data);
    YSE::pHandle endHandle(&end);
    YSE::pHandle fileHandle(&file);
    YSE::pHandle cueHandle(&cue);
    YSE::pHandle gainHandle(&gain);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* qlist = p.CreateObject(YSE::OBJ::G_QLIST, "");
    REQUIRE(qlist != nullptr);
    p.Connect(qlist, 0, &dataHandle, 0);
    p.Connect(qlist, 1, &endHandle, 0);
    p.Connect(qlist, 2, &fileHandle, 0);

    YSE::pHandle* cueReceive = p.CreateObject(YSE::OBJ::G_RECEIVE, "cue");
    YSE::pHandle* gainReceive = p.CreateObject(YSE::OBJ::G_RECEIVE, "gain");
    REQUIRE(cueReceive != nullptr);
    REQUIRE(gainReceive != nullptr);
    p.Connect(cueReceive, 0, &cueHandle, 0);
    p.Connect(gainReceive, 0, &gainHandle, 0);

    qlist->SetListData(0, "set 100; cue 60 100; 250 gain 0.5");
    qlist->SetListData(0, "write " + path);
    SettleFiles(p);

    // Max's format: one cue line per line, each ended with a semicolon. The
    // numbers-then-message line was split when it was stored, so it is written
    // out as the two lines it became.
    CHECK(ReadWholeFile(path) == "100;\ncue 60 100;\n250;\ngain 0.5;\n");
    // Max has no outlet for a finished write and neither does this.
    CHECK(file.seen.empty());

    qlist->SetListData(0, "clear");
    qlist->SetListData(0, "next");
    REQUIRE(Joined(end.seen) == "!");
    data.reset();
    end.reset();

    qlist->SetListData(0, "read " + path);
    SettleFiles(p);
    // Exactly one bang, and only after the list is in place.
    REQUIRE(Joined(file.seen) == "!");

    // Three `next`es walk the whole reloaded list: the first stops on 100, the
    // second passes `cue` on its way to 250, the third passes `gain` and runs
    // off the end.
    qlist->SetListData(0, "next");
    qlist->SetListData(0, "next");
    qlist->SetListData(0, "next");
    // The remote sends were made from the control thread, so one block delivers
    // them.
    p.Calculate(YSE::T_DSP);

    CHECK(Joined(data.seen) == "i100,i250");
    CHECK(Joined(end.seen) == "!");
    CHECK(Joined(cue.seen) == "s60 100");
    CHECK(Joined(gain.seen) == "f0.50");

    Remove(path);
  }

  TEST_CASE("qlist: read does nothing in the message handler (#689)") {
    // The reason the plumbing exists. A `read` may be dispatched on the audio
    // callback, so the handler must not open anything — which is observable: the
    // cue list is still empty when the message returns, and only a rendered
    // block puts the file in it.
    const std::string path = TempFile("yse_qlist_deferred_689.txt");
    WriteWholeFile(path, "10;\n20;\n");

    Recorder data;
    Recorder end;
    Recorder file;
    YSE::pHandle dataHandle(&data);
    YSE::pHandle endHandle(&end);
    YSE::pHandle fileHandle(&file);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* qlist = p.CreateObject(YSE::OBJ::G_QLIST, "");
    REQUIRE(qlist != nullptr);
    p.Connect(qlist, 0, &dataHandle, 0);
    p.Connect(qlist, 1, &endHandle, 0);
    p.Connect(qlist, 2, &fileHandle, 0);

    qlist->SetListData(0, "read " + path);
    // Nothing yet: the list is still empty, so a `next` reaches its end at once
    // and the file outlet has not fired. The request is a claim on a slot and
    // the disk has not been touched on this thread.
    qlist->SetListData(0, "next");
    CHECK(data.seen.empty());
    CHECK(Joined(end.seen) == "!");
    CHECK(file.seen.empty());
    REQUIRE(p.FileIO() != nullptr);
    CHECK(p.FileIO()->PendingCount() == 1);

    end.reset();
    SettleFiles(p);
    REQUIRE(Joined(file.seen) == "!");
    CHECK(p.FileIO()->PendingCount() == 0);

    qlist->SetListData(0, "fwd 10");
    CHECK(Joined(data.seen) == "i10,i20");
    CHECK(Joined(end.seen) == "!");

    Remove(path);
  }

  TEST_CASE("qlist: a read replaces the cue list and rewinds the cursor (#689)") {
    // Max's read loads a file into the object; it is not a merge. The cursor
    // goes back with it, because it pointed into a list that no longer exists —
    // without that, a walk after a read starts in the middle of the new one.
    const std::string path = TempFile("yse_qlist_replace_689.txt");
    WriteWholeFile(path, "9;\n");

    Recorder data;
    YSE::pHandle dataHandle(&data);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* qlist = p.CreateObject(YSE::OBJ::G_QLIST, "");
    REQUIRE(qlist != nullptr);
    p.Connect(qlist, 0, &dataHandle, 0);

    qlist->SetListData(0, "set 1; 2; 3");
    qlist->SetListData(0, "next");
    REQUIRE(Joined(data.seen) == "i1");

    qlist->SetListData(0, "read " + path);
    SettleFiles(p);

    data.reset();
    qlist->SetListData(0, "fwd 10");
    CHECK(Joined(data.seen) == "i9");

    Remove(path);
  }

  TEST_CASE("qlist: a file's numbers-then-message line is stored as two lines (#689)") {
    // The grammar rule applies to a file exactly as it applies to the inlet:
    // Max's "treated as two separate lines", so a `next` stops on the number
    // before the message it timed is ever sent.
    const std::string path = TempFile("yse_qlist_split_689.txt");
    WriteWholeFile(path, "500 foo 1 2;\n");

    Recorder data;
    Recorder end;
    Recorder heard;
    YSE::pHandle dataHandle(&data);
    YSE::pHandle endHandle(&end);
    YSE::pHandle heardHandle(&heard);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* qlist = p.CreateObject(YSE::OBJ::G_QLIST, "");
    REQUIRE(qlist != nullptr);
    p.Connect(qlist, 0, &dataHandle, 0);
    p.Connect(qlist, 1, &endHandle, 0);
    p.Connect(qlist, 2, &heardHandle, 0);

    YSE::pHandle* receive = p.CreateObject(YSE::OBJ::G_RECEIVE, "foo");
    REQUIRE(receive != nullptr);
    Recorder remote;
    YSE::pHandle remoteHandle(&remote);
    p.Connect(receive, 0, &remoteHandle, 0);

    qlist->SetListData(0, "read " + path);
    SettleFiles(p);

    qlist->SetListData(0, "next");
    p.Calculate(YSE::T_DSP);
    // The number stopped the walk, so the message half has not been sent yet.
    CHECK(Joined(data.seen) == "i500");
    CHECK(remote.seen.empty());

    qlist->SetListData(0, "next");
    p.Calculate(YSE::T_DSP);
    CHECK(Joined(remote.seen) == "s1 2");
    CHECK(Joined(end.seen) == "!");

    Remove(path);
  }

  TEST_CASE("qlist: a file breaks lines on a newline as well as a semicolon (#689)") {
    // A departure from `.coll`, whose records break on `;` alone, and a
    // deliberate one: a cue line has no punctuation of its own for a line break
    // to steal, so a hand-written file that left the semicolons off still loads
    // as the lines it looks like. Without it the whole file is one cue.
    const std::string path = TempFile("yse_qlist_newline_689.txt");
    WriteWholeFile(path, "10\n20\n30\n");

    Recorder data;
    YSE::pHandle dataHandle(&data);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* qlist = p.CreateObject(YSE::OBJ::G_QLIST, "");
    REQUIRE(qlist != nullptr);
    p.Connect(qlist, 0, &dataHandle, 0);

    qlist->SetListData(0, "read " + path);
    SettleFiles(p);

    qlist->SetListData(0, "fwd 10");
    CHECK(Joined(data.seen) == "i10,i20,i30");

    Remove(path);
  }

  TEST_CASE("qlist: a CRLF file loads the same cue lines (#689)") {
    // The carriage return before the break is whitespace to the tokenizer, so a
    // file written by another editor loads identically on every platform.
    const std::string path = TempFile("yse_qlist_crlf_689.txt");
    WriteWholeFile(path, "10;\r\ncue 60;\r\n20;\r\n");

    Recorder data;
    Recorder heard;
    YSE::pHandle dataHandle(&data);
    YSE::pHandle heardHandle(&heard);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* qlist = p.CreateObject(YSE::OBJ::G_QLIST, "");
    REQUIRE(qlist != nullptr);
    p.Connect(qlist, 0, &dataHandle, 0);

    YSE::pHandle* receive = p.CreateObject(YSE::OBJ::G_RECEIVE, "cue");
    REQUIRE(receive != nullptr);
    p.Connect(receive, 0, &heardHandle, 0);

    qlist->SetListData(0, "read " + path);
    SettleFiles(p);

    qlist->SetListData(0, "next");
    qlist->SetListData(0, "next");
    p.Calculate(YSE::T_DSP);
    CHECK(Joined(data.seen) == "i10,i20");
    CHECK(Joined(heard.seen) == "i60");

    Remove(path);
  }

  TEST_CASE("qlist: the bare forms reuse the last name given, each half its own (#689)") {
    // Max's bare `read` and `write` open a file dialog, which a headless patcher
    // has no equivalent of, so they reuse the last name. Each half remembers its
    // own, which is `.coll`'s rule: a bare `write` after a `read` from somewhere
    // else must not overwrite the file that was read.
    const std::string written = TempFile("yse_qlist_again_written_689.txt");
    const std::string source = TempFile("yse_qlist_again_source_689.txt");
    WriteWholeFile(source, "42;\n");

    Recorder data;
    YSE::pHandle dataHandle(&data);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* qlist = p.CreateObject(YSE::OBJ::G_QLIST, "");
    REQUIRE(qlist != nullptr);
    p.Connect(qlist, 0, &dataHandle, 0);

    qlist->SetListData(0, "set 5");
    qlist->SetListData(0, "write " + written);
    SettleFiles(p);
    REQUIRE(ReadWholeFile(written) == "5;\n");

    qlist->SetListData(0, "read " + source);
    SettleFiles(p);
    qlist->SetListData(0, "fwd 10");
    REQUIRE(Joined(data.seen) == "i42");

    // A bare write goes back to the file the last `write` named, not to the one
    // the last `read` came from.
    qlist->SetListData(0, "write");
    SettleFiles(p);
    CHECK(ReadWholeFile(written) == "42;\n");
    CHECK(ReadWholeFile(source) == "42;\n");

    // And a bare read comes from the source again.
    qlist->SetListData(0, "set 7");
    qlist->SetListData(0, "read");
    SettleFiles(p);
    data.reset();
    qlist->SetListData(0, "fwd 10");
    CHECK(Joined(data.seen) == "i42");

    Remove(written);
    Remove(source);
  }

  TEST_CASE("qlist: a bare read with no name and no argument does nothing (#689)") {
    // Nothing has been named and there is no dialog to ask with, so the message
    // is consumed and no slot is claimed.
    Recorder file;
    YSE::pHandle fileHandle(&file);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* qlist = p.CreateObject(YSE::OBJ::G_QLIST, "");
    REQUIRE(qlist != nullptr);
    p.Connect(qlist, 2, &fileHandle, 0);

    qlist->SetListData(0, "read");
    qlist->SetListData(0, "write");
    REQUIRE(p.FileIO() != nullptr);
    CHECK(p.FileIO()->PendingCount() == 0);

    SettleFiles(p);
    CHECK(file.seen.empty());
  }

  TEST_CASE("qlist: a read of a missing file leaves the cue list alone (#689)") {
    // Every failure alike — no such file, too large, unopenable — is reported by
    // the outlet staying silent and the contents staying put.
    const std::string path = TempFile("yse_qlist_missing_689.txt");

    Recorder data;
    Recorder file;
    YSE::pHandle dataHandle(&data);
    YSE::pHandle fileHandle(&file);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* qlist = p.CreateObject(YSE::OBJ::G_QLIST, "");
    REQUIRE(qlist != nullptr);
    p.Connect(qlist, 0, &dataHandle, 0);
    p.Connect(qlist, 2, &fileHandle, 0);

    qlist->SetListData(0, "set 3");
    qlist->SetListData(0, "read " + path);
    SettleFiles(p);

    CHECK(file.seen.empty());
    qlist->SetListData(0, "fwd 10");
    CHECK(Joined(data.seen) == "i3");
  }

  TEST_CASE("qlist: a file with more cue lines than the table holds keeps the first 256 (#689)") {
    // The bound a `set` already follows, on the file path: the rest of the file
    // is dropped rather than growing a table that cannot be grown from a message
    // handler.
    const std::string path = TempFile("yse_qlist_overflow_689.txt");
    std::string contents;
    for (std::size_t i = 0; i < gQlist::MAX_ENTRIES + 40; i++)
      contents += "1;\n";
    WriteWholeFile(path, contents);

    Recorder data;
    Recorder end;
    YSE::pHandle dataHandle(&data);
    YSE::pHandle endHandle(&end);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* qlist = p.CreateObject(YSE::OBJ::G_QLIST, "");
    REQUIRE(qlist != nullptr);
    p.Connect(qlist, 0, &dataHandle, 0);
    p.Connect(qlist, 1, &endHandle, 0);

    qlist->SetListData(0, "read " + path);
    SettleFiles(p);

    qlist->SetListData(0, "fwd 400");
    CHECK(data.seen.size() == gQlist::MAX_ENTRIES);
    CHECK(Joined(end.seen) == "!");

    Remove(path);
  }

  TEST_CASE("qlist: an over-long cue line is skipped and the rest of the file loads (#689)") {
    // Refused whole rather than truncated, because half a cue is a different
    // cue — and only that line: what surrounds it still loads.
    const std::string path = TempFile("yse_qlist_longline_689.txt");
    WriteWholeFile(path, "1;\n" + std::string(gQlist::ENTRY_CAPACITY + 4, '9') + ";\n2;\n");

    Recorder data;
    YSE::pHandle dataHandle(&data);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* qlist = p.CreateObject(YSE::OBJ::G_QLIST, "");
    REQUIRE(qlist != nullptr);
    p.Connect(qlist, 0, &dataHandle, 0);

    qlist->SetListData(0, "read " + path);
    SettleFiles(p);

    qlist->SetListData(0, "fwd 10");
    CHECK(Joined(data.seen) == "i1,i2");

    Remove(path);
  }

  TEST_CASE("qlist: deleting the object with a read in flight is safe (#689)") {
    // The job holds no pObject at all, and the completion re-resolves its target
    // against the block's pinned GraphState — so a delete racing a read drops
    // the result by construction. An ASan build trips here if it does not.
    const std::string path = TempFile("yse_qlist_delete_689.txt");
    WriteWholeFile(path, "1;\n2;\n");

    patcherImplementation p(1, nullptr);
    YSE::pHandle* qlist = p.CreateObject(YSE::OBJ::G_QLIST, "");
    REQUIRE(qlist != nullptr);

    qlist->SetListData(0, "read " + path);
    REQUIRE(p.FileIO() != nullptr);
    REQUIRE(p.FileIO()->PendingCount() == 1);
    p.DeleteObject(qlist);

    SettleFiles(p);
    CHECK(p.FileIO()->PendingCount() == 0);

    Remove(path);
  }

  TEST_CASE("qlist: a cue list loaded from a file plays on the clock (#689)") {
    // The whole point, end to end: a file on disk becomes a sequence that a bang
    // plays in time, with its symbol cues reaching real receivers and its
    // numeric cue actually waiting. Nothing here exists standalone — the read,
    // the completion, the clock and the remote sends are all the patcher's.
    const std::string path = TempFile("yse_qlist_play_689.txt");
    WriteWholeFile(path, "cue 60 100;\n100;\ncue 62 100;\n");

    Recorder data;
    Recorder end;
    Recorder file;
    Recorder heard;
    YSE::pHandle dataHandle(&data);
    YSE::pHandle endHandle(&end);
    YSE::pHandle fileHandle(&file);
    YSE::pHandle heardHandle(&heard);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* qlist = p.CreateObject(YSE::OBJ::G_QLIST, "");
    REQUIRE(qlist != nullptr);
    p.Connect(qlist, 0, &dataHandle, 0);
    p.Connect(qlist, 1, &endHandle, 0);
    p.Connect(qlist, 2, &fileHandle, 0);

    YSE::pHandle* receive = p.CreateObject(YSE::OBJ::G_RECEIVE, "cue");
    REQUIRE(receive != nullptr);
    p.Connect(receive, 0, &heardHandle, 0);

    qlist->SetListData(0, "read " + path);
    SettleFiles(p);
    REQUIRE(Joined(file.seen) == "!");

    qlist->SetBang(0);
    // The first cue is a symbol line sent from the control thread, so it is
    // queued for the next block; the second is numeric, so it goes out at once
    // and arms the wait.
    CHECK(Joined(data.seen) == "i100");
    CHECK(heard.seen.empty());

    p.Calculate(YSE::T_DSP);
    CHECK(Joined(heard.seen) == "s60 100");
    CHECK(end.seen.empty());

    // The last cue is a resumed step on the audio callback, which reaches the
    // receiver without a further block.
    const std::uint64_t due = messageScheduler::BlocksForMillis(100);
    for (std::uint64_t block = 2; block <= due; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(Joined(heard.seen) == "s60 100,s62 100");
    CHECK(Joined(end.seen) == "!");
    CHECK(p.Scheduler()->PendingCount() == 0);

    Remove(path);
  }

} // TEST_SUITE("patcher")
