#pragma once
#include "../pAtomList.h"
#include "../pObject.h"
#include "gArray.h"
#include "gArrayEnds.h"
#include <cstddef>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Shared body for the three converters of the ``array.*`` family —
     *         ``.array.tolist``, ``.array.tostring`` and ``.array.tosymbol``
     *         (issue #796).
     *
     *  The way out: getting an array to everything that is not an array — a
     *  list for the rest of the patcher, text for the symbol family, one
     *  token for a ``.s`` or a file name. Three objects, one render: each ask
     *  collects the store's elements into one ``AtomList`` under one hold of
     *  the store's guard, and the three differ only in what leaves the outlet
     *  afterwards, which is why the whole family is one file and the
     *  difference is one virtual ``Deliver`` — ``gThreshBase``'s arrangement,
     *  as ``gArrayEndsBase`` already applies it.
     *
     *  ### What "list", "string" and "symbol" honestly mean here
     *
     *  Max has atom types — ``array.tostring`` outputs a string *object* and
     *  ``array.tosymbol`` a symbol *atom*, both holding a serialized version
     *  of the array. **This patcher has no atom types**: a message is text,
     *  and the serialized version of an array *is* its list text — an array
     *  and the list it spells are the same thing seen twice, ``gArray``'s
     *  founding rule. So the three conversions that actually exist here are
     *  the three sends the transport supports:
     *
     *  - **``.array.tolist`` sends the array typed** — ``SendAtoms``' rule:
     *    an array of one element sends that element as the int, float or
     *    symbol it spells rather than as a list of one, and several elements
     *    leave as one list message. ``.array getvalue``'s exact answer, as
     *    its patching form: a trigger and a reference inlet in, the list and
     *    an empty bang out.
     *  - **``.array.tostring`` sends the same characters, always as text** —
     *    never retyped, because a string is characters, not a value: where
     *    ``.array.tolist``'s single numeric element leaves as the int it is,
     *    this object's leaves as the text that spells it, which is what the
     *    symbol family (``.sprintf``, ``.substitute``, ``.textedit``) works
     *    on. ``.tosymbol``'s own send discipline ("always a list, whatever
     *    arrived"), applied to an array.
     *  - **``.array.tosymbol`` sends one token** — the only reading of "a
     *    single symbol" this model can represent is a message that is a
     *    single whitespace-free token (``gSymbolBase``'s argument, inherited
     *    whole), so the elements leave butted together: ``kick .wav`` becomes
     *    ``kick.wav``, which is the file-name use the issue names. Never
     *    retyped either — a symbol is a *name*, so ``1 2`` collapses to the
     *    characters ``12``, where ``.array.join`` (whose empty-separator
     *    default butts the same way) deliberately leaves as the int 12.
     *    ``.array.join`` is the object with a separator; space-separated text
     *    is ``.array.tostring``'s and ``.array.tolist``'s answer, exactly as
     *    ``.tosymbol``'s space default already emits it.
     *
     *  ### The bound, and the two overflow rules
     *
     *  The render's bound is ``AtomList::TEXT_CAPACITY`` — what a cord
     *  carries. For the two *list-text* converters a longer array loses its
     *  tail and every lost element is a counted refusal, **exactly as
     *  ``.array getvalue`` does** (#796's own prescription): a list that lost
     *  its tail is a shorter list, and the patch can see the count. For
     *  ``.array.tosymbol`` the result is one token, and a token that lost
     *  elements is a *different name* — truncation by another name, so the
     *  ask is refused whole and counted instead, ``.array.join``'s rule
     *  (#793) for the same one-token send. The choice is written down here
     *  because #796 asks for getvalue's rule across the trio and predates
     *  #793 settling the whole-refusal rule for one-token results; the two
     *  rules are kept where each is honest.
     *
     *  ### Binding, shape and real-time behaviour
     *
     *  All of ``gArrayEndsBase``, unchanged: the array is bound from the
     *  creation argument on the control thread, an ``array <name>`` message
     *  is honoured only when it names the array already bound, and
     *  ``patcherImplementation::SetName`` re-anchors through the shared
     *  base's virtual ``RefreshBinding``. Two inlets, two outlets —
     *  ``gArrayJoin``'s shape: the trigger takes a bang or the bound array's
     *  reference (the family's gesture), the reference inlet acknowledges the
     *  bound name silently, the result outlet carries the answer and the
     *  empty outlet bangs for an empty or unnamed (private) array — "no
     *  data" is a state a patch must be able to route on, not an error.
     *
     *  ``Calculate()`` does nothing — the object is driven by its inlet, the
     *  family's rule. No message path allocates, locks or blocks: the whole
     *  collect happens under one hold of the store's guard into storage
     *  reserved at construction, the send after release, and refusals are
     *  counted (``Dropped()``), never logged.
     */
    class gArrayConvertBase : public gArrayEndsBase {
    public:
      /**
       *  @brief Characters of list text an ask renders whole —
       *         ``AtomList::TEXT_CAPACITY``, the bound every rendered list
       *         send already has. Past it the list-text converters lose the
       *         tail (counted) and the one-token converter refuses whole.
       */
      static constexpr std::size_t TEXT_CAPACITY = AtomList::TEXT_CAPACITY;

      void BangIn(int inlet, YSE::THREAD thread);
      void ListIn(const std::string& value, int inlet, YSE::THREAD thread);

    protected:
      // `typedOutlet` is whether the result outlet retypes — ANY for
      // .array.tolist, whose single element leaves as the value it spells;
      // LIST for the two that only ever send text.
      explicit gArrayConvertBase(bool typedOutlet);

      // What a bang and the reference gesture both come down to: collect the
      // elements into `emitList` under one hold of the store's guard, release,
      // then hand what leaves the outlet to the subclass. `lost` is how many
      // elements did not fit the render — see the class notes on the two
      // overflow rules.
      void Ask(YSE::THREAD thread);

      // The send itself — the one thing the three objects differ in. Runs
      // after the guard is released, so it may drive the whole downstream
      // graph, including writes into this same array.
      virtual void Deliver(std::size_t lost, YSE::THREAD thread) = 0;

      // The collected elements, filled under the guard and sent after it is
      // released — gArray's `emitList`, for gArray's reason: it carries the
      // patcher's own bound on how much list text may travel down a cord.
      AtomList emitList;

      // Render buffer for the result outlet, reserved to
      // AtomList::RENDER_CAPACITY at construction.
      std::string emitScratch;
    };

    /**
     *  @brief Output an array as a list — Max's ``array.tolist`` on the
     *         name-addressed value model ``.array`` settled (issue #796).
     *
     *  The array as the message it spells, typed: one element leaves as the
     *  int, float or symbol it is, several as one list message —
     *  ``.array getvalue``'s answer as its patching form. Read
     *  ``gArrayConvertBase`` for the trio's shape, bounds and rules.
     */
    class gArrayToList : public gArrayConvertBase {
    public:
      gArrayToList();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_TOLIST;
      }
      CREATE(gArrayToList)

    protected:
      void Deliver(std::size_t lost, YSE::THREAD thread) override;
    };

    /**
     *  @brief Output an array as a string — Max's ``array.tostring`` on the
     *         value model ``.array`` settled (issue #796).
     *
     *  The array as the characters it spells: the same space-separated list
     *  text ``.array.tolist`` renders, sent always as text and never retyped
     *  — a single numeric element leaves as the text that spells it, which is
     *  what the symbol family works on. Read ``gArrayConvertBase`` for the
     *  trio's shape, bounds and rules.
     */
    class gArrayToString : public gArrayConvertBase {
    public:
      gArrayToString();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_TOSTRING;
      }
      CREATE(gArrayToString)

    protected:
      void Deliver(std::size_t lost, YSE::THREAD thread) override;
    };

    /**
     *  @brief Output an array as a single symbol — Max's ``array.tosymbol``
     *         on the value model ``.array`` settled (issue #796).
     *
     *  The elements butted together into one whitespace-free token — the only
     *  reading of "a single symbol" this patcher has — sent as text and never
     *  retyped: a symbol is a name, wherever a ``.s``, a ``.route`` or a file
     *  name wants one. A result that cannot leave whole is refused whole, a
     *  partial name being a different name. Read ``gArrayConvertBase`` for
     *  the trio's shape, bounds and rules, and ``.array.join`` for the
     *  separator this object deliberately does not take.
     */
    class gArrayToSymbol : public gArrayConvertBase {
    public:
      gArrayToSymbol();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_TOSYMBOL;
      }
      CREATE(gArrayToSymbol)

    protected:
      void Deliver(std::size_t lost, YSE::THREAD thread) override;
    };

  } // namespace PATCHER
} // namespace YSE
