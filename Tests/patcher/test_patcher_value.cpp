// Tests for .value (issue #486) — a named cell shared by every .value with the
// same name.
//
// The object is small; what it has to get right is who sees whose writes, and
// every rule below is one a plausible implementation gets wrong:
//
//   - **storing is silent, banging emits.** Max's `value` is a register, not a
//     send. An implementation that echoed on store would be a `.s` with a
//     memory, and every patch that writes and reads the same name in one graph
//     would loop.
//   - **the cell is shared by name, and the name is the bus's name.** Two
//     `.value tempo` in one patcher are one cell; two patchers given the same
//     `patcher::name()` are one cell too, exactly as their sends and receives
//     already are. Two patchers left with their auto-generated names are not.
//   - **an unnamed .value is private.** Not "shares the empty name":
//     `"<patcherName>."` is a real address, so pooling there would silently
//     join every unconfigured `.value` in the patcher.
//   - **the initial argument belongs to whoever creates the cell.** A second
//     `.value tempo 60` joining an established name must adopt what is stored
//     rather than rewind it — otherwise adding a reader to a patch changes what
//     every other object reads.
//   - **the stored value is run-time state, not a parameter.** What a saved
//     patch carries is where the cell *started*.
//   - **a store arriving on the audio thread lands.** The in-patcher delivery
//     path (`PassData`) dispatches on T_DSP, so ".s writes a .value" is the
//     ordinary case rather than an exotic one, and it must not be the case that
//     silently does nothing.
//
// No audio device and no engine of its own. Patchers created here take their
// auto-generated "patcher_<N>" names unless a case renames them on purpose,
// which is what keeps one case's cells invisible to the next.

#include <doctest/doctest.h>
#include <string>

#include "patcher/genericObjects/gValue.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"
#include "utils/json.hpp"

using TestHelpers::MultiSink;
using TestHelpers::Wire;
using YSE::PATCHER::gValue;
using YSE::PATCHER::valueKind;
using YSE::PATCHER::valueSlot;

namespace {

  // A patcher with two .value objects and a sink on each, so "what does the
  // other one see?" is one assertion. The sinks are declared before the patcher
  // so the patcher is torn down first, while the inlets it is wired to still
  // exist.
  struct Rig {
    MultiSink a;
    MultiSink b;
    YSE::pHandle aHandle{&a};
    YSE::pHandle bHandle{&b};
    YSE::patcher p;
    YSE::pHandle* va = nullptr;
    YSE::pHandle* vb = nullptr;

    Rig(const std::string& argsA, const std::string& argsB) {
      p.create(2);
      va = p.CreateObject(YSE::OBJ::G_VALUE, argsA);
      vb = p.CreateObject(YSE::OBJ::G_VALUE, argsB);
      REQUIRE(va != nullptr);
      REQUIRE(vb != nullptr);
      p.Connect(va, 0, &aHandle, 0);
      p.Connect(vb, 0, &bHandle, 0);
    }

    void Reset() {
      a.reset();
      b.reset();
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("value: registered, one inlet, one outlet (#486)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_VALUE);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".value");
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 1);
  }

  TEST_CASE("value: appears in the registry's name list (#486)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_VALUE)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("value: the inlet takes bang, int, float and list (#486)") {
    gValue v;
    const unsigned int types = v.GetInlet(0)->GetAcceptedTypes();
    CHECK((types & YSE::PATCHER::IT_BANG) != 0);
    CHECK((types & YSE::PATCHER::IT_INT) != 0);
    CHECK((types & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((types & YSE::PATCHER::IT_LIST) != 0);
  }

  TEST_CASE("value: without a parent patcher it still stores and recalls (#486)") {
    // A standalone .value has no patcher name to prefix with, so it keeps a
    // private cell rather than crashing or silently dropping.
    MultiSink sink;
    gValue v;
    v.SetParams("tempo");
    Wire(v, 0, sink);
    CHECK_FALSE(v.IsShared());

    v.GetInlet(0)->SetInt(7, YSE::T_GUI);
    v.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 7);
  }

  // ─── store and recall ───────────────────────────────────────────────────────

  TEST_CASE("value: a bang before anything is stored emits nothing (#486)") {
    // Emitting a zero here would be indistinguishable from a zero a patch
    // actually stored.
    Rig rig("", "");
    rig.va->SetBang(0);
    CHECK_FALSE(rig.a.gotBang);
    CHECK_FALSE(rig.a.gotInt);
    CHECK_FALSE(rig.a.gotFloat);
    CHECK_FALSE(rig.a.gotList);
  }

  TEST_CASE("value: storing emits nothing; the bang does (#486)") {
    Rig rig("tempo", "tempo");
    rig.va->SetIntData(0, 120);
    CHECK_FALSE(rig.a.gotInt);
    CHECK_FALSE(rig.b.gotInt);

    rig.va->SetBang(0);
    CHECK(rig.a.gotInt);
    CHECK(rig.a.intValue == 120);
  }

  TEST_CASE("value: int, float and list are recalled as themselves (#486)") {
    Rig rig("v", "v");

    rig.va->SetIntData(0, 42);
    rig.va->SetBang(0);
    CHECK(rig.a.gotInt);
    CHECK(rig.a.intValue == 42);

    rig.Reset();
    rig.va->SetFloatData(0, 0.125f);
    rig.va->SetBang(0);
    CHECK(rig.a.gotFloat);
    CHECK(rig.a.floatValue == doctest::Approx(0.125f));
    CHECK_FALSE(rig.a.gotInt);

    rig.Reset();
    rig.va->SetListData(0, "one two three");
    rig.va->SetBang(0);
    CHECK(rig.a.gotList);
    CHECK(rig.a.listValue == "one two three");
    CHECK_FALSE(rig.a.gotFloat);
  }

  TEST_CASE("value: the last value written wins, whichever object wrote it (#486)") {
    Rig rig("tempo", "tempo");
    rig.va->SetIntData(0, 120);
    rig.vb->SetIntData(0, 90);

    rig.va->SetBang(0);
    CHECK(rig.a.intValue == 90);
  }

  // ─── who shares with whom ───────────────────────────────────────────────────

  TEST_CASE("value: two .value objects with the same name are one cell (#486)") {
    // The object itself: written here, read there, with no wire between them.
    Rig rig("tempo", "tempo");
    CHECK(rig.va->GetParams() == std::string("tempo"));

    rig.va->SetIntData(0, 120);
    rig.vb->SetBang(0);
    CHECK(rig.b.gotInt);
    CHECK(rig.b.intValue == 120);
    CHECK_FALSE(rig.a.gotInt);
  }

  TEST_CASE("value: different names are different cells (#486)") {
    Rig rig("tempo", "swing");
    rig.va->SetIntData(0, 120);
    rig.vb->SetBang(0);
    CHECK_FALSE(rig.b.gotInt);

    rig.vb->SetIntData(0, 66);
    rig.va->SetBang(0);
    CHECK(rig.a.gotInt);
    CHECK(rig.a.intValue == 120);
  }

  TEST_CASE("value: unnamed .value objects do not pool with each other (#486)") {
    // "<patcherName>." is a real, reachable address, so sharing it would join
    // two objects a patch author has not connected in any visible way.
    Rig rig("", "");
    rig.va->SetIntData(0, 5);
    rig.vb->SetBang(0);
    CHECK_FALSE(rig.b.gotInt);

    rig.va->SetBang(0);
    CHECK(rig.a.gotInt);
    CHECK(rig.a.intValue == 5);
  }

  TEST_CASE("value: the cell address is the bus's address form (#486)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("value_song");

    gValue v;
    v.SetParams("tempo");
    v.SetParent(&p);
    CHECK(v.IsShared());
    CHECK(v.Address() == "value_song.tempo");

    gValue unnamed;
    unnamed.SetParent(&p);
    CHECK_FALSE(unnamed.IsShared());
    CHECK(unnamed.Address().empty());
  }

  // ─── across patchers ────────────────────────────────────────────────────────

  TEST_CASE("value: patchers sharing a name share their cells (#486)") {
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);

    YSE::patcher writer;
    writer.create(2);
    writer.name("value_song");
    YSE::pHandle* out = writer.CreateObject(YSE::OBJ::G_VALUE, "tempo");
    REQUIRE(out != nullptr);

    {
      YSE::patcher reader;
      reader.create(2);
      reader.name("value_song");
      YSE::pHandle* in = reader.CreateObject(YSE::OBJ::G_VALUE, "tempo");
      REQUIRE(in != nullptr);
      reader.Connect(in, 0, &sinkHandle, 0);

      out->SetIntData(0, 132);
      in->SetBang(0);
      CHECK(sink.gotInt);
      CHECK(sink.intValue == 132);
    }
  }

  TEST_CASE("value: patchers with different names keep their cells apart (#486)") {
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);

    YSE::patcher writer;
    writer.create(2);
    writer.name("value_songA");
    YSE::pHandle* out = writer.CreateObject(YSE::OBJ::G_VALUE, "tempo");
    REQUIRE(out != nullptr);

    YSE::patcher reader;
    reader.create(2);
    reader.name("value_songB");
    YSE::pHandle* in = reader.CreateObject(YSE::OBJ::G_VALUE, "tempo");
    REQUIRE(in != nullptr);
    reader.Connect(in, 0, &sinkHandle, 0);

    out->SetIntData(0, 132);
    in->SetBang(0);
    CHECK_FALSE(sink.gotInt);
  }

  TEST_CASE("value: renaming a patcher re-binds its values with its sends (#486)") {
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);

    YSE::patcher writer;
    writer.create(2);
    writer.name("value_early");
    YSE::pHandle* out = writer.CreateObject(YSE::OBJ::G_VALUE, "tempo");
    REQUIRE(out != nullptr);

    YSE::patcher reader;
    reader.create(2);
    reader.name("value_late");
    YSE::pHandle* in = reader.CreateObject(YSE::OBJ::G_VALUE, "tempo");
    REQUIRE(in != nullptr);
    reader.Connect(in, 0, &sinkHandle, 0);

    out->SetIntData(0, 100);
    in->SetBang(0);
    CHECK_FALSE(sink.gotInt);

    // The name is the address, so moving the patcher moves its cells — onto
    // the ones the renamed patcher's .s and .r now speak about too.
    writer.name("value_late");
    out->SetIntData(0, 140);
    in->SetBang(0);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 140);
  }

  // ─── the initial value ──────────────────────────────────────────────────────

  TEST_CASE("value: the second argument is the initial value (#486)") {
    Rig ints("tempo 120", "tempo");
    ints.vb->SetBang(0);
    CHECK(ints.b.gotInt);
    CHECK(ints.b.intValue == 120);

    Rig floats("gain 0.5", "gain");
    floats.vb->SetBang(0);
    CHECK(floats.b.gotFloat);
    CHECK(floats.b.floatValue == doctest::Approx(0.5f));

    // A number spelled as a float stays a float, the way .sel and .trigger
    // classify their own constant arguments.
    Rig spelled("count 4.", "count");
    spelled.vb->SetBang(0);
    CHECK(spelled.b.gotFloat);
    CHECK_FALSE(spelled.b.gotInt);
  }

  TEST_CASE("value: the initial is the whole rest of the argument, so it can be a list (#486)") {
    Rig rig("pos 0 12 5", "pos");
    rig.vb->SetBang(0);
    CHECK(rig.b.gotList);
    CHECK(rig.b.listValue == "0 12 5");
  }

  TEST_CASE("value: a non-numeric initial is stored as the symbol it was written as (#486)") {
    Rig rig("mode legato", "mode");
    rig.vb->SetBang(0);
    CHECK(rig.b.gotList);
    CHECK(rig.b.listValue == "legato");
  }

  TEST_CASE("value: a later .value adopts the stored value rather than resetting it (#486)") {
    // Adding a reader to a patch must not rewind what every other object is
    // reading — so only whoever creates the cell gets to say where it starts.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* first = p.CreateObject(YSE::OBJ::G_VALUE, "tempo 120");
    REQUIRE(first != nullptr);
    first->SetIntData(0, 90);

    YSE::pHandle* second = p.CreateObject(YSE::OBJ::G_VALUE, "tempo 60");
    REQUIRE(second != nullptr);
    p.Connect(second, 0, &sinkHandle, 0);

    second->SetBang(0);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 90);
  }

  // ─── what is refused ────────────────────────────────────────────────────────

  TEST_CASE("value: a list of 256 characters is stored, 257 is refused (#486)") {
    // Refused rather than truncated: half a list is a different list, and the
    // value a patch is reading must not silently become one.
    Rig rig("v", "v");
    const std::string longest(valueSlot::kTextCapacity, 'x');
    rig.va->SetListData(0, longest);
    rig.va->SetBang(0);
    CHECK(rig.a.gotList);
    CHECK(rig.a.listValue == longest);

    rig.Reset();
    rig.va->SetListData(0, std::string(valueSlot::kTextCapacity + 1, 'y'));
    rig.va->SetBang(0);
    CHECK(rig.a.gotList);
    CHECK(rig.a.listValue == longest);
  }

  // ─── threads ────────────────────────────────────────────────────────────────

  TEST_CASE("value: a store arriving on the audio thread lands (#486)") {
    // The in-patcher delivery path defers to the audio thread and is drained by
    // an explicit Calculate (issue #225), so a .r feeding a .value stores on
    // T_DSP. That is the ordinary case, not an exotic one.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "tempo");
    YSE::pHandle* store = p.CreateObject(YSE::OBJ::G_VALUE, "tempo");
    YSE::pHandle* poll = p.CreateObject(YSE::OBJ::G_VALUE, "tempo");
    REQUIRE(recv != nullptr);
    REQUIRE(store != nullptr);
    REQUIRE(poll != nullptr);
    p.Connect(recv, 0, store, 0);
    p.Connect(poll, 0, &sinkHandle, 0);

    p.PassData(96, "tempo", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);

    poll->SetBang(0);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 96);
  }

  TEST_CASE("value: a list stored and recalled on the audio thread survives intact (#486)") {
    MultiSink sink;
    gValue v;
    Wire(v, 0, sink);

    v.GetInlet(0)->SetList("held on T_DSP", YSE::T_DSP);
    CHECK(v.Kind() == valueKind::List);
    v.GetInlet(0)->SetBang(YSE::T_DSP);
    CHECK(sink.gotList);
    CHECK(sink.listValue == "held on T_DSP");
  }

  // ─── parameters ─────────────────────────────────────────────────────────────

  TEST_CASE("value: params survive a DumpJSON / ParseJSON round trip (#486)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_VALUE, "tempo 120");
    REQUIRE(h != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".value") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".value"));
    CHECK(copy->GetParams() == std::string("tempo 120"));
    CHECK(copy->GetInputs() == 1);
    CHECK(copy->GetOutputs() == 1);
  }

  TEST_CASE("value: the stored value is run-time state, not a parameter (#486)") {
    // What a saved patch carries is where the cell *started*. Storing into it
    // is a message, exactly as .router's connections are, and a message must
    // not rewrite the file.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_VALUE, "tempo 120");
    REQUIRE(h != nullptr);
    h->SetIntData(0, 777);
    CHECK(h->GetParams() == std::string("tempo 120"));

    // Read the dump as JSON rather than searching its text for "777". Every
    // object also serialises its storage ID — and, under "outputs", the IDs of
    // whatever its outlets point at — so a substring search over the raw dump
    // is also asking what those IDs happen to spell. When this case was written
    // the ID came from a process-wide counter, which made the answer depend on
    // how many patcher objects the cases before this one had built: it came up
    // 35777 on Linux CI and 35798 on Windows, which is the whole of why the old
    // assertion passed locally and failed there. Issue #730 has since made the
    // counter per-patcher, so this dump's IDs are now a small stable 0, but the
    // shape of the assertion is the point and stays: what #486 is about is the
    // object's own record — its creation parameters, and any state it asks to
    // persist alongside them — not fields the patcher fills in for it.
    const auto dump = nlohmann::json::parse(src.DumpJSON(), nullptr, false);
    REQUIRE_FALSE(dump.is_discarded());
    REQUIRE(dump.size() == 1u);

    nlohmann::json record = dump.begin().value();
    CHECK(record["parms"].get<std::string>() == std::string("tempo 120"));
    // A .value has nothing to keep beyond its creation argument, so it writes
    // no "state" key either — that hook (issue #494) is exactly where a stored
    // value would end up if it were mistaken for persistent data.
    CHECK(record.find("state") == record.end());
    // And the store is nowhere else in the record either. Same reach as the
    // substring search this replaces, minus the two counter-derived fields.
    record.erase("ID");
    record.erase("outputs");
    CHECK(record.dump().find("777") == std::string::npos);
  }

  TEST_CASE("value: re-parsing with an empty argument returns it to a private cell (#486)") {
    gValue v;
    v.SetParams("tempo");
    CHECK(v.ValueName() == "tempo");
    v.SetParams("");
    CHECK(v.ValueName().empty());
    CHECK_FALSE(v.IsShared());
  }

  TEST_CASE("value: a live re-parse that leaves the name alone keeps the stored value (#486)") {
    // A SetParams on a published object is a rebuild (#234): the replacement is
    // built while the original still holds the cell, so it joins the cell it
    // already had and the running value carries across the edit instead of
    // snapping back to the creation argument.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* keeper = p.CreateObject(YSE::OBJ::G_VALUE, "tempo 120");
    YSE::pHandle* edited = p.CreateObject(YSE::OBJ::G_VALUE, "tempo");
    REQUIRE(keeper != nullptr);
    REQUIRE(edited != nullptr);
    p.Connect(keeper, 0, &sinkHandle, 0);

    edited->SetIntData(0, 90);
    edited->SetParams("tempo 120");

    keeper->SetBang(0);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 90);
  }

  // ─── end to end ─────────────────────────────────────────────────────────────

  TEST_CASE("value: a whole patch polls a shared value with no wire between the ends (#486)") {
    // Nothing test-only in this one: two .value objects and an .i, built and
    // driven entirely through the public patcher API, which is how a host
    // reaches the object.
    YSE::patcher p;
    p.create(2);
    p.name("e2e_song");

    YSE::pHandle* writer = p.CreateObject(YSE::OBJ::G_VALUE, "tempo 120");
    YSE::pHandle* reader = p.CreateObject(YSE::OBJ::G_VALUE, "tempo");
    YSE::pHandle* display = p.CreateObject(YSE::OBJ::G_INT);
    REQUIRE(writer != nullptr);
    REQUIRE(reader != nullptr);
    REQUIRE(display != nullptr);
    p.Connect(reader, 0, display, 0);

    // The cell starts where the creation argument put it, and the far end can
    // ask for it without ever having been told.
    reader->SetBang(0);
    CHECK(display->GetGuiValue() == std::string("120"));

    // A write on one end is visible from the other on the next poll.
    writer->SetIntData(0, 90);
    reader->SetBang(0);
    CHECK(display->GetGuiValue() == std::string("90"));

    // ...and a poll with no write in between still answers, which is the whole
    // difference from .s / .r.
    reader->SetBang(0);
    CHECK(display->GetGuiValue() == std::string("90"));
  }

} // TEST_SUITE("patcher")
