#pragma once
#include "../pObject.h"
#include "gRandomSource.h"
#include "gTransitionTable.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Weighted transition table, walked — ``.prob`` (issue #456).
     *
     *  A first-order Markov chain as a patcher object. The table is built by
     *  three-number lists, ``<from> <to> <weight>``: "there is a weight of 4 in
     *  going from state 1 to state 2" is the list ``1 2 4``. A bang then makes
     *  one weighted jump from the current state and emits where it landed,
     *  which becomes the current state for the next bang.
     *
     *  Weights are relative. For any one state the weights of its outgoing
     *  transitions are summed, and each one's share of that sum is its
     *  probability — so ``3 4 1`` out of a state means 37.5% / 50% / 12.5%.
     *  A state may transition to itself.
     *
     *  This is the generative-composition object the patcher was missing: the
     *  existing music primitives (scales, chords, motifs, the generative
     *  player) say *what* the material is, and ``.prob`` says how likely one
     *  piece of it is to follow another. It pairs with ``.anal`` (#457), which
     *  builds the same table by counting the pairs in an input stream, so a
     *  phrase can be analysed and then re-generated in its own style.
     *
     *  ### Shape
     *
     *  Inlet 0 is the hot one, inlet 1 takes a seed the way the rest of this
     *  family's right inlet does. Three outlets:
     *
     *  - 0 (int) — the state jumped to.
     *  - 1 (bang) — the current state is a **dead end**: it has no outgoing
     *    transitions, or all of them weigh zero. Outlet 0 stays silent.
     *  - 2 (list) — the answer to ``dump``, one ``<from> <to> <weight>`` list
     *    per stored entry. Max prints its dump to the console; the YSE patcher
     *    is headless, so the natural translation is an outlet — and it is the
     *    shape ``.anal`` emits, which makes a table copyable from one object
     *    to another with a patch cord.
     *
     *  ### Dead ends
     *
     *  A bang from a state with nothing to go to emits nothing on outlet 0,
     *  bangs outlet 1, and moves the current state to the ``reset`` fallback
     *  (Max's ``reset <n>``, and this object's second creation parameter). The
     *  *next* bang therefore departs from the fallback and the chain recovers
     *  by itself — but the recovery is not attempted inside the same bang.
     *  Retrying in place would mean walking from state to state looking for one
     *  with an exit, which is a loop with no bound on it; one bang is one
     *  attempt. If the fallback is itself a dead end the object simply bangs
     *  outlet 1 on every bang, which is the honest report of an empty or
     *  exhausted table.
     *
     *  The current state starts at the fallback too, so a freshly built object
     *  departs from a defined place. Together with an empty table that gives
     *  Max's documented "no output is produced if no input has been received or
     *  if the contents have been cleared" — nothing on outlet 0, a bang on
     *  outlet 1 to say why.
     *
     *  ### Messages on inlet 0
     *
     *  - ``<from> <to> <weight>`` — stores the transition, replacing whatever
     *    weight that pair had. A weight of 0 keeps the pair in the table but
     *    makes it unreachable, which is how a transition is switched off;
     *    negative weights clamp to 0. See TransitionTable for the full rules.
     *  - ``clear`` — forgets every transition.
     *  - ``dump`` — sends every stored entry out outlet 2, in insertion order.
     *  - ``reset <n>`` — sets the fallback state.
     *  - ``seed <n>`` — restarts the random sequence.
     *
     *  An int or a float on inlet 0 sets the current state **without** emitting
     *  anything, as in Max: it says where the next bang departs from.
     *
     *  ### Real-time behaviour
     *
     *  The table is a fixed member array of ``TransitionTable::CAPACITY``
     *  entries; a table edit fills a slot and publishes it with a release
     *  store, and a bang is two bounded scans over the stored entries. No
     *  allocation, no lock, no I/O and no unbounded loop on any path — in
     *  particular the weighted choice is a prefix-sum scan rather than the
     *  usual "draw again until it lands somewhere" rejection loop, whose
     *  running time has no upper bound. ``Calculate()`` does nothing at all:
     *  the object is driven by its inlets, not by the DSP tick. ``dump`` is the
     *  one exception worth naming — it formats a string per entry, so it
     *  belongs on the control thread, like every other list-sending object in
     *  the patcher.
     *
     *  ### Seeding
     *
     *  As elsewhere in this family: a non-zero seed makes the whole walk
     *  reproducible across runs, seed 0 takes an arbitrary stream from the
     *  engine generator, and state lives per object rather than in the engine's
     *  shared ``thread_local`` stream. Exactly one draw is taken per bang —
     *  including a bang that finds a dead end, so that adding or removing a
     *  transition does not shift the stream position of everything after it.
     */
    PATCHER_CLASS(gProb, YSE::OBJ::G_PROB)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(Bang)
    _INT_IN(SetInt)
    _FLOAT_IN(SetFloat)
    _LIST_IN(SetList)

    _PARM_PARSE

    _HAS_GUI

    /** @brief Stored transitions — exposed so a patch can ask how full it is. */
    int Entries() const {
      return table.Size();
    }

  private:
    // Sends every stored entry out outlet 2. Control thread: formats strings.
    void Dump(YSE::THREAD thread);

    // Creation seed. 0 means "pick a stream for me" — see the class docs.
    aInt seed{0};
    // Where the walk departs from after a dead end, and where it starts.
    aInt reset{0};
    // Where the walk is now.
    aInt current{0};

    TransitionTable table;
    RandomSource rng;
  };
}
}
