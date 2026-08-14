#pragma once
#include "../pObject.h"
#include <atomic>
#include <string>
#include <vector>

// Declares one of the two renderings of the labelled on/off control (issue
// #557). Mirrors ITEMLIST_CLASS in gItemList.h: the whole body lives in
// gLabelSwitchBase, and the constructor in gLabelSwitch.cpp only has to fill in
// the description that says how a host is expected to draw it.
#define LABELSWITCH_CLASS(className, typeName)                                                     \
  class className : public gLabelSwitchBase {                                                      \
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
     *  @brief Shared body for ``.led`` and ``.textbutton`` (issue #557) — an
     *         on/off state that carries a name.
     *
     *  Max's ``led`` ("display an on/off indicator") and ``textbutton`` ("a
     *  button or toggle carrying a text label"). The patcher already has the
     *  value: ``.b`` is a press and ``.t`` is a latch. What it has never had is
     *  a control that can *say what it is*. A host rendering a headless patch
     *  gets a grid of anonymous squares, because nothing in ``.b`` or ``.t``
     *  answers "which one is the mute?" — and that is the whole of issue #557.
     *
     *  ### Two names, one base
     *
     *  Same reasoning as the #556 selector family, and the same shape. The
     *  value model is *a named on/off* in both, and what differs is only how a
     *  host draws it — a lamp against a pressable labelled rectangle. So they
     *  share a base, and both have the same inlet, the same two outlets, the
     *  same creation arguments and the same messages. Swapping one name for the
     *  other changes what the host draws and nothing else, which is only true
     *  because there is one implementation rather than two that agree by
     *  inspection.
     *
     *  They are not one object with a display parameter (``.zl``'s answer to
     *  its own modes) for #556's reason: the rendering is not behaviour a patch
     *  drives, it never changes at run time, and a host reading the type is
     *  exactly how it learns which widget to build. Two names cost two registry
     *  rows and no divergence.
     *
     *  Max draws the difference sharper than this — its ``led`` is an indicator
     *  a click happens to toggle, its ``textbutton`` a button that may latch.
     *  Here that difference *is* the type, and the value mode below is a
     *  separate axis, offered on both.
     *
     *  ### Where the label lives, and why not the other two places
     *
     *  **The label is a creation parameter, and it never changes afterwards.**
     *  Issue #557 asks the question outright — parameter or ``guiProperties``?
     *  — so it is worth answering all three ways:
     *
     *   - **Not the GUI cells.** #551's cells are *live state* — what a host
     *     polls to draw and what ``.preset`` stores and pushes back. A label is
     *     neither: it is identical on every load, and putting it in the cell
     *     would make ``GetGuiValue()`` hand back the label on every repaint and
     *     ``.preset`` store a copy of it per snapshot.
     *   - **Not ``guiProperties``.** Two reasons, and the second is decisive.
     *     The first is #556's: this object *emits* its label on outlet 1, so it
     *     has to own it, and a map the host writes and the engine never reads
     *     cannot be the source of a message — worse, ``guiProperties`` is a
     *     plain ``std::map`` with no synchronisation, so a send path reading it
     *     while the host writes it is a data race by construction. The second
     *     is ``CopyStorageIdentity``: a #234 replacement inherits the *old*
     *     object's ``guiProperties`` wholesale, so a label mirrored there would
     *     be silently reverted by the very rebuild that was supposed to change
     *     it. There is no version of "the label lives in the map" that survives
     *     a live ``SetParams``.
     *   - **So it is the parameter string**, which is where pObject.h says
     *     "what the object was *created* with" belongs, and where ``.text``
     *     already keeps its string. Registering it as a ``LIST`` parameter (and
     *     registering the parse callbacks that resolve it) makes
     *     ``Parameters::NeedsRebuild()`` true, so a live ``SetParams`` on a
     *     published object **replaces** it through the #234 graph swap rather
     *     than rewriting a string underneath the audio thread. That is not a
     *     consolation prize; it is the reason the label can be read from any
     *     thread with no synchronisation at all, because on a published object
     *     it is immutable by construction.
     *
     *  Max's ``settext`` / ``text`` messages are not ported, for the reason
     *  ``Parameters`` itself gives: "strings cannot be written allocation-free
     *  and are read on both threads once the object is published". Retyping an
     *  object's arguments in Max recreates it too, and ``SetParams`` reproduces
     *  that exactly.
     *
     *  ### The arguments
     *
     *  One ``LIST`` parameter, read as: an optional leading ``momentary``
     *  keyword, then the label.
     *
     *  Everything after the keyword is the label, joined with single spaces, so
     *  ``.textbutton filter cutoff`` is one control called "filter cutoff"
     *  rather than two arguments. A control given no label has an empty one and
     *  still works; it is then a ``.t`` that a host draws differently, which is
     *  a thing to do deliberately rather than by accident. A label that is
     *  literally ``momentary`` cannot be the first word — the same one-position
     *  trap ``multi`` has in #556, and the same answer.
     *
     *  ### ``momentary`` — the value mode
     *
     *  Without it the control **latches**: an int or a float sets the state
     *  (0 off, anything else on) and a bang flips it. That is ``.t``.
     *
     *  With it the control is **momentary**: any input is a press, which lights
     *  the state until the next GUI poll clears it. That is ``.b``.
     *
     *  It is offered on *both* names, for #556's reason: one press against one
     *  latch is a property of the **value**, not of the drawing, and this
     *  family's premise is that the value model is shared. A flashing LED and a
     *  latching text button are both things hosts draw; refusing either would
     *  be the one place the two objects differed, for no reason a patch could
     *  see. It is a creation argument and not a message because it changes what
     *  the GUI cell *means* — a state against an event — and #551's parse
     *  callbacks make that a rebuild.
     *
     *  **Both default to latching**, which is a deliberate divergence from
     *  Max, whose ``textbutton`` defaults to button mode. The family's whole
     *  claim is that swapping the name changes only the drawing, and a default
     *  that differed per name would break that for the one patch that swapped
     *  them. A latched named on/off is also what a headless patch mostly wants;
     *  a press with no label is ``.b``, which already exists, and a press
     *  *with* one is one word away.
     *
     *  ### The grammar
     *
     *  One inlet, hot, and everything on it emits — the patcher's rule for a
     *  hot inlet, which ``.rslider`` states. A message that addresses nothing
     *  changes nothing and still emits, which is a re-send and exactly what a
     *  bang does.
     *
     *   - an **int or a float**: latching, it *is* the state, 0 off and
     *     anything else on; momentary, it is a press whatever its value, which
     *     is what ``.b`` does and what "the user clicked" has to mean when the
     *     control has no state to be set to.
     *   - a **list of one number** is the whole state, which is #551's round
     *     trip. In momentary mode this is deliberately *not* the same as an
     *     int: an int is the user pressing, a whole-state write is a host
     *     restoring the flag, and only the second one can be asked to write a
     *     0. #556 draws the same line between "the user clicked item N" and
     *     "here is the whole mask".
     *   - ``set 0 <value>`` is #551's cell write. There is one cell, so any
     *     other index addresses nothing; the value is stored absolutely.
     *   - a **bang**: latching, it flips; momentary, it presses.
     *   - **anything else** — a leading token that is not a number and not
     *     ``set`` — addresses nothing and is dropped. In particular Max's
     *     ``settext`` and ``text`` land here, deliberately: see above.
     *
     *  **Outlets**, both fired right to left — the ordering guarantee
     *  ``.trigger`` and ``.bangbang`` document — so the label is in hand by the
     *  time the state lands on a hot inlet downstream:
     *
     *   - outlet 0 (int): the state, 0 or 1. Max's only outlet on both objects.
     *   - outlet 1 (list): the label. Not in Max, and the reason this object is
     *     more than a decorated ``.t``: a bank of labelled buttons into one
     *     ``.route`` or ``.s`` needs the label to say *which* one fired, and
     *     without it every host would have to keep its own second copy of the
     *     names it drew. It is the same outlet ``.umenu`` grew for the same
     *     reason. An unlabelled control sends an empty list, which is honest —
     *     there is no name to send.
     *
     *  Both carry one sample of the state, taken at the top of ``Calculate()``:
     *  a press arriving mid-send would otherwise report two different states
     *  down two cords of the same event.
     *
     *  ### Max's ``set``
     *
     *  Max gives both objects a ``set <value>`` that changes the state
     *  *without* output. It is not ported, on two counts. ``set`` is already
     *  spoken for — it is #551's cell write, and that write emits, because a
     *  hot inlet in this patcher always calculates. And the two forms do not
     *  even coincide by accident the way ``.multislider``'s did: Max's is
     *  ``set <value>`` and the protocol's is ``set <index> <value>``, so a Max
     *  patch's ``set 1`` arrives here as a cell write with no value and is
     *  dropped rather than silently misread. A patch that wants to change the
     *  state without emitting is asking for a cold inlet, which this object
     *  does not have and which ``.t`` does not have either.
     *
     *  ### The GUI value protocol (issue #551)
     *
     *  One cell — the scalar case pObject supplies for free, so neither
     *  ``GetGuiValueCount()`` nor ``GetGuiValueAt()`` is overridden and the
     *  base's answers are correct by construction.
     *
     *  The cell is ``"0"`` or ``"1"`` and **not** ``.b`` / ``.t``'s
     *  ``"on"`` / ``"off"``: inlet 0 takes a number, so the string it hands
     *  out is the string it takes back. That promise is the point of a
     *  labelled control — ``.preset`` can store and restore a named switch,
     *  and a host can push a whole saved surface back into a patch. When this
     *  family was written the promise set it apart from ``.b`` and ``.t``
     *  entirely; #846 has since migrated ``.t`` and the other scalar controls
     *  onto the protocol (``.t`` accepts its own ``"on"`` / ``"off"`` back),
     *  while ``.b`` still answers false: it reports a word for a press it has
     *  just consumed — an event, not a state — so neither half of the promise
     *  is its to make.
     *
     *  In momentary mode the poll is **destructive**, which the protocol
     *  explicitly permits: it reports and clears a pending press in one step.
     *  The round trip still holds — the string says "a press is pending" and
     *  writing it back arms one — but a host should know that snapshotting a
     *  momentary control at the moment of a press and restoring it later
     *  *replays* that press, because inlet 0 is hot and everything on it emits.
     *  Storing an event is storing an event; the alternative would be to
     *  refuse the promise on half the family, which helps nobody.
     *
     *  ### Issue #197, and where it bites
     *
     *  Both halves of the lost-update family live here, and pObject.h's thread
     *  contract states the rule centrally: **a read-modify-write is one step**.
     *
     *   - the momentary poll reports *and clears*. Written as a load then a
     *     store, a press landing between the two is silently dropped — that is
     *     ``gButton``'s bug, and ``exchange()`` is the fix.
     *   - the latching bang *flips*. Written as a load then a store, two
     *     concurrent bangs collapse into one flip — that is ``gToggle``'s bug,
     *     and a ``compare_exchange`` loop is the fix. ``std::atomic<bool>`` has
     *     no ``fetch_xor``, hence the CAS.
     *
     *  An indicator/button pair is precisely where both bite, because a host
     *  polls it every frame while the patch drives it from the audio thread.
     *
     *  ### What persists
     *
     *  The parameters, and nothing else — no ``DumpState`` override. pObject.h's
     *  rule again: a reload brings back the label and the mode the patch was
     *  written with, and the control off. The form a host stores a live state
     *  in is ``GetGuiValue()``, which is what ``.preset`` is for and what inlet
     *  0 takes back.
     *
     *  ### Real time
     *
     *  No path allocates, locks or blocks. The label is built by the parameter
     *  callbacks on the control thread before the object is wired or published,
     *  and a message handler only ever reads it or writes one atomic bool.
     *  Outlet 1 sends the label *itself* rather than a copy of it, which is
     *  only sound because the label is immutable. The list handler compares its
     *  keyword and walks its numbers in place rather than through a ``substr``,
     *  because a whole-state list may well arrive on the audio thread down a
     *  cord.
     *
     *  ### Deliberately not here
     *
     *  Everything about drawing and the mouse: every colour, font, size,
     *  rounding and alignment attribute, ``blinktime``, ``texton`` / ``textoff``
     *  (a second label for the on state is two labels for one control — a host
     *  that wants that draws it), ``bgoncolor``, the border and outline
     *  attributes, and Max's ``mode`` attribute, which is the ``momentary``
     *  creation keyword above rather than a live message for the reason given
     *  there. The YSE patcher is headless and issue #557 names any editor
     *  representation a non-goal; how a host draws the cell it polls, and what
     *  it titles it with, is the host's business — the title is the point, and
     *  it is on outlet 1 and in the parameters where the host can reach it.
     *
     *  Max's ``ubutton`` (a transparent click area) is excluded by the issue
     *  itself: it has ``.b``'s value model and adds nothing headless.
     */
    class gLabelSwitchBase : public pObject {
    public:
      gLabelSwitchBase();

      _NO_MESSAGES
      _DO_CALCULATE

      _BANG_IN(BangIn)
      _INT_IN(IntIn)
      _FLOAT_IN(FloatIn)
      _LIST_IN(ListIn)

      _PARM_CLEAR
      _PARM_PARSE

      _HAS_GUI_SETTABLE

      /** @brief The control's name, joined with single spaces, or "" for one
       *         built with no label. Fixed for the object's lifetime — see the
       *         class comment on why that is what makes it readable from any
       *         thread. */
      const std::string& Label() const {
        return label;
      }

      /** @brief Whether an input is a press rather than a state — the
       *         ``momentary`` creation keyword. Fixed for the object's
       *         lifetime. */
      bool Momentary() const {
        return momentary;
      }

      /** @brief The state as it stands, without consuming it. Deliberately
       *         separate from ``GetGuiValue()``, which in momentary mode is a
       *         destructive poll: a test (or a second reader) that wants to
       *         *look* must not be the thing that clears the press. */
      bool IsOn() const {
        return on.load(std::memory_order_relaxed);
      }

    protected:
      // Fills in the one piece of documentation that differs per rendering; the
      // base constructor already documented the ports, the parameter and the
      // category. RT-cold — constructor use only.
      void Document(const char* summary);

    private:
      // Rebuild the label and the mode from the current creation arguments.
      // Control thread only: called from the constructor and from the parameter
      // callbacks, all of which run before the object is wired or published. A
      // *live* SetParams never reaches here on a published object — registering
      // the callbacks makes ParamsNeedRebuild() true, so #234 replaces the
      // object instead, which is what lets every other path read the label with
      // no synchronisation.
      void ShapeLabel();

      // "The user pressed it" in momentary mode: light the state and leave it
      // lit until a poll consumes it. Idempotent, which is what a press has to
      // be when several can arrive between two frames.
      void Press();

      // Write the state outright — the protocol's cell write and the
      // whole-state write, which are absolute where a bang toggles. Anything
      // but 0 is on; a NaN is not 0 and is therefore on, which it can only
      // reach here from a patch that computed one.
      void Store(float value);

      // The creation arguments, as tokens. Control thread only: written by
      // Parameters::Set, read by ParseParams(), never by a message handler.
      std::vector<std::string> creationArgs;

      // The resolved label. Built by ShapeLabel() on the control thread and
      // immutable afterwards, which is what lets outlet 1 send it rather than a
      // copy of it.
      std::string label;

      // The mode. Written only by ShapeLabel(), read by every path — a plain
      // field for `.matrixctrl`'s reason: the callbacks that write it run
      // before the object is published, and a live re-parse replaces the
      // object.
      bool momentary = false;

      // The state. Written by the host thread and by the audio thread, read by
      // both — a plain atomic, so the two never tear, and every
      // read-modify-write on it is one step (issue #197).
      std::atomic<bool> on;
    };

    LABELSWITCH_CLASS(gLed, YSE::OBJ::G_LED)
    LABELSWITCH_CLASS(gTextButton, YSE::OBJ::G_TEXTBUTTON)

  } // namespace PATCHER
} // namespace YSE
