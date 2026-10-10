#pragma once
#include "../pObject.h"
#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Non-blocking exclusive access to a ``.textedit``'s text cell.
     *
     *  ``.value``'s ``valueSlotGuard`` and ``.coll``'s ``storeGuard``, with the
     *  one addition this object needs: a **mode**. Both existing guards are
     *  try-locks whose loser drops, which is the only thing an audio path may
     *  do; the host-thread GUI read cannot afford to drop (see
     *  ``gTextEdit::GetGuiValue``), so it waits instead. Making the choice an
     *  argument puts the asymmetry at every call site rather than in a comment:
     *  every message path constructs this with ``Try``, and exactly one reader
     *  constructs it with ``Wait``.
     *
     *  ``Wait`` terminates because the critical section it waits on is a bounded
     *  ``memcpy`` over at most ``TEXT_CAPACITY`` bytes with no allocation, no
     *  lock, no system call and no outlet send inside it — the holder is always
     *  a few dozen nanoseconds from releasing, and can only be delayed by the
     *  scheduler, never by another lock. It is still a wait, which is why it is
     *  spelled out rather than hidden: nothing on an audio path may use it.
     */
    class textCellGuard {
    public:
      enum class mode {
        Try, //!< The loser drops. The only form a message path may use.
        Wait //!< The loser waits. Host thread only.
      };

      textCellGuard(std::atomic<bool>& flag, mode how) : flag_(flag) {
        if (how == mode::Wait) {
          while (flag_.exchange(true, std::memory_order_acquire)) {
            // Spin. See the class comment for why this terminates.
          }
          held_ = true;
        } else {
          held_ = !flag_.exchange(true, std::memory_order_acquire);
        }
      }
      ~textCellGuard() {
        if (held_) flag_.store(false, std::memory_order_release);
      }
      textCellGuard(const textCellGuard&) = delete;
      textCellGuard& operator=(const textCellGuard&) = delete;
      textCellGuard(textCellGuard&&) = delete;
      textCellGuard& operator=(textCellGuard&&) = delete;

      /** @brief False only after a ``Try`` that lost; the caller then does
       *         nothing at all. */
      bool Held() const {
        return held_;
      }

    private:
      std::atomic<bool>& flag_;
      bool held_ = false;
    };

    /**
     *  @brief An editable string value — ``.textedit`` (issue #560).
     *
     *  Max's ``textedit``, "enter and output text as a symbol or list". The
     *  patcher can already *manipulate* text — ``.sprintf``, ``.combine``,
     *  ``.tosymbol`` / ``.fromsymbol``, ``.regexp``, ``.route`` — and until now
     *  it had no way of *receiving* any. A name, a file path, a bus address or
     *  a label had to be baked into a creation argument, which means a patch
     *  could not be pointed at a different file without being rewritten. This
     *  is the input side of the whole symbol family.
     *
     *  ### The mutable string, and how it crosses the thread boundary
     *
     *  This is the object the rest of the GUI family routed around, so the
     *  reasoning is stated here in full.
     *
     *  ``.umenu`` / ``.radiogroup`` / ``.tab`` (#556) and ``.led`` /
     *  ``.textbutton`` (#557) all hold strings, and all made them **immutable
     *  creation parameters** for the reason ``Parameters`` itself gives:
     *  "strings cannot be written allocation-free and are read on both threads
     *  once the object is published". Changing one is therefore a ``SetParams``,
     *  which replaces the object through the #234 graph swap. That escape is not
     *  available here — a text field the host cannot type into is not a text
     *  field — so the value has to be genuinely mutable and genuinely readable
     *  from both threads.
     *
     *  It is worth being precise about *who* writes it, because the obvious
     *  answers all assume a single writer and none of them survives:
     *
     *   - the **host** writes it, on the control thread, when the user types
     *     (``pHandle::SetListData``, which is #551's write half);
     *   - a **patch** writes it, down a cord, and in-patcher delivery
     *     (``PassData``) dispatches on **T_DSP** — so "the audio thread stores
     *     into a ``.textedit``" is the ordinary case, not an exotic one;
     *   - the **host** reads it every frame through ``GetGuiValue()``;
     *   - the **audio thread** reads it whenever a bang arrives down a cord and
     *     the stored text goes out the outlet.
     *
     *  Four options were weighed:
     *
     *   - **A fixed inline buffer with an atomically published length.** Not
     *     enough on its own. The length is then atomic but the *characters* are
     *     still written and read concurrently — a data race in the memory model,
     *     and on the wire a string spliced out of two different edits. pObject.h
     *     permits a torn *frame* for a repaint; this would be a torn *value*,
     *     and it would leave down the outlet as a message.
     *   - **A seqlock**, in the manner of ``.multislider``'s never-resized bank.
     *     Wrong way round. The audio thread would be the *reader*, and a seqlock
     *     reader spins until the writer's sequence settles — so a host preempted
     *     mid-edit stalls the audio callback. Worse, a seqlock needs one writer
     *     and this object has two.
     *   - **A double buffer with an atomically published index.** Same fatal
     *     objection: it is a single-writer structure. With the host and a cord
     *     both writing, one can reuse the buffer the other has just published.
     *   - **Declaring the value control-thread-only** and giving the audio
     *     thread nothing to read. ``_NO_CALCULATE`` is right and is used, but it
     *     does not make the audio thread go away: a bang arriving down a cord
     *     dispatches on T_DSP and *must* read the stored text to emit it. The
     *     only way to keep the audio thread out is a ``.textedit`` a patch
     *     cannot bang, which is not a text field either.
     *
     *  **So: a fixed-capacity character cell, written in place and never
     *  resized, under mutual exclusion that never waits on an audio path.**
     *  ``.value`` (#486) and ``.coll`` (#494) already settled exactly this for
     *  exactly these reasons and this object copies their answer rather than
     *  inventing a third: ``busy`` is claimed with one ``exchange``, and on a
     *  message path whoever loses **drops** its store or its emit rather than
     *  spinning. The window is a bounded ``memcpy``, so a loss needs two threads
     *  inside the same handful of nanoseconds on the same object.
     *
     *  The one place this object goes further than ``.value`` is
     *  ``GetGuiValue()``, which **waits** rather than dropping — see there.
     *
     *  Text longer than ``TEXT_CAPACITY`` is **refused**, and what is stored is
     *  kept: half a string is a different string, and a path that may be the
     *  audio thread cannot grow the cell. Silently on the inlet, loudly on the
     *  creation argument, which is control-thread only.
     *
     *  ### Nothing is a keyword
     *
     *  Every other object in this family reserves words on inlet 0 — ``set``,
     *  ``clear``, a mode name. **This one reserves none**, and that is a
     *  property of what it holds rather than a stylistic choice: any word that
     *  is a keyword is a word the field cannot contain, and a text field that
     *  silently swallows the text you typed into it is worse than one missing a
     *  feature. So:
     *
     *   - Max's ``set <text>`` (set without output) is not ported. ``.led`` /
     *     ``.textbutton`` refused it because ``set`` is spoken for by #551;
     *     here it would additionally eat any message beginning with the word.
     *   - Max's ``clear`` is not ported: the empty list is the clear, and it is
     *     unambiguous.
     *   - Max's ``outputmode`` is not ported, both because a keyword message
     *     would eat text and for the deeper reason below.
     *
     *  ### Why there is no symbol/list output mode
     *
     *  Issue #560 proposes Max's output mode — a symbol, or a list of words —
     *  as a parameter. **The distinction does not exist in this patcher**, and
     *  ``.tosymbol`` (#490) already had to settle that: "Max has atom types: a
     *  symbol is one atom, a list is several... This patcher has no atom types.
     *  A message is carried as text, and every object that reads a message apart
     *  splits that text on whitespace." So a symbol here *is* a message that
     *  happens to be one whitespace-free token, and ``hello world`` sent as a
     *  symbol and sent as a list of two words are the same bytes on the same
     *  outlet. A mode switch would be a parameter that changed nothing.
     *
     *  What a patch actually wants when it asks for "as a symbol" — one token a
     *  ``.route`` or a ``.forward`` will take as a name — is the token collapse,
     *  and the object that performs it is ``.tosymbol`` with a separator, one
     *  cord downstream. That is the pairing issue #560 names in its own use
     *  case, so nothing is lost by not duplicating it here. Max's numeric output
     *  modes are covered the same way by ``.fromsymbol``, which restores an int
     *  or a float from a single numeric token.
     *
     *  ### The grammar
     *
     *  One inlet, hot, and everything on it emits — the patcher's rule for a hot
     *  inlet, which ``.rslider`` states. There is no cold inlet, so there is no
     *  silent write; a host that wants to change the field without the patch
     *  hearing about it is asking for the one thing a headless control cannot
     *  offer, since the outlet is the only way a change reaches anything.
     *
     *   - a **list** is the text, taken **verbatim**, whatever it says. This is
     *     both the natural behaviour of a text field and #551's whole-state
     *     write, and here the two are the same operation.
     *   - an **int** or a **float** is the text that spells it, written with
     *     ``ExprFormatValue`` — the patcher's only number-to-text writer, so a
     *     ``.f`` wired into a ``.textedit`` fills it with the number a patch
     *     author would have typed, and a float keeps its decimal point.
     *   - a **bang** re-emits what is stored without changing it. Max's bang.
     *
     *  **Outlet 0** carries the text as a list. It is *not* re-typed on the way
     *  out: a field holding ``440`` emits the text ``440`` and not the int,
     *  because the object holds text and a patch that wants the number puts a
     *  ``.fromsymbol`` after it — the same division of labour as the mode above.
     *
     *  ### The creation argument
     *
     *  One ``LIST`` parameter: the text the field starts at, every argument
     *  joined with single spaces, so ``.textedit some/default/path`` and
     *  ``.textedit hello world`` both do what they look like. There is no
     *  leading keyword and therefore none of the one-position trap ``multi``
     *  (#556) and ``momentary`` (#557) carry — which is the same "nothing is a
     *  keyword" rule as on the inlet, applied to the arguments.
     *
     *  A live ``SetParams`` on a published object is a #234 rebuild, so changing
     *  the argument replaces the object and the field comes back at its new
     *  initial text rather than keeping what was typed. That is honest: the
     *  argument is what the *patch* says the field starts at, and a host that
     *  wants to preserve a typed value across a re-parse has it in
     *  ``GetGuiValue()`` and can push it straight back in.
     *
     *  ### The GUI value protocol (issue #551)
     *
     *  One cell, so ``GetGuiValueCount()`` and ``GetGuiValueAt()`` come from
     *  pObject by construction and only ``GetGuiValue()`` is overridden —
     *  ``_HAS_GUI_SETTABLE``.
     *
     *  ``GuiValueIsSettable()`` is **true**, and more exactly true than anywhere
     *  else in the family: the protocol asks that inlet 0 accept back the string
     *  ``GetGuiValue()`` produced, and this object is the identity function on
     *  its own state, so the round trip holds for every string it can ever
     *  report, not merely for the ones it can spell.
     *
     *  **The ``set <index> <value>`` half is deliberately not offered**, and
     *  pObject.h's protocol block carries the carve-out. In short: the
     *  disambiguation argument for the ``set`` keyword is stated there in terms
     *  of *numeric* cells — "a leading token no numeric cell can ever hold" —
     *  and a text cell can hold it. Honouring it here would mean a text field
     *  could not contain the text ``set 0 hello``, in exchange for nothing at
     *  all: this control has one cell, so the whole-state write and the cell
     *  write are the same write with the same string.
     *
     *  ### What persists
     *
     *  The parameter, and nothing else — no ``DumpState`` override, pObject.h's
     *  rule. A reload brings back the field the patch was written with. The form
     *  a live value is stored in is ``GetGuiValue()``, which is what ``.preset``
     *  is for and what inlet 0 takes back. Restoring one **emits**, because
     *  inlet 0 is hot; ``.matrixctrl`` made the same call for the same reason,
     *  and it is what carries a restored value on into the rest of the patch.
     *
     *  ### Real time
     *
     *  ``Calculate()`` does nothing: the object is driven by its inlet, and one
     *  that emitted would re-send the text on every DSP tick — the rule
     *  ``.value``, ``.matrixctrl`` and the whole routing family establish.
     *
     *  No message path allocates, locks or blocks. A store is a bounded
     *  ``memcpy`` into a cell built at construction; an emit copies out under
     *  the guard into ``emitScratch``, reserved to ``TEXT_CAPACITY`` on the
     *  control thread, and sends **after** releasing the guard — ``.value``'s
     *  discipline, and it is not cosmetic: a send runs the whole downstream
     *  graph, which may well write back into this same object, and inside the
     *  guard that write would be the one thing the try-lock drops. Numbers are
     *  spelled into a stack buffer through ``ExprFormatValue``, which touches
     *  neither the heap nor the locale.
     *
     *  ### Deliberately not here
     *
     *  Everything about drawing and the keyboard, which issue #560 names a
     *  non-goal: every colour, font, size and alignment attribute, ``keymode``,
     *  ``readonly``, ``wordwrap``, ``autoscroll``, ``selectall``, ``setfocus``,
     *  and the focus-change outlet, which reports a mouse event a headless
     *  patcher does not have.
     *
     *  Max's ``textbox`` is not registered as a second name. It is the same
     *  value model under a newer name — not a second *rendering* of it — so it
     *  fails the test the #556 and #557 families are built on: two names earn
     *  their place when a host draws them differently, and here it would only
     *  give the same widget two spellings.
     */
    PATCHER_CLASS(gTextEdit, YSE::OBJ::G_TEXTEDIT)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(EmitStored)
    _INT_IN(StoreInt)
    _FLOAT_IN(StoreFloat)
    _LIST_IN(StoreList)

    _PARM_CLEAR
    _PARM_PARSE

    _HAS_GUI_SETTABLE

    /**
     *  @brief Longest text the field holds, in characters.
     *
     *  256 — ``patcherImplementation::kValueListCap``, the bound the patcher's
     *  own value queue carries a list payload in and the same one ``.value`` and
     *  ``.coll`` hold their payloads at. So anything that can reach a
     *  ``.textedit`` through a patch fits in one, and the ceiling is the
     *  patcher's rather than this object's.
     */
    static constexpr std::size_t TEXT_CAPACITY = 256;

    /**
     *  @brief What the field holds right now.
     *
     *  Returns a copy, so it allocates for anything past the small-string
     *  buffer: host thread only, and never on a message path. Waits for the
     *  cell rather than dropping — see ``GetGuiValue()``, which is this.
     */
    std::string Text();

  private:
    // Replace the text with `length` characters of `text`. Returns false when
    // the cell was held by another thread, or when the payload is longer than
    // the cell — refused rather than truncated, and silently, since this may be
    // the audio thread.
    bool StoreText(const char* text, std::size_t length);

    // Copy the stored text out under the guard and send it *after* releasing —
    // see the real-time section. A lost guard emits nothing, which is the same
    // answer `.value` gives a bang it could not serve.
    void Emit(YSE::THREAD thread);

    // Store and then emit, which is what every message on the hot inlet does.
    void StoreAndEmit(const char* text, std::size_t length, YSE::THREAD thread);

    // The creation argument, as tokens. Control thread only: written by
    // Parameters::Set, read by ParseParams(), never by a message handler.
    std::vector<std::string> initial;

    // Claimed with a single exchange by readers and writers alike; on a message
    // path the loser drops. Excluding readers from each other is unnecessary but
    // costs one atomic and keeps the invariant a single sentence.
    std::atomic<bool> busy{false};

    // The cell. Both fields are touched only while `busy` is held. Built to its
    // full width at construction and never resized, which is the whole of the
    // real-time argument: a store is a memcpy into storage that already exists.
    char cell[TEXT_CAPACITY + 1] = {};
    std::size_t cellLength = 0;

    // Read-out buffer for the send path, reserved to TEXT_CAPACITY at
    // construction so copying the cell out never allocates — the
    // `patcherImplementation::listScratch_` discipline, which lets the outlet be
    // handed the `const std::string&` it wants for free.
    std::string emitScratch;
  };
} // namespace PATCHER
} // namespace YSE
