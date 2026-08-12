#pragma once
#include "../math/gExprEval.h"
#include "../pObject.h"
#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

// Declares one of the three renderings of the item-list control (issue #556).
// Mirrors SYMBOL_CLASS in gSymbol.h and AFFIX_CLASS in gAffix.h: the whole
// body lives in gItemListBase, and the constructor in gItemList.cpp only has to
// fill in the description that says how a host is expected to draw it.
#define ITEMLIST_CLASS(className, typeName)                                                        \
  class className : public gItemListBase {                                                         \
  public:                                                                                          \
    className();                                                                                   \
    const char* Type() const override {                                                            \
      return typeName;                                                                             \
    }                                                                                              \
    CREATE(className)                                                                              \
  };

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Shared body for ``.umenu``, ``.radiogroup`` and ``.tab`` (issue
     *         #556) — a bounded index over a named item list.
     *
     *  Max's ``umenu`` ("pop-up menu over a named item list"), ``radiogroup``
     *  ("radio buttons or check boxes") and ``tab`` ("tab control over a named
     *  item list"). The patcher can express a *number* as a control every way
     *  there is — ``.slider`` a position, ``.dial`` a value, ``.rslider`` a
     *  span, ``.multislider`` a bank, ``.kslider`` a note — and cannot express a
     *  **bounded named choice** at all. Picking a waveform, a scale, a preset
     *  slot or a routing destination is not a number that happens to run 0-N: it
     *  is one of N named things, and the name is half of what the patch wants
     *  back.
     *
     *  ### One implementation, three names
     *
     *  Issue #556 calls the family "mechanical" and it is right: the value model
     *  is *an index into an item list* in all three, and what differs is only
     *  how a host draws it — a pop-up, a column of buttons, a row of tabs. So
     *  they share a base the way ``.tosymbol`` / ``.fromsymbol`` and ``.prepend``
     *  / ``.append`` do, and every one of them has the same inlets, the same
     *  three outlets, the same creation arguments and the same messages. A patch
     *  that swaps one for another changes what the host draws and nothing else,
     *  which is only true because there is one implementation rather than three
     *  that agree by inspection.
     *
     *  They are *not* collapsed into one object with a display parameter —
     *  ``.zl``'s answer to its own thirty-one modes — because the rendering is
     *  not behaviour a patch drives. It is what the object *is*, it never
     *  changes at run time, and a host reading the type is exactly how it learns
     *  which widget to build. Three names cost three registry rows and no
     *  divergence.
     *
     *  ### Where the item names live, and why not the other two places
     *
     *  **The labels are creation parameters, and they never change afterwards.**
     *  That is the whole of the object's thread story, so it is worth stating
     *  why the other two homes were rejected:
     *
     *   - **Not the GUI cells.** Issue #551's cells are *live state* — what a
     *     host polls to draw and what ``.preset`` stores and pushes back. The
     *     labels are neither: they are static decoration, identical on every
     *     load, and putting them in the cells would make ``GetGuiValue()`` hand
     *     back the item list on every repaint and ``.preset`` store a copy of it
     *     per snapshot.
     *   - **Not ``guiProperties``.** #551 rules that map out for live state the
     *     audio thread writes, and labels are not that — but they are not host
     *     decoration either. The object *emits* the name on outlet 1, so it has
     *     to own it; a map the host writes and the engine never reads cannot be
     *     the source of a message.
     *
     *  So they are the parameter string, which is where pObject.h says "what the
     *  object was *created* with" belongs. Registering them as a ``LIST``
     *  parameter (and registering the parse callbacks that resolve them) makes
     *  ``Parameters::NeedsRebuild()`` true, so a live ``SetParams`` on a
     *  published object **replaces** it through the #234 graph swap rather than
     *  rewriting the list underneath the audio thread. That is not a
     *  consolation prize; it is the reason the labels can be read from any
     *  thread with no synchronisation at all, because on a published object they
     *  are immutable by construction.
     *
     *  Issue #556 asks for item lists "set at SetParams time **and by
     *  message**". The first half is here and the second deliberately is not.
     *  Max can afford ``append`` / ``insert`` / ``delete`` / ``clear`` because
     *  its menu items are drawn; here the list would be a string bank written on
     *  whichever thread the message arrived on and read by a host poll at the
     *  same moment, which is precisely the write ``Parameters`` itself refuses —
     *  "strings cannot be written allocation-free and are read on both threads
     *  once the object is published". Re-typing an object's arguments in Max
     *  recreates it too, and ``SetParams`` reproduces that exactly.
     *
     *  ### The arguments
     *
     *  One ``LIST`` parameter, read as: an optional leading ``multi`` keyword,
     *  then the items.
     *
     *   - **no items** — ``DEFAULT_ITEMS`` of them, named by their index;
     *   - **exactly one item, and it reads as a whole number ≥ 1** — that many
     *     items, named by their index. A one-item control is not a choice, so
     *     the single-token case is free to mean the count, which is what saves
     *     ``.radiogroup multi 8`` from having to be written ``0 1 2 3 4 5 6 7``;
     *   - **anything else** — one item per token, in order, up to ``MAX_ITEMS``.
     *
     *  Every item therefore has a name: an unnamed one is called by its index,
     *  so outlet 1 always has something to say and no path has to render a
     *  number at send time. An item named ``multi`` cannot be the first one and
     *  an item named ``set`` cannot be selected by name; both words are spoken
     *  for, and both are one position away from working.
     *
     *  ``multi`` is issue #556's "carry that as a mode", and it is offered on
     *  all three rather than on ``.radiogroup`` alone. Max only has it there
     *  (its ``nonzero`` attribute, radio buttons against check boxes), but the
     *  mode is a property of the *value* — one index against a set — not of the
     *  drawing, and the family's whole premise is that the value model is shared.
     *  A multi-select pop-up and a multi-select tab strip are both things hosts
     *  draw; refusing them here would be the one place the three objects
     *  differed, for no reason a patch could see. It is a creation argument and
     *  not a message because it changes what the GUI cells *mean*, and #551's
     *  parse callbacks make that a rebuild.
     *
     *  ### The state, and the two shapes it takes
     *
     *   - **single select** — one cell, and it holds the index. That is the
     *     scalar case pObject supplies for free, so ``GetGuiValueCount()`` is 1
     *     and ``GetGuiValueAt(0)`` is the whole state.
     *   - **multi select** — one cell per item, holding ``0`` or ``1``.
     *
     *  Either way the object also points at exactly one item — the one the last
     *  message was about — which is what outlet 0 carries. In single select that
     *  *is* the selection; in multi select it is the item that was just toggled,
     *  so a patch can act on the change rather than diffing two masks.
     *
     *  ### The grammar
     *
     *  One inlet, hot, and everything on it emits — the patcher's rule for a hot
     *  inlet, which ``.rslider`` states. A message that addresses nothing
     *  changes nothing and still emits, which is a re-send and exactly what a
     *  bang does.
     *
     *   - an **int or a float** is an item number: it selects that item, or in
     *     multi select toggles it. This is "the user clicked item N", which is
     *     what a host driving a click has to hand. Max's ``radiogroup`` reads an
     *     int as the whole bitmask in multi mode; that form is reachable here as
     *     the whole-state list, and one message meaning "an item" in one mode
     *     and "every item" in another is the sort of thing a patch has to know
     *     rather than read.
     *   - a **list of numbers** is the whole state, which is #551's round trip:
     *     one number is the index in single select, and ``itemCount`` of them
     *     are the flags in multi select.
     *   - ``set <index> <value>`` is #551's cell write. Single select has one
     *     cell, so ``set 0 <item>`` selects an item outright (it never toggles —
     *     a cell write is absolute by definition); multi select writes one flag.
     *   - a **name** — any leading token that is not a number and not ``set`` —
     *     selects the item with that name, which is Max's ``symbol`` message and
     *     half the point of naming the items in the first place. A name no item
     *     has is dropped.
     *   - a **bang** re-sends.
     *
     *  **Outlets**, and all three fire right to left — the ordering guarantee
     *  ``.trigger`` and ``.bangbang`` document — so the state and the name are
     *  in hand by the time the index lands on a hot inlet downstream:
     *
     *   - outlet 0 (int): the item the object points at. Max's ``umenu`` and
     *     ``tab`` left outlet.
     *   - outlet 1 (list): that item's name. Max's ``umenu`` and ``tab`` right
     *     outlet, and what makes this object more than a bounded ``.i``.
     *   - outlet 2 (list): the whole selection, spelled exactly as
     *     ``GetGuiValue()`` spells it, so a host reading the state and a patch
     *     receiving it downstream cannot disagree — and it is the string inlet 0
     *     takes straight back. In multi select this is the mask issue #556 asks
     *     ``.radiogroup`` for; in single select it is the index again, which
     *     keeps the three objects structurally identical whichever mode they are
     *     built in.
     *
     *  All three carry one sample of the state, taken at the top of
     *  ``Calculate()``: a selection moved mid-send would otherwise report two
     *  different choices down two cords of the same event.
     *
     *  ### Deliberately not here
     *
     *  Max's item-list messages — ``append``, ``insert``, ``delete``, ``clear``,
     *  ``setitem``, ``prefix``, ``populate`` — for the reason above: the list is
     *  a creation parameter, and a string bank written from a message path is
     *  the one thing the parameter system already refuses.
     *
     *  ``next`` / ``prev`` / Max's arrow-key stepping. Stepping a bounded number
     *  is what ``.incdec`` is, and wiring one into this inlet is one cord — an
     *  object that grew its own step messages would be a second, worse
     *  ``.incdec`` living inside a chooser.
     *
     *  Everything about drawing and the mouse: every colour, font, size,
     *  alignment, arrow and pattern attribute, ``open`` / ``showdialog``, the
     *  tab layout and orientation attributes, and Max's silent ``set`` display
     *  message (which is spoken for by the protocol's ``set`` in any case). The
     *  YSE patcher is headless and issue #556 names any editor representation a
     *  non-goal; how a host draws the cells it polls is the host's business.
     *
     *  ``ubumenu``, whose item list is a folder scan, and ``chooser``: the issue
     *  excludes both, the first because a filesystem walk is not something a
     *  patcher object does and the second because headless it is this object.
     *
     *  ### What persists
     *
     *  The parameters, and nothing else — no ``DumpState`` override. pObject.h's
     *  rule again: a reload brings back the item list the patch was written with
     *  and item 0 selected. The form a host stores a live selection in is
     *  ``GetGuiValue()``, which is what ``.preset`` is for and what inlet 0 takes
     *  back.
     *
     *  ### Real time
     *
     *  No path allocates, locks or blocks. The labels and the flag bank are
     *  built by the parameter callbacks on the control thread before the object
     *  is wired or published, and a message handler only ever reads a label or
     *  writes a flag. Outlet 1 sends the label *itself* rather than a copy of
     *  it, which is only sound because the labels are immutable. Outlet 2 fills
     *  a string reserved at construction immediately before its send rather than
     *  keeping it between sends (``.funnel``'s re-entrancy lesson). The list
     *  handler compares its keyword and its names in place and walks its numbers
     *  token by token rather than into an ``MAX_ITEMS``-wide stack buffer,
     *  because a whole-state list may well arrive on the audio thread down a
     *  cord.
     *
     *  The flags are read and written ``relaxed``: they are independent scalars,
     *  exactly as ``.multislider``'s and ``.matrixctrl``'s cells are, and the
     *  protocol explicitly permits a host to see a torn *frame* — a mask from
     *  before an edit next to a pointer from after it. A host that needs a
     *  coherent snapshot uses the bulk read, which is one call.
     */
    class gItemListBase : public pObject {
    public:
      gItemListBase();

      _NO_MESSAGES
      _DO_CALCULATE

      _BANG_IN(BangIn)
      _INT_IN(IntIn)
      _FLOAT_IN(FloatIn)
      _LIST_IN(ListIn)

      _PARM_CLEAR
      _PARM_PARSE

      _HAS_GUI_CELLS

      /**
       *  @brief Most items the list can hold — 256.
       *
       *  The patcher's list ceiling (``AtomList::MAX_ATOMS``, ``.matrix``'s and
       *  ``.matrixctrl``'s ``MAX_PORTS``), and far past any menu a person picks
       *  from. Unlike ``.multislider``'s ceiling it costs nothing until it is
       *  asked for: the items come from the creation arguments on the control
       *  thread, so the storage is exactly the item count rather than the
       *  ceiling.
       */
      static constexpr int MAX_ITEMS = 256;

      /** @brief Fewest items. A control with nothing in it has nothing to
       *         choose, draw or report. */
      static constexpr int MIN_ITEMS = 1;

      /** @brief Items with no creation arguments — **2**, the smallest list
       *         that is a choice at all. */
      static constexpr int DEFAULT_ITEMS = 2;

      /** @brief How many items the list holds. In multi select this is also the
       *         GUI cell count; in single select there is one cell whatever
       *         this is. */
      int ItemCount() const {
        return itemCount;
      }

      /** @brief Whether more than one item can be selected at once — the
       *         ``multi`` creation keyword. Fixed for the object's lifetime. */
      bool MultiSelect() const {
        return multi;
      }

      /** @brief The item the object points at: the selection in single select,
       *         the item last touched in multi select. Always a real item —
       *         bounded on the way out, the family's rule. */
      int Index() const;

      /** @brief Item @p item's name, or "" for an item the list does not have.
       *         An item given no name is called by its index. */
      const std::string& Label(int item) const;

      /** @brief Whether @p item is selected. In single select that is "it is
       *         the index"; in multi select it is that item's flag. */
      bool IsSelected(int item) const;

    protected:
      // Fills in the one piece of documentation that differs per rendering; the
      // base constructor already documented the ports, the parameter and the
      // category. RT-cold — constructor use only.
      void Document(const char* summary);

    private:
      // Rebuild the item list and the send buffer from the current creation
      // arguments. Control thread only: called from the constructor and from the
      // parameter callbacks, all of which run before the object is wired or
      // published. A *live* SetParams never reaches here on a published object —
      // registering the callbacks makes ParamsNeedRebuild() true, so #234
      // replaces the object instead, which is what lets every other path read
      // the labels with no synchronisation.
      void ShapeItems();

      // "The user touched item `item`": selects it, or toggles it in multi
      // select, and points the object at it. Ignores an item the list does not
      // have, which is the family's reading of a message with nothing to
      // address.
      void Touch(int item);

      // Write one multi-select flag outright — the protocol's cell write, which
      // is absolute where Touch() toggles. Anything but 0 selects.
      void StoreFlag(int item, float value);

      // The lowest selected item, or -1 when none is. What a whole-state write
      // points the object at, there being no single item such a message was
      // about.
      int LowestSelected() const;

      // The item whose name is exactly the `length` characters at `text`, or -1.
      // Compared in place rather than through a substr, because a list may
      // arrive on the audio thread.
      int FindLabel(const char* text, std::size_t length) const;

      // The creation arguments, as tokens. Control thread only: written by
      // Parameters::Set, read by ParseParams(), never by a message handler.
      std::vector<std::string> creationArgs;

      // The resolved item names, one per item, including the index spelling an
      // unnamed item is called by. Built by ShapeItems() on the control thread
      // and immutable afterwards, which is what lets outlet 1 send an entry
      // rather than a copy of one.
      std::vector<std::string> labels;

      // The multi-select flags, allocated exactly `itemCount` wide by
      // ShapeItems() and never touched structurally afterwards. Unused in single
      // select, where the index is the whole state. A unique_ptr array rather
      // than a vector because a vector of atomics can be neither resized nor
      // element-assigned; this one is replaced whole, and only where nothing is
      // yet reading it.
      std::unique_ptr<std::atomic<int>[]> flags;

      // The shape. Written only by ShapeItems(), read by every path — plain
      // fields for `.matrixctrl`'s reason: the callbacks that write them run
      // before the object is published, and a live re-parse replaces the object.
      int itemCount = 0;
      bool multi = false;

      // The item the object points at. Written by the host thread, read by the
      // audio thread — a plain atomic, so the two never tear. Deliberately not
      // called `index`: GetGuiValueAt's parameter is, and a member it shadowed
      // would compile.
      std::atomic<int> current;

      // The outlet-2 text, built into memory reserved by ShapeItems() so the
      // send path never allocates.
      std::string stateText;
    };

    ITEMLIST_CLASS(gUMenu, YSE::OBJ::G_UMENU)
    ITEMLIST_CLASS(gRadioGroup, YSE::OBJ::G_RADIOGROUP)
    ITEMLIST_CLASS(gTab, YSE::OBJ::G_TAB)

  } // namespace PATCHER
} // namespace YSE
