// The GUI value protocol (issue #551) — the shape every GUI object publishes
// its state through, and the shape `.preset` will read and push back.
//
// The protocol itself is documented in YseEngine/patcher/pObject.h; this file
// pins it. Two halves:
//
//   read   GetGuiValue() is the whole state as one string; GetGuiValueCount()
//          / GetGuiValueAt(i) are the same state cell by cell. A scalar
//          control is the one-cell case and gets it from pObject for free, so
//          every object that predates the protocol answers exactly what it
//          always answered.
//   write  no setter. State arrives as a list on inlet 0, on the control
//          thread, and an object that answers GuiValueIsSettable() promises
//          that the string its own read produced is accepted back verbatim,
//          plus "set <index> <value>" for one cell.
//
// The `CellObject` below is the worked example the structured controls
// (`.rslider`, `.multislider`, `.matrixctrl`) are built against — it uses the
// same _HAS_GUI_CELLS / GUI_VALUE_COUNT / GUI_VALUE_AT macros they will, so a
// break in the macros surfaces here rather than in three objects at once.
//
// No audio device required.

#include <doctest/doctest.h>
#include <atomic>
#include <sstream>
#include <string>
#include <vector>

#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObject.h"
#include "patcher/pObjectList.hpp"

namespace {

  constexpr unsigned int kCells = 3;

  // A three-cell float bank: the smallest honest structured control. Cells are
  // atomic because the read side is polled from the host thread while the
  // write side runs wherever the message arrived — the contract in pObject.h.
  struct CellObject : YSE::PATCHER::pObject {
    CellObject() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterList([this](const std::string& v, int, YSE::THREAD) { SetCells(v); });
      for (unsigned int i = 0; i < kCells; i++)
        cells[i].store(0.f, std::memory_order_relaxed);
    }
    const char* Type() const override {
      return "cell_object";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

    _HAS_GUI_CELLS

    // The write half. Wait-free by construction: it parses into locals and
    // publishes with plain relaxed stores, so it is safe on whichever thread
    // the message arrived on.
    void SetCells(const std::string& value) {
      std::istringstream in(value);
      std::string first;
      if (!(in >> first)) return;

      if (first == "set") {
        unsigned int index = 0;
        float v = 0.f;
        if (!(in >> index) || !(in >> v)) return;
        if (index >= kCells) return; // range-checked, never indexed blindly
        cells[index].store(v, std::memory_order_relaxed);
        return;
      }

      // Whole-state form: the exact string GetGuiValue() produced. Parsed
      // through a stream rather than std::stof, which throws — nothing on a
      // message path may.
      float parsed[kCells];
      std::istringstream firstIn(first);
      if (!(firstIn >> parsed[0])) return;
      for (unsigned int i = 1; i < kCells; i++) {
        if (!(in >> parsed[i])) return; // short list changes nothing
      }
      for (unsigned int i = 0; i < kCells; i++)
        cells[i].store(parsed[i], std::memory_order_relaxed);
    }

    std::atomic<float> cells[kCells];
  };

#define className CellObject

  GUI_VALUE() {
    std::string out;
    for (unsigned int i = 0; i < kCells; i++) {
      if (i > 0) out += ' ';
      out += std::to_string(cells[i].load(std::memory_order_relaxed));
    }
    return out;
  }

  GUI_VALUE_COUNT() {
    return kCells;
  }

  GUI_VALUE_AT() {
    if (index >= kCells) return std::string();
    return std::to_string(cells[index].load(std::memory_order_relaxed));
  }

#undef className

} // namespace

TEST_SUITE("patcher") {

  // ─── the default shape ──────────────────────────────────────────────────────

  TEST_CASE("gui protocol: every pre-#551 object is the one-cell case, unchanged") {
    // The protocol had to extend GetGuiValue() without touching the ~30
    // objects that implement it. The default count is 1, cell 0 *is*
    // GetGuiValue() (by construction in pObject, not by copied code), and
    // nothing claims the write round trip it cannot honour. The scalar
    // controls have since been migrated onto the write half (#846) and moved
    // to the settable case below; these are the ones that stay out — `.b`
    // deliberately (its value is a consume-on-read press, an event no restore
    // could write back), the rest because nothing has needed them yet.
    YSE::patcher p;
    p.create(2);

    const char* types[] = {YSE::OBJ::G_BUTTON, YSE::OBJ::G_LIST, YSE::OBJ::G_MESSAGE,
                           YSE::OBJ::G_COUNTER, YSE::OBJ::G_TEXT};

    for (const char* type : types) {
      CAPTURE(type);
      YSE::pHandle* h = p.CreateObject(type);
      REQUIRE(h != nullptr);

      CHECK(h->GetGuiValueCount() == 1u);
      // False everywhere: these do not take the display string back, so
      // claiming the round trip would be a lie.
      CHECK_FALSE(h->GuiValueIsSettable());
      // Cell 0 and the whole state are the same read. `.b` consumes on read,
      // so this compares an untouched object, where both forms answer "off" —
      // polling a *pressed* one twice is exactly what the protocol forbids.
      CHECK(h->GetGuiValueAt(0) == h->GetGuiValue());
      // Past the end is "", not the value again.
      CHECK(h->GetGuiValueAt(1).empty());
      CHECK(h->GetGuiValueAt(4000000u).empty());
    }
  }

  TEST_CASE("gui protocol: a scalar object's cell 0 tracks its value") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_SLIDER);
    REQUIRE(h != nullptr);

    h->SetFloatData(0, 0.25f);
    CHECK(h->GetGuiValue() == std::to_string(0.25f));
    CHECK(h->GetGuiValueAt(0) == std::to_string(0.25f));
    CHECK(h->GetGuiValueCount() == 1u);
  }

  // ─── the #846 migration: the scalar controls join the settable protocol ─────

  TEST_CASE("gui protocol: the #846 scalar controls are settable and stay one-cell") {
    // The per-object migration issue #846 asks for: `.slider`, `.i`, `.f`,
    // `.dial`, `.incdec` and `.t` now hold the write half of the protocol.
    // They stay the one-cell case pObject supplies for free.
    YSE::patcher p;
    p.create(2);

    const char* types[] = {YSE::OBJ::G_SLIDER, YSE::OBJ::G_INT,    YSE::OBJ::G_FLOAT,
                           YSE::OBJ::G_DIAL,   YSE::OBJ::G_INCDEC, YSE::OBJ::G_TOGGLE};

    for (const char* type : types) {
      CAPTURE(type);
      YSE::pHandle* h = p.CreateObject(type);
      REQUIRE(h != nullptr);

      CHECK(h->GuiValueIsSettable());
      CHECK(h->GetGuiValueCount() == 1u);
      CHECK(h->GetGuiValueAt(0) == h->GetGuiValue());
      CHECK(h->GetGuiValueAt(1).empty());
    }
  }

  TEST_CASE("gui protocol: each #846 control round-trips its own GetGuiValue") {
    // The settable promise per migrated object: read the display string, move
    // the control, send the string back as a list on inlet 0, and the state
    // is back — exactly the trip `.preset` makes on a recall.
    YSE::patcher p;
    p.create(2);

    auto roundTrip = [](YSE::pHandle* h, const std::string& stored) {
      REQUIRE(h->GetGuiValue() != stored);
      h->SetListData(0, stored);
      CHECK(h->GetGuiValue() == stored);
    };

    SUBCASE(".slider") {
      YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_SLIDER);
      REQUIRE(h != nullptr);
      h->SetFloatData(0, 0.25f);
      const std::string stored = h->GetGuiValue();
      h->SetFloatData(0, 0.75f);
      roundTrip(h, stored);
    }
    SUBCASE(".i") {
      YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_INT);
      REQUIRE(h != nullptr);
      h->SetIntData(0, 42);
      const std::string stored = h->GetGuiValue();
      h->SetIntData(0, 7);
      roundTrip(h, stored);
    }
    SUBCASE(".f") {
      YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_FLOAT);
      REQUIRE(h != nullptr);
      h->SetFloatData(0, 1.5f);
      const std::string stored = h->GetGuiValue();
      h->SetFloatData(0, -2.25f);
      roundTrip(h, stored);
    }
    SUBCASE(".dial") {
      YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_DIAL, "20 20000 4");
      REQUIRE(h != nullptr);
      h->SetFloatData(0, 0.25f);
      const std::string stored = h->GetGuiValue();
      h->SetFloatData(0, 0.5f);
      roundTrip(h, stored);
    }
    SUBCASE(".incdec") {
      YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_INCDEC);
      REQUIRE(h != nullptr);
      h->SetIntData(0, 5);
      const std::string stored = h->GetGuiValue();
      h->SetIntData(0, 9);
      roundTrip(h, stored);
    }
    SUBCASE(".t") {
      YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_TOGGLE);
      REQUIRE(h != nullptr);
      h->SetIntData(0, 1);
      const std::string stored = h->GetGuiValue();
      REQUIRE(stored == "on");
      h->SetIntData(0, 0);
      roundTrip(h, stored);
      // And the other way round: "off" is a string the inlet takes back too.
      h->SetListData(0, "off");
      CHECK(h->GetGuiValue() == "off");
    }
  }

  TEST_CASE("gui protocol: 'set 0 <value>' writes each #846 scalar's one cell") {
    // The cell half of the promise, and its range check: index 0 is the one
    // cell there is, and any other index is dropped rather than folded.
    YSE::patcher p;
    p.create(2);

    SUBCASE(".slider clamps the cell write like any other input") {
      YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_SLIDER);
      REQUIRE(h != nullptr);
      h->SetListData(0, "set 0 0.5");
      CHECK(h->GetGuiValue() == std::to_string(0.5f));
      h->SetListData(0, "set 1 0.9");
      CHECK(h->GetGuiValue() == std::to_string(0.5f));
      h->SetListData(0, "set 0 2");
      CHECK(h->GetGuiValue() == std::to_string(1.f));
    }
    SUBCASE(".i reads decimal integers, so a large value survives the trip") {
      YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_INT);
      REQUIRE(h != nullptr);
      h->SetListData(0, "set 0 41");
      CHECK(h->GetGuiValue() == "41");
      h->SetListData(0, "set 1 5");
      CHECK(h->GetGuiValue() == "41");
      // 24 bits of float mantissa would not carry this; 31 bits of int do.
      h->SetListData(0, "set 0 2000000001");
      CHECK(h->GetGuiValue() == "2000000001");
    }
    SUBCASE(".f") {
      YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_FLOAT);
      REQUIRE(h != nullptr);
      h->SetListData(0, "set 0 1.5");
      CHECK(h->GetGuiValue() == std::to_string(1.5f));
      h->SetListData(0, "set 3 9");
      CHECK(h->GetGuiValue() == std::to_string(1.5f));
    }
    SUBCASE(".dial stores the position, clamped") {
      YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_DIAL);
      REQUIRE(h != nullptr);
      h->SetListData(0, "set 0 0.5");
      CHECK(h->GetGuiValue() == std::to_string(0.5f));
      h->SetListData(0, "set 0 7");
      CHECK(h->GetGuiValue() == std::to_string(1.f));
      h->SetListData(0, "set 1 0.1");
      CHECK(h->GetGuiValue() == std::to_string(1.f));
    }
    SUBCASE(".incdec tells the cell write from Max's 'set <n>' by counting") {
      YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_INCDEC);
      REQUIRE(h != nullptr);
      // Two numbers after the word: the protocol's cell write.
      h->SetListData(0, "set 0 9");
      CHECK(h->GetGuiValue() == "9");
      // Any other index addresses nothing — not Max's form, not cell 0.
      h->SetListData(0, "set 3 7");
      CHECK(h->GetGuiValue() == "9");
      // One number after the word: Max's silent set, unchanged.
      h->SetListData(0, "set 5");
      CHECK(h->GetGuiValue() == "5");
    }
    SUBCASE(".t takes 'on'/'off' or a number as the cell value") {
      YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_TOGGLE);
      REQUIRE(h != nullptr);
      h->SetListData(0, "set 0 on");
      CHECK(h->GetGuiValue() == "on");
      h->SetListData(0, "set 0 0");
      CHECK(h->GetGuiValue() == "off");
      h->SetListData(0, "set 1 1");
      CHECK(h->GetGuiValue() == "off");
      h->SetListData(0, "set 0 1");
      CHECK(h->GetGuiValue() == "on");
    }
  }

  TEST_CASE("gui protocol: the #846 migration leaves the native inlets unchanged") {
    // The acceptance line issue #846 draws: the existing int/float grammar of
    // each control is exactly what it was — the list handler is an addition,
    // not a rewrite — and a leading token that is neither a number nor "set"
    // addresses nothing.
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* slider = p.CreateObject(YSE::OBJ::G_SLIDER);
    YSE::pHandle* number = p.CreateObject(YSE::OBJ::G_INT);
    YSE::pHandle* toggle = p.CreateObject(YSE::OBJ::G_TOGGLE);
    REQUIRE(slider != nullptr);
    REQUIRE(number != nullptr);
    REQUIRE(toggle != nullptr);

    // The slider still clamps.
    slider->SetFloatData(0, 1.5f);
    CHECK(slider->GetGuiValue() == std::to_string(1.f));
    slider->SetIntData(0, -2);
    CHECK(slider->GetGuiValue() == std::to_string(0.f));

    // The int box still truncates a float.
    number->SetFloatData(0, 2.9f);
    CHECK(number->GetGuiValue() == "2");

    // The toggle's bang still flips; a whole-state write sets absolutely.
    toggle->SetIntData(0, 5);
    CHECK(toggle->GetGuiValue() == "on");
    toggle->SetBang(0);
    CHECK(toggle->GetGuiValue() == "off");
    toggle->SetListData(0, "off");
    CHECK(toggle->GetGuiValue() == "off");

    // A junk list addresses nothing on any of them.
    slider->SetFloatData(0, 0.5f);
    slider->SetListData(0, "wobble");
    CHECK(slider->GetGuiValue() == std::to_string(0.5f));
    number->SetListData(0, "wobble 9");
    CHECK(number->GetGuiValue() == "2");
    toggle->SetListData(0, "wobble");
    CHECK(toggle->GetGuiValue() == "off");
  }

  // ─── the structured shape ───────────────────────────────────────────────────

  TEST_CASE("gui protocol: a structured object reports cells and a whole state") {
    CellObject obj;

    CHECK(obj.GetGuiValueCount() == kCells);
    CHECK(obj.GuiValueIsSettable());

    obj.GetInlet(0)->SetList("0.5 0.25 0.125", YSE::T_GUI);

    // The whole state is every cell, space separated, in index order — one
    // call, one allocation, and the form `.preset` stores.
    CHECK(obj.GetGuiValue() ==
          std::to_string(0.5f) + " " + std::to_string(0.25f) + " " + std::to_string(0.125f));

    // Cell 0 of a structured object is its first cell, *not* the whole state.
    CHECK(obj.GetGuiValueAt(0) == std::to_string(0.5f));
    CHECK(obj.GetGuiValueAt(1) == std::to_string(0.25f));
    CHECK(obj.GetGuiValueAt(2) == std::to_string(0.125f));
    CHECK(obj.GetGuiValueAt(0) != obj.GetGuiValue());
  }

  TEST_CASE("gui protocol: cell reads are range-checked rather than indexing") {
    // A live SetParams can shrink a control between a host's count read and
    // its cell reads, so "past the end" is a normal outcome of a legitimate
    // poll. It must answer "" and never index.
    CellObject obj;
    CHECK(obj.GetGuiValueAt(kCells).empty());
    CHECK(obj.GetGuiValueAt(kCells + 1).empty());
    CHECK(obj.GetGuiValueAt(0xFFFFFFFFu).empty());
  }

  // ─── the write half ─────────────────────────────────────────────────────────

  TEST_CASE("gui protocol: a settable object round-trips its own GetGuiValue") {
    // This is the whole of what `.preset` needs: read one string, later send
    // it back as an ordinary list on inlet 0, and get the state back. No
    // reaching into another object's fields, so the restore runs through the
    // object's own handler on the control thread.
    CellObject obj;
    obj.GetInlet(0)->SetList("0.75 0.5 0.25", YSE::T_GUI);

    const std::string stored = obj.GetGuiValue();

    obj.GetInlet(0)->SetList("0 0 0", YSE::T_GUI);
    REQUIRE(obj.GetGuiValueAt(0) == std::to_string(0.f));

    obj.GetInlet(0)->SetList(stored, YSE::T_GUI);
    CHECK(obj.GetGuiValue() == stored);
    CHECK(obj.GetGuiValueAt(0) == std::to_string(0.75f));
    CHECK(obj.GetGuiValueAt(2) == std::to_string(0.25f));
  }

  TEST_CASE("gui protocol: 'set <index> <value>' writes one cell and leaves the rest") {
    CellObject obj;
    obj.GetInlet(0)->SetList("0.75 0.5 0.25", YSE::T_GUI);

    obj.GetInlet(0)->SetList("set 1 0.125", YSE::T_GUI);
    CHECK(obj.GetGuiValueAt(0) == std::to_string(0.75f));
    CHECK(obj.GetGuiValueAt(1) == std::to_string(0.125f));
    CHECK(obj.GetGuiValueAt(2) == std::to_string(0.25f));

    // An out-of-range cell write is dropped, not clamped onto a real cell.
    obj.GetInlet(0)->SetList("set 99 1.0", YSE::T_GUI);
    CHECK(obj.GetGuiValueAt(0) == std::to_string(0.75f));
    CHECK(obj.GetGuiValueAt(1) == std::to_string(0.125f));
    CHECK(obj.GetGuiValueAt(2) == std::to_string(0.25f));
  }

  TEST_CASE("gui protocol: the 'set' keyword disambiguates the two write forms") {
    // Why the cell form is not a bare "<index> <value>": for a two-cell
    // control the whole state and a cell write are the same two tokens. The
    // keyword settles it for every arity at once — proven here on the
    // three-cell case, where "set 0 1" must write one cell rather than being
    // read as a short whole-state list.
    CellObject obj;
    obj.GetInlet(0)->SetList("0.75 0.5 0.25", YSE::T_GUI);

    obj.GetInlet(0)->SetList("set 0 1", YSE::T_GUI);
    CHECK(obj.GetGuiValueAt(0) == std::to_string(1.f));
    CHECK(obj.GetGuiValueAt(1) == std::to_string(0.5f));
  }

  TEST_CASE("gui protocol: the write path goes through the object's own inlet") {
    // pHandle::SetListData is the host-side spelling of the same message —
    // control thread, T_GUI, the object's registered handler. `.preset` will
    // use exactly this, which is what keeps it off the audio thread and out
    // of another object's fields.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_LIST);
    REQUIRE(h != nullptr);

    h->SetListData(0, "alpha beta");
    CHECK(h->GetGuiValue() == "alpha beta");
    CHECK(h->GetGuiValueAt(0) == "alpha beta");
  }

} // TEST_SUITE("patcher")
