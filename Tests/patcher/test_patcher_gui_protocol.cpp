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
    // nothing claims the write round trip it cannot honour.
    YSE::patcher p;
    p.create(2);

    const char* types[] = {YSE::OBJ::G_BUTTON,  YSE::OBJ::G_TOGGLE,  YSE::OBJ::G_INT,
                           YSE::OBJ::G_FLOAT,   YSE::OBJ::G_SLIDER,  YSE::OBJ::G_LIST,
                           YSE::OBJ::G_MESSAGE, YSE::OBJ::G_COUNTER, YSE::OBJ::G_DIAL,
                           YSE::OBJ::G_INCDEC,  YSE::OBJ::G_TEXT};

    for (const char* type : types) {
      CAPTURE(type);
      YSE::pHandle* h = p.CreateObject(type);
      REQUIRE(h != nullptr);

      CHECK(h->GetGuiValueCount() == 1u);
      // False everywhere: inlet 0 of these takes an int or a float, not the
      // display string the read produces.
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
