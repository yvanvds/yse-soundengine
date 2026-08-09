#pragma once
#include "../io/fileScheduler.h"
#include "../namedStore.h"
#include "../pObject.h"
#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief One entry of a ``.coll`` — an address and the message held at it.
     *
     *  All three strings are reserved to their capacity when the store is built
     *  and never grow, so writing one is a ``memcpy`` into storage that already
     *  exists. ``key`` always holds the address as text — for a numeric entry
     *  the decimal spelling of ``index``, kept in step by ``SetNumericKey`` so
     *  that ``delete``'s renumbering cannot leave the two disagreeing.
     *
     *  ``alias`` is the **second address** issue #695 added: the symbol a
     *  numeric entry is also reachable by, empty when it has none. Only a
     *  numeric entry can carry one — a symbol-addressed entry's address already
     *  *is* a symbol — so ``numeric`` decides whether the field means anything,
     *  exactly as it decides between ``index`` and ``key``.
     */
    struct collEntry {
      std::string key;
      std::string value;
      std::string alias;
      int index = 0;
      bool numeric = false;
    };

    /**
     *  @brief The table behind ``.coll`` — one per name, held by every ``.coll``
     *         that addresses it (issue #684).
     *
     *  Everything a ``.coll`` *holds* lives here; everything a ``.coll`` *is
     *  doing* stays on the object. The split is what makes Max's shared ``name``
     *  context work: two ``.coll notes`` share this, and each keeps its own
     *  traversal pointer, its own file names and its own send buffers. Max's
     *  ``next`` / ``prev`` are per-object cursors in exactly that sense — two
     *  patches walking one collection do not fight over a single position.
     *
     *  ### Why the store is bounded, and why it is not a COW snapshot
     *
     *  256 entries, each holding an address of at most 64 characters, a symbol
     *  alias of at most 64 more (issue #695) and a message of at most 256 — the
     *  same 256 that bounds ``.atoi`` / ``.itoa``
     *  and the patcher's own value queue
     *  (``patcherImplementation::kValueListCap``), so anything that can reach a
     *  ``.coll`` through a patch also fits in it. The whole table is allocated
     *  once, when the store is built on the control thread, and never resized: a
     *  ``store`` writes into storage that already exists.
     *
     *  That bound is what makes the object real-time safe, and it is why the
     *  literal reading of issue #494's mandate — publish a new snapshot the way
     *  ``GraphState`` is published (#226/#227) — is the wrong model here. A
     *  copy-on-write publish assumes the *writer* is the control thread. A
     *  ``.coll`` is written by whichever thread the message arrived on, and
     *  in-patcher delivery dispatches on **T_DSP**, so a ``store`` reaching this
     *  object from a rendering graph would have to allocate a replacement table
     *  on the audio thread. Bounded storage plus non-blocking exclusion gives
     *  the property the mandate is actually after — no allocation, no lock, no
     *  I/O on any path — without that.
     *
     *  Sharing sharpens the argument rather than changing it: with two ``.coll``
     *  objects on one name there is genuinely no single writer, so a seqlock is
     *  out for the same reason it is out for ``.value``, and what is left is
     *  mutual exclusion that never waits.
     */
    struct collStore {
      /**
       *  @brief Most entries the collection holds — 256.
       *
       *  The patcher's own bound (``kValueListCap``, ``.atoi``'s character
       *  ceiling). A ``store`` past it is refused rather than growing the table,
       *  since growing it would allocate on whichever thread the message
       *  arrived on.
       */
      static constexpr std::size_t MAX_ENTRIES = 256;

      /** @brief Longest address, in characters — and the bound on an alias
       *         too, since an alias is an address (issue #695). */
      static constexpr std::size_t KEY_CAPACITY = 64;

      /** @brief Longest stored message, in characters — ``kValueListCap``. */
      static constexpr std::size_t VALUE_CAPACITY = 256;

      collStore();

      // Claimed with a single exchange by readers and writers alike; the loser
      // drops. `.value`'s guard, for `.value`'s reason.
      std::atomic<bool> busy{false};

      // The table. Sized to MAX_ENTRIES when the store is built and never
      // resized; only the first `count` entries are live.
      std::vector<collEntry> entries;
      std::size_t count = 0;
    };

    /**
     *  @brief A keyed collection of messages — ``.coll`` (issues #494, #683,
     *         #684).
     *
     *  Max's ``coll``, "store and edit a collection of different messages". The
     *  general-purpose data store of Max patching: presets, note tables,
     *  mapping curves and sequences all live in one. Until this object the
     *  patcher could not hold **more than one value** anywhere — ``.value``,
     *  ``.f``, ``.i`` and ``.bucket`` each hold a single thing — so a patch that
     *  wanted a table had to spell it out as one object per entry.
     *
     *  ### The address model
     *
     *  An address is a **number** or a **symbol**, decided the way the whole
     *  patcher decides it: a token the strict ``ReadNumericToken`` reads as one
     *  whole finite number is numeric (truncated to an int, Max's "a float is
     *  converted to an int"), and anything else is a symbol. The two never
     *  collide — the address ``1`` and the address ``one`` are different
     *  entries — and a numeric address does not have to be contiguous or in
     *  order, exactly as in Max.
     *
     *  Since #695 a numeric entry may carry a **second** address as well: a
     *  symbol *alias*, which reaches the same entry. See the aliasing section
     *  below; the rule above is unchanged, and an alias lives in the symbol
     *  space rather than beside it.
     *
     *  Entries keep **storage order**, which is what makes ``dump``, ``next``
     *  and ``prev`` mean anything: Max's ``dump`` sends them "in the order in
     *  which they are stored", not in address order, and a patch that stored a
     *  sequence gets that sequence back. ``sort`` is the one message that
     *  rewrites that order, and it rewrites only the order — the addresses stay
     *  with the data they belong to, which is what separates it from ``swap``.
     *
     *  ### Reserved words, and why this object gets to have them
     *
     *  ``store``, ``insert``, ``append``, ``remove``, ``delete``, ``clear``,
     *  ``length``, ``goto``, ``start``, ``end``, ``next``, ``prev``, ``dump``,
     *  ``sub``, ``nsub``, ``nth``, ``min``, ``max``, ``sort``, ``swap``,
     *  ``merge``, ``separate``, ``renumber``, ``renumber2``, ``assoc``,
     *  ``deassoc``, ``nstore`` and ``subsym`` are read as
     *  commands when they are the first item of a message, so a symbol address
     *  spelled as one of them cannot be reached by writing it bare. That is
     *  Max's own contract for ``coll`` and not a shortcut here: ``coll`` is the
     *  one object in the family whose inlet is a *command* inlet rather than a
     *  data inlet, which is precisely why the ``.prepend`` / ``.atoi``
     *  discipline — never reserve a word on an inlet that has to carry arbitrary
     *  text — does not apply to it. The data an entry holds is never parsed for
     *  commands, only the leading item of the incoming message is; a symbol
     *  address spelled as a command word is still reachable through ``store``,
     *  ``remove`` and the rest, which take it as an argument rather than as a
     *  leading word.
     *
     *  What a bare message means, when it is not a command:
     *
     *  - a **number** (int, float, or a one-token list) recalls the entry at
     *    that numeric address;
     *  - a **symbol** recalls the entry at that symbol address — Max's
     *    ``symbol`` and ``anything`` methods, which both "retrieve a message
     *    stored at the address named by the symbol". Items after the leading
     *    symbol are ignored, as they are in Max; ``store`` is how a symbol
     *    address is *written*;
     *  - a **list whose first item is a number** stores the rest of the list at
     *    that numeric address — Max's ``list`` method, "the first value is used
     *    as the address at which to store the remaining items";
     *  - a **bang** outputs the entry at the pointer.
     *
     *  ### The outlets
     *
     *  All four of Max's, but not in Max's order. Outlet 0 is the data, outlet
     *  1 the address, outlet 2 bangs when a ``dump`` has finished, and outlet 3
     *  bangs when a ``read`` has finished — Max's first, second, fourth and
     *  **third**. The file outlet was **appended** rather than inserted where
     *  Max puts it, because ``.coll`` shipped with three outlets in #494 and
     *  moving the dump outlet from 2 to 3 would shift the cords of every patch
     *  saved since. #494 documented that promise before there was anything to
     *  keep it with; this is it being kept, and it is the rule the rest of the
     *  file-reading family (#687, #689, #691, #692) follows.
     *
     *  The address only leaves when Max says it does: on ``bang``, ``dump``,
     *  ``next``, ``prev`` and ``sub`` — "the address is sent out whenever a
     *  message out the 1st outlet is triggered by bang, dump, next, prev, or
     *  sub" — and not on a plain lookup, which answers with the data alone.
     *  Address before data, which is Max's right-to-left outlet order and the
     *  order ``.trigger`` and ``.bucket`` already fire in.
     *
     *  **Which** address leaves, for an entry that has two, is the question
     *  #695 says this object had been sidestepping, and the answer is the
     *  number. An aliased entry has a numeric address, so it reports the
     *  numeric address; the alias is a way *in*, not something the outlet
     *  announces. cyclone's one output routine reads
     *  ``if (ep->e_hasnumkey) outlet_float(...); else if (ep->e_symkey)
     *  outlet_symbol(...); else outlet_float(0)``, and its help patch annotates
     *  the second outlet after an ``assoc`` with "address is still an int, not
     *  the alias"
     *  (https://github.com/porres/pd-cyclone/blob/master/cyclone_objects/binaries/control/coll.c).
     *  No outlet was added for it: an entry's two addresses are not two events,
     *  and a fifth outlet would fire empty for every unaliased entry in a
     *  ``dump``.
     *
     *  The same routine also settles the *other* half of that question. Both
     *  Max references say ``next`` and ``prev`` send **0** out the second outlet
     *  "if the address is a symbol rather than a number", which contradicts
     *  their own Output section one screen away ("int or symbol — the address
     *  is sent out whenever..."). ``.coll`` sends the symbol, as a one-element
     *  list, and has since #494; cyclone sends the symbol too, and reserves the
     *  ``0`` for an entry that has neither address. So what looked like a
     *  deliberate departure is the reading the reference's own output table and
     *  the reference implementation share.
     *
     *  What leaves the data outlet is the stored message **in the kind it is**,
     *  the rule ``.route`` establishes: a stored ``60 100`` leaves as a list, a
     *  stored ``60`` as the int 60, a stored ``60.5`` as that float, and a
     *  stored lone symbol as a one-element list. Max instead prefixes a lone
     *  symbol with the word ``symbol``; that is Max's way of restoring an atom
     *  type this patcher does not have, and inventing a word here would put a
     *  token in the message that nothing downstream asked for.
     *
     *  ### The shared ``name`` context (issue #684)
     *
     *  Max: "Determines the named context of the ``coll`` object. All ``coll``
     *  objects that share the same name share their contents." The first
     *  creation argument is that name, and a named ``.coll`` binds to a
     *  ``collStore`` held in the patcher's shared-name registry
     *  (``AcquireNamedStore`` in ``namedStore.h``) under
     *  ``"<patcherName>.<name>"`` — the ``INTERNAL::NamedBus`` address form, so
     *  ``.coll notes``, ``.s notes`` and ``.r notes`` in one patcher speak about
     *  one word, and two patchers given the same ``patcher::name()`` share
     *  their collections exactly as they already share their sends.
     *
     *  That registry is the thing four objects deferred in turn — ``.coll``
     *  (#494), ``.bag`` (#495), ``.funbuff`` (#497) and ``.table`` (#498), the
     *  last of which wrote down why ``INTERNAL::NamedBus`` cannot do it: it is a
     *  publish/subscribe *value* bus with no storage and no way to answer "give
     *  me the object called X". It is built generically here so ``.table``'s
     *  shared values, ``.funbuff``'s ``interptab`` and the rest can adopt it
     *  without a second registry.
     *
     *  Three consequences worth stating, because a patch cannot see them:
     *
     *  - **An unnamed ``.coll`` is private**, not "shares the empty name".
     *    ``"<patcherName>."`` is a real, reachable address, so unnamed objects
     *    pooling on it would silently connect two collections a patch author
     *    never wired together. ``.value``'s rule, for ``.value``'s reason — and
     *    it is also what keeps every patch written against #494 behaving exactly
     *    as it did.
     *  - **The pointer stays per-object.** The store holds the entries; the
     *    cursor ``bang`` / ``next`` / ``prev`` walk belongs to the object, so
     *    two ``.coll`` objects on one name can traverse the same collection
     *    independently rather than dragging each other's position around.
     *  - **``refer`` is not ported.** Max's ``refer`` re-points an object at
     *    another name *from a message*, and resolving a name means taking the
     *    registry's mutex and possibly allocating. A message handler runs on
     *    whichever thread dispatched it, and in-patcher delivery dispatches on
     *    ``T_DSP``, so ``refer`` would be a mutex on the audio callback. The
     *    name is resolved once, on the control thread, in ``SetParent`` /
     *    ``PARM_PARSE`` / ``RefreshBinding``, which is the whole reason the
     *    message path is one pointer hop.
     *
     *  Max's second creation argument, ``no-search``, is accepted and inert: it
     *  suppresses Max's automatic hunt for a file named after the collection,
     *  and there is no such hunt here — it would mean reading a file at
     *  construction from a search path this engine does not have.
     *
     *  ### What persists, and who restores it
     *
     *  The **contents**, through ``DumpJSON`` / ``ParseJSON``. They cannot ride
     *  the parameter string, which is what an object was *created* with rather
     *  than what it has since been told, so ``.coll`` is the first object to use
     *  ``pObject::DumpState`` / ``RestoreState`` — a per-object state key that
     *  is written only when an object has state of its own, leaving every other
     *  object's serialised form byte for byte what it was. That is Max's "save
     *  data with patcher" flag, on by default, since a collection a patch cannot
     *  reload is a collection a patch has to rebuild by hand.
     *
     *  #684 asks who owns that save when several objects share one store. The
     *  answer is **everyone writes and only the creator reads**:
     *
     *  - every bound ``.coll`` writes the contents, because they are all reading
     *    one table and their copies are therefore identical. Nominating a single
     *    writer would mean the collection silently stopped being saved the day
     *    that one object was deleted from the patch, which is a data-loss bug a
     *    patch author could not see;
     *  - only the object that **created** the store restores into it. That is
     *    ``.value``'s rule for its ``initial`` argument, and it is what makes the
     *    round trip exact in both directions: within one patch the first
     *    ``.coll`` of a name fills the store and its siblings adopt it rather
     *    than reloading identical contents over the top, and a patcher loaded
     *    into an engine where that name is *already live* joins the running
     *    collection instead of resetting it under the patch that owns it.
     *
     *  The pointer is not saved: it is run-time position, the way ``.cycle``'s
     *  ``thresh`` and ``.bucket``'s ``freeze`` are, and a reloaded patch starts
     *  at the first entry.
     *
     *  ### Collection files (issue #683)
     *
     *  ``read``, ``readagain``, ``write`` and ``writeagain``, over the
     *  plain-text format Max uses — one ``<address>, <message>;`` record per
     *  line. The parsing and the formatting are this object's; getting the
     *  bytes to and from the disk is ``fileScheduler``'s, because a ``read``
     *  arrives on whichever thread dispatched it and that may be the audio
     *  callback. Nothing on the message path opens anything: the request is a
     *  wait-free claim on a slot, the disk work happens on the background pool,
     *  and the contents are parsed in the completion callback the patcher
     *  delivers at the top of a later block.
     *
     *  A ``read`` **replaces** what is held, as Max's does. Records past
     *  ``MAX_ENTRIES`` are dropped and the first ``MAX_ENTRIES`` kept — the
     *  same rule a ``store`` into a full collection follows, since the table
     *  cannot grow without allocating on whichever thread the message arrived
     *  on. A record whose address or message is too long is skipped and the
     *  rest of the file still loads. A file too large to fit
     *  ``fileScheduler::BYTES_CAPACITY``, or one that cannot be opened, is
     *  refused whole and outlet 3 stays silent.
     *
     *  The format is Max's and inherits its one limitation: ``,`` separates the
     *  address from the message and ``;`` ends the record, so a stored message
     *  containing either cannot round-trip through a file. Everything else
     *  does, exactly — a ``write`` followed by a ``read`` reproduces the
     *  collection entry for entry, in storage order.
     *
     *  An **aliased** entry (#695) has two addresses and still one record, and
     *  they are written in the order Max 5 gives — the only version of the
     *  reference that describes the format at all, and its paragraph is
     *  truncated mid-sentence in the served page, so the surviving half is all
     *  there is: "the format of each line is as follows: the address (an int or
     *  a symbol), any symbols associated with that address (if the address is
     *  an int), a comma (to separate the address from the data it contains),
     *  the data (anything), and a semicolon to indicate the end of each line"
     *  (https://docs.cycling74.com/max5/refpages/max-ref/coll.html; Max 7, 8
     *  and the current reference drop the paragraph entirely). So the number
     *  comes first and the symbol second:
     *
     *  ```
     *  1 one, 1.1;      an aliased entry — numeric address 1, alias "one"
     *  2, 200;          a numeric address
     *  triad, 0 4 7;    a symbol address
     *  ```
     *
     *  cyclone writes exactly that (``if (e_hasnumkey) SETFLOAT; if (e_symkey)
     *  SETSYMBOL; SETCOMMA``) and its help file states the spelling outright —
     *  "the format in which the alias is saved inside the [coll] object is:
     *  ``<int> <alias> , <data>``"
     *  (https://github.com/porres/pd-cyclone/blob/master/cyclone_objects/binaries/control/coll.c).
     *
     *  **Reading** is looser than writing, as cyclone's is: everything before
     *  the comma is a sequence of address tokens, each classified by the same
     *  reader the inlet uses, and order does not matter — ``one 1, 1.1;`` loads
     *  as the same entry ``1 one, 1.1;`` does. That is what lets a file written
     *  by real Max load whichever way round it spelled the pair, and it costs
     *  nothing, since a record with one token is the case that was already
     *  there.
     *
     *  ``read`` and ``write`` with no argument reuse the last name given, which
     *  is also all ``readagain`` and ``writeagain`` do here: Max's bare forms
     *  open a file dialog, and a headless patcher has none. ``filetype`` is
     *  consumed and does nothing for the same reason — it narrows the types
     *  those dialogs offer.
     *
     *  ### The arithmetic and reordering messages (issue #684)
     *
     *  ``sub`` / ``nsub`` replace one element inside a stored message, ``nth``
     *  reads one out, ``min`` / ``max`` scan one element position across every
     *  entry, ``sort`` reorders storage, ``swap`` exchanges two entries'
     *  addresses, ``merge`` appends to what an address already holds,
     *  ``separate`` opens a numeric gap, ``renumber`` renumbers consecutively
     *  and ``renumber2`` shifts the numeric addresses up by one. Element
     *  positions are **1-based**, Max's ``nth 75 2`` being "the second item in
     *  the list stored at address 75".
     *
     *  Several departures are worth naming because the reference does not
     *  settle them:
     *
     *  - ``renumber``'s default starting address, and what ``renumber2`` even
     *    means, are not stated in the Max 7/8 reference: ``renumber`` is
     *    "renumbers data entries as consecutive and in increasing order. The
     *    optional argument specifies the starting number address", and
     *    ``renumber2`` is the whole of "increment indices by one". Settled in
     *    #694 from two older and more explicit sources. The Max 5 reference
     *    spells the second one out as "the ``renumber2`` message increments the
     *    indices associated with the data in the ``coll`` object by one" — an
     *    increment of the addresses that are there, not a re-sequencing, so the
     *    gaps survive it
     *    (https://docs.cycling74.com/max5/refpages/max-ref/coll.html). And
     *    cyclone, the Pd library written to clone Max's objects, implements
     *    both and documents them in ``coll-help.pd``: "the renumber message
     *    affects only integer addresses and lists all of them in a consecutive
     *    order starting at a given value (default 0)" and "the renumber2
     *    message also only affects integer addresses and increments all of them
     *    by one - also starting at a given value (default 0)"; its
     *    ``collcommon_renumber`` assigns ``startkey++`` to each numeric key and
     *    its ``collcommon_renumber2`` adds one to every numeric key ``>=
     *    startkey``, both methods bound with a default argument of 0
     *    (https://github.com/porres/pd-cyclone/blob/master/cyclone_objects/binaries/control/coll.c).
     *    So ``renumber [n]`` renumbers consecutively from ``n``, default 0, and
     *    ``renumber2 [n]`` moves every numeric address at or above ``n`` up by
     *    one, default 0 — which makes it ``separate`` with a default and an
     *    at-or-above bound rather than a strictly-above one, and that bound is
     *    forced: a strictly-above ``renumber2`` would leave an entry at 0 alone
     *    and collide it with the entry that was at 1.
     *  - ``separate`` is **at or above** its argument too, so the slot it opens
     *    is the address it was given and not the one after. Both Max references
     *    say otherwise in prose — Max 8's "increments the numerical indices for
     *    all data whose index is greater than the provided"
     *    (https://docs.cycling74.com/legacy/max8/refpages/coll), Max 5's
     *    "incrementing the numerical indices for all data whose index is greater
     *    than the number" — but Max 5 also prints a before/after for the same
     *    message, and the example contradicts the sentence above it: ``separate
     *    2`` on ``0, apple; 1, banana; 2, cherry; 3, durian`` gives ``0, apple;
     *    1, banana; 3, cherry; 4, durian``, so the entry sitting *on* 2 moved
     *    and 2 is what came free
     *    (https://docs.cycling74.com/max5/refpages/max-ref/coll.html). cyclone
     *    agrees with the example rather than the prose: its ``coll_separate``
     *    is ``if(ep->e_hasnumkey && ep->e_numkey >= indx) ep->e_numkey += 1;``
     *    and ``coll-help.pd`` says "given an int address as the argument, the
     *    separate message increments numeric addresses equal and above it.
     *    Thus, it creates an open slot or a separation in the data collection"
     *    (https://github.com/porres/pd-cyclone/blob/master/cyclone_objects/binaries/control/coll.c).
     *    Settled that way in #709: a worked example and a working
     *    implementation outrank a one-line description that neither of them
     *    matches, it is the reading the message's name implies, and it makes
     *    ``separate`` exactly ``renumber2`` with a required argument — which is
     *    why there is one helper for both. (#709 quotes Max 5 as saying "equal
     *    to or greater than" for ``separate``; it does not. That wording is
     *    ``insert``'s and ``insert2``'s. The example is the real evidence.)
     *  - Both leave **symbol addresses alone**, which the Max reference does not
     *    cover but cyclone's help file states outright ("affects only integer
     *    addresses"). A symbol address has no place in a numeric sequence and
     *    rewriting one would destroy the only handle a patch has on that entry.
     *  - ``sort``'s second argument is documented as "-1 → the index is used,
     *    absent or 0 → the first item in the data, 1 or greater → that data
     *    element". Read literally, 0 and 1 name the same element, and they do
     *    here. Comparing a number against a symbol has no documented order
     *    either; numbers sort before symbols and symbols among themselves
     *    lexicographically, and an entry with no element at the position sorts
     *    first. The sort is **stable**, so entries that compare equal keep the
     *    storage order they had.
     *
     *  ``sub`` and ``nsub`` replace the element with the **rest of the
     *  message**, which is one token in every example Max gives; a replacement
     *  of several tokens widens the entry rather than being refused, because
     *  splicing text is the same operation either way and refusing it would
     *  invent a rule Max does not have.
     *
     *  ### Real-time behaviour
     *
     *  Exclusion is ``.value``'s: ``busy`` is claimed with a single
     *  ``exchange`` and whoever loses **drops** its operation rather than
     *  spinning. A ``std::mutex`` is out (nothing on an audio path blocks) and
     *  a seqlock cannot work for a writer. The guard is never held across a
     *  send: everything an outlet needs is copied into buffers reserved at
     *  construction, the guard is released, and only then does the message go
     *  out — so a patch that wires an outlet back into this object's inlet
     *  finds the store free rather than dropping its own message. ``dump``
     *  takes the guard once per entry for the same reason.
     *
     *  ``sort`` is the one message with a super-linear shape, and it is written
     *  so that the expensive half is not the copying: the sort key of every
     *  entry is read once into a fixed array, the *indices* are insertion-sorted
     *  against those keys, and the resulting permutation is applied by
     *  cycle-following with a single scratch entry — so at most one entry copy
     *  per entry, whatever the input. Nothing in it allocates, and every buffer
     *  it uses was reserved at construction.
     *
     *  ``Calculate()`` does nothing: the object is driven by its inlet, and one
     *  that emitted would re-send on every DSP tick — the rule ``.route``,
     *  ``.sel``, ``.value`` and ``.atoi`` establish.
     *
     *  ### The symbol aliases (issue #695)
     *
     *  ``assoc``, ``deassoc``, ``nstore`` and ``subsym`` — the one group that
     *  changes the address model rather than adding to it. A numeric entry may
     *  carry a symbol *alias*, and "any reference to that symbol will be
     *  interpreted as a reference to the number address": ``store``, ``remove``,
     *  ``nth``, ``sub``, ``merge`` and a bare recall all reach it by either
     *  name, because ``Find`` matches a symbol query against a numeric entry's
     *  alias exactly as it matches a symbol-addressed entry's key. Nothing else
     *  in the object learned a special case; the second address is one more
     *  field on the entry and one more branch in one function.
     *
     *  - ``assoc <symbol> <number>`` — associate, "provided that the number
     *    address already exists". A number that does not exist is not created
     *    and nothing is said.
     *  - ``deassoc <symbol> <number>`` — take the association away, leaving the
     *    entry, its number and its data alone.
     *  - ``nstore <number> <symbol> <message>`` — store and associate in one
     *    message. Both orders of the pair are accepted: Max 5's prose says
     *    "followed by a number and a symbol (or a symbol and a number)", and
     *    cyclone's help file agrees ("they can come in any order"), while Max
     *    8's argument table lists only the number-first form.
     *  - ``subsym <new> <old>`` — rename. Max 5's worked example renames a plain
     *    symbol *address* (``subsym jack jill`` turns ``jill, 40 50 60;`` into
     *    ``jack, 40 50 60;``), so this renames those as well as aliases —
     *    cyclone gets that for free by holding both in one field.
     *
     *  **One symbol reaches one entry.** That is the invariant the whole group
     *  is built on: without it ``Find``'s answer would depend on storage order,
     *  and an entry could be shadowed into being unreachable by name. Max says
     *  what gives way, and only Max 5 still says it — ``assoc``'s parenthetical,
     *  dropped from Max 7 onwards: "if the symbol was already being used as an
     *  address, or was already associated with a number address, the message
     *  that was stored at that address is removed"
     *  (https://docs.cycling74.com/max5/refpages/max-ref/coll.html). cyclone
     *  implements exactly that, colliding entry and all
     *  (``if ((ep2 = collcommon_symkey(cc, s))) collcommon_remove(cc, ep2);``),
     *  and guards the "already carries this symbol" case so that associating
     *  twice is not read as a collision with itself
     *  (https://github.com/porres/pd-cyclone/blob/master/cyclone_objects/binaries/control/coll.c).
     *  The removal is ``remove`` and not ``delete``: nothing is renumbered.
     *
     *  Three places where the reference and the reference implementation part,
     *  and what is done here:
     *
     *  - **``deassoc`` names both halves and both must match.** cyclone reads
     *    only the number — its handler opens with ``s = NULL;`` — so
     *    ``deassoc anything 1`` clears whatever alias 1 had. That discards an
     *    argument its own method signature declares, and the reference sentence
     *    is about "the association between *the* symbol and *the* number
     *    address", so an association a message did not name is left alone here.
     *  - **``subsym`` refuses a name already in use** rather than duplicating
     *    it. cyclone does not check, which leaves two entries answering to one
     *    symbol and the second unreachable by name. Removing the other entry
     *    the way ``assoc`` does is not the answer either: Max documents that
     *    removal for ``assoc`` alone, and a rename that silently takes another
     *    entry's data with it is worse than a rename that does not happen.
     *  - **A plain ``store`` at an aliased address keeps the alias.** In cyclone
     *    it does not: ``collcommon_replace`` overwrites *both* key fields from
     *    its arguments, so storing by number nulls the symbol and storing by the
     *    symbol drops the number, which makes ``nstore`` the only way to write
     *    to an aliased entry without losing half its address. No Max
     *    documentation says so, and it reads as a consequence of that function's
     *    shape rather than a decision: a plain ``store`` *is* one of the
     *    "references to that symbol" ``assoc`` promises to redirect, and it
     *    would be strange for the redirect to sever the link it just used.
     *
     *  Two smaller readings, for completeness. An alias must be a **symbol**: a
     *  numeric token is refused, since an alias spelled ``1`` would be reached
     *  by a numeric lookup that already means another entry, and #494's
     *  never-collide guarantee is the thing being preserved. And address ``0``
     *  may be aliased like any other — Max 5 alone carves it out ("except 0,
     *  which cannot have an associated symbol"), Max 7 onwards dropped the
     *  clause, and cyclone never implemented it.
     *
     *  Every message that rewrites numeric addresses — ``insert``, ``delete``,
     *  ``renumber``, ``renumber2``, ``separate`` — carries the alias along,
     *  because it lives on the entry rather than on the number, and cyclone's
     *  equivalents touch only ``e_numkey`` for the same reason. ``swap`` is the
     *  exception that proves it: it exchanges *addresses*, so the alias goes
     *  with the number to the other entry, which is what cyclone's
     *  ``collcommon_swapkeys`` does (it swaps ``e_symkey`` along with
     *  ``e_hasnumkey`` and ``e_numkey``).
     *
     *  ### Deliberately not here
     *
     *  The editor window (``open`` / ``wclose``), ``refer`` (see the naming
     *  section) and the ``embed`` / ``flags`` save switch — the contents are
     *  always saved here.
     */
    PATCHER_CLASS(gColl, YSE::OBJ::G_COLL)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(BangIn)
    _INT_IN(IntIn)
    _FLOAT_IN(FloatIn)
    _LIST_IN(ListIn)

    _PARM_CLEAR
    _PARM_PARSE

    /** @brief Most entries the collection holds — ``collStore::MAX_ENTRIES``. */
    static constexpr std::size_t MAX_ENTRIES = collStore::MAX_ENTRIES;

    /** @brief Longest address, in characters. */
    static constexpr std::size_t KEY_CAPACITY = collStore::KEY_CAPACITY;

    /** @brief Longest stored message, in characters. */
    static constexpr std::size_t VALUE_CAPACITY = collStore::VALUE_CAPACITY;

    /**
     *  @brief Longest text a ``write`` produces, in characters.
     *
     *  Every entry at its maximum, plus the ``", "`` and ``";\n"`` each record
     *  costs: the whole collection always fits, so a ``write`` can only fail on
     *  the disk rather than on its own bound. Reserved once at construction,
     *  because the message that asks for a ``write`` may be on the audio
     *  thread.
     *
     *  Re-derived for #695: a record may now carry a **second** address, so the
     *  worst case grew by an alias and the space in front of it —
     *  ``KEY_CAPACITY + 1`` per entry, taking a full collection from just under
     *  81 KiB to just under 98, still comfortably inside one file slot.
     */
    static constexpr std::size_t FILE_TEXT_CAPACITY =
        MAX_ENTRIES * (KEY_CAPACITY + 1 + KEY_CAPACITY + VALUE_CAPACITY + 4);
    static_assert(FILE_TEXT_CAPACITY <= fileScheduler::BYTES_CAPACITY,
                  "a full .coll must fit one file slot");

    /** @brief How many entries the collection holds. */
    std::size_t Count() const {
      return store->count;
    }

    /** @brief Where ``bang`` / ``next`` / ``prev`` currently point, as a
     *         position in storage order. 0 on an empty collection. */
    std::size_t Pointer() const {
      return pointer < store->count ? pointer : 0;
    }

    /** @brief The address of entry @p position in storage order, or ``""``.
     *         Diagnostics and tests: it returns a copy, so control thread
     *         only. */
    std::string KeyAt(std::size_t position) const;

    /** @brief The message stored at entry @p position in storage order, or
     *         ``""``. Same contract as ``KeyAt``. */
    std::string ValueAt(std::size_t position) const;

    /** @brief The symbol alias of entry @p position, or ``""`` when it has none
     *         (issue #695). Same contract as ``KeyAt``. */
    std::string AliasAt(std::size_t position) const;

    /** @brief The message stored at @p address, or ``""`` when nothing is.
     *         Same contract as ``KeyAt``. */
    std::string Lookup(const std::string& address) const;

    /**
     *  @brief The shared name, or empty when this ``.coll`` has a store of its
     *         own. The first creation argument (issue #684).
     */
    const std::string& CollName() const {
      return collName;
    }

    /**
     *  @brief The address the store is registered under —
     *         ``"<patcherName>.<name>"`` — or empty while the store is private.
     *
     *  Spelled ``StoreAddress`` rather than ``.value``'s ``Address``, because
     *  this object already has an ``Address`` — the resolved entry key its
     *  message path passes around — and one name cannot be both.
     */
    const std::string& StoreAddress() const {
      return boundAddress;
    }

    /** @brief Whether this object's store is shared by name rather than
     *         private. */
    bool IsShared() const {
      return !boundAddress.empty();
    }

    /** @brief The name the last ``read`` was given, which ``readagain`` and a
     *         bare ``read`` reuse. Empty until one has been. Control thread. */
    const std::string& ReadFile() const {
      return readPath;
    }

    /** @brief The same for ``write`` / ``writeagain``. Control thread. */
    const std::string& WriteFile() const {
      return writePath;
    }

    // Bind to the store the moment the patcher name is known, and build the
    // file plumbing while still on the control thread — so neither a message
    // nor a `read` arriving later on the audio thread ever resolves a name or
    // allocates a table. SetParent is only called under
    // patcherImplementation::mtx.
    void SetParent(pObject* newParent) override;

    // Re-bind after a patcher rename: the address prefix moved, so this object
    // now addresses a different store. Called from
    // patcherImplementation::SetName alongside gValue::RefreshBinding, so a
    // renamed patcher's collections re-anchor with its values, sends and
    // receives (issue #684).
    void RefreshBinding();

    // A read or write this object asked for has finished. Called on the
    // patcher's dispatch thread inside a fresh messageEventScope; parses the
    // bytes into the store and bangs outlet 3. Allocation-free, like every
    // other path into this object.
    void DeliverFileResult(const fileResult& result, YSE::THREAD thread) override;

    // The contents, into the object's "state" key of a DumpJSON (issue #494).
    // Control thread — patcherImplementation::DumpJSON holds mtx — but the
    // guard is still taken, because a message may be arriving from a rendering
    // graph while the patch is being saved.
    void DumpState(nlohmann::json::value_type& json) override;

    // The other half: called from ParseJSON on the control thread, on a
    // freshly built object the audio thread cannot see yet. A no-op on a shared
    // store this object did not create — see the persistence section.
    void RestoreState(const nlohmann::json::value_type& json) override;

  private:
    using Entry = collEntry;

    // One resolved address. `numeric` decides which of the other fields means
    // anything, and a numeric address never matches a symbolic one. Points into
    // the caller's message rather than copying it, so resolving costs nothing.
    struct Address {
      const char* text = nullptr;
      std::size_t length = 0;
      bool numeric = false;
      int index = 0;
    };

    // One entry's contribution to a `sort`, read once before any entry moves.
    // Precomputing these is what keeps the comparison loop free of repeated
    // numeric parsing: the indices are sorted against this array and the table
    // is touched only when the permutation is applied.
    struct SortKey {
      const char* text = nullptr;
      std::size_t length = 0;
      float number = 0.f;
      bool numeric = false;
      bool present = false;
    };

    /**
     *  @brief Non-blocking exclusive access to the store.
     *
     *  ``Held()`` is false when another thread had it — the caller then does
     *  nothing at all. Never waits, never allocates. The same shape as
     *  ``valueSlotGuard`` in gValue.h and for the same reason: this object is
     *  reachable from the control thread and from a rendering graph alike, a
     *  mutex is out on the second of those, and with several ``.coll`` objects
     *  on one name there is no single writer to build a seqlock around.
     */
    class storeGuard {
    public:
      explicit storeGuard(std::atomic<bool>& flag)
        : flag_(flag), held_(!flag.exchange(true, std::memory_order_acquire)) {}
      ~storeGuard() {
        if (held_) flag_.store(false, std::memory_order_release);
      }
      storeGuard(const storeGuard&) = delete;
      storeGuard& operator=(const storeGuard&) = delete;
      storeGuard(storeGuard&&) = delete;
      storeGuard& operator=(storeGuard&&) = delete;

      bool Held() const {
        return held_;
      }

    private:
      std::atomic<bool>& flag_;
      bool held_;
    };

    // Point at the store the current name and parent address, creating it if
    // this is the first .coll to name it. Control thread only (SetParams /
    // SetParent / SetName). A no-op when the address has not changed, so the
    // running contents survive a re-parse that leaves the name alone.
    void Rebind();

    // Resolve the `length` characters at `text` into an address. False for an
    // empty token, or one longer than KEY_CAPACITY — an address that cannot be
    // stored cannot be looked up either, so both ends refuse the same things.
    static bool ReadAddress(const char* text, std::size_t length, Address& out);

    // Position of the entry at `address` in storage order, or -1. Guard held.
    int Find(const Address& address) const;

    // Write `length` characters of `value` into `entry`. False when the message
    // is longer than the entry can hold — refused rather than truncated, since
    // half a message is a different message. Guard held.
    static bool AssignValue(Entry& entry, const char* value, std::size_t length);

    // Point `entry` at numeric address `index`, keeping its key text in step.
    // Leaves the alias alone: `index` is the entry's number, and every message
    // that rewrites numbers (insert, delete, renumber, renumber2, separate)
    // carries the symbol along rather than orphaning it (issue #695). Guard
    // held.
    static void SetNumericKey(Entry& entry, int index);

    // Copy `src` over `dst` without allocating: both strings were reserved to
    // capacity when the store was built, so assign() reuses storage that
    // exists. This is what lets insert, delete and sort shift the table around
    // on the audio thread. Guard held.
    static void CopyEntry(Entry& dst, const Entry& src);

    // Store `value` at `address`, replacing what was there or appending a new
    // entry at the end. False when the collection is full or the message does
    // not fit. Guard held.
    bool StoreAt(const Address& address, const char* value, std::size_t length);

    // Store `value` at numeric address `index`, taking that address off
    // whatever held it: every equal or greater numeric address goes up by one
    // and the new entry lands in front of them in storage order. False when the
    // collection is full or the message does not fit. Guard held.
    bool InsertAt(int index, const char* value, std::size_t length);

    // Drop the entry at `position`, closing the gap so storage order survives.
    // When `renumber`, every numeric address above the one removed comes down
    // by one — Max's `delete` as against its `remove`. Guard held.
    void Erase(std::size_t position, bool renumber);

    // Highest numeric address in use, or -1 when no entry has one. Guard held.
    int HighestIndex() const;

    // Take the guard, copy entry `position` into the send buffers, release it,
    // and send: the address out outlet 1 when `withAddress`, then the data out
    // outlet 0. False when there is no such entry, in which case nothing is
    // sent. The guard is deliberately not held across the send — see the class
    // documentation.
    bool Output(std::size_t position, bool withAddress, YSE::THREAD thread);

    // Send `text` out outlet `pin` in the kind it is: a list, or the int or
    // float it spells when it is a single number. `.route`'s rule, minus its
    // bang case — an entry holding nothing is still an entry, and a bang would
    // read downstream as "no data" rather than "empty data".
    void SendTyped(std::size_t pin, const std::string& text, YSE::THREAD thread);

    // The command half of the inlet. Returns false when `text` is not a
    // command at all, leaving the caller to read it as an address.
    bool HandleCommand(const char* text, std::size_t length, YSE::THREAD thread);

    // The half of HandleCommand that #684 added — the arithmetic and reordering
    // messages. Split out because one function of twenty-five branches is
    // harder to read than two of a dozen, and because these all share the
    // "tokenise the argument region" preamble. `argBegin`/`argEnd` bound the
    // already-trimmed argument region of `text`.
    bool HandleEditCommand(const char* word, std::size_t wordLength, const char* text,
                           std::size_t argBegin, std::size_t argEnd, YSE::THREAD thread);

    // Recall the entry at `address` and send its data alone — no address
    // outlet, which is Max's rule for a plain lookup.
    void Recall(const Address& address, YSE::THREAD thread);

    // ─── the edit messages (issue #684) ────────────────────────────────────
    //
    // All of these run with the guard held and none of them allocates.

    // Replace the 1-based element `position` of the entry at `position` in
    // storage order with the `length` characters at `data`. False when there is
    // no such element or the result would not fit the entry. Guard held.
    bool Substitute(std::size_t at, int position, const char* data, std::size_t length);

    // Append `length` characters of `data` to the entry at `at`, separated by a
    // space when it already holds something. False when the result does not
    // fit. Guard held.
    static bool MergeInto(Entry& entry, const char* data, std::size_t length);

    // Copy the 1-based element `position` of `value` into `sendValue`. False
    // when `value` has no such element. Guard held.
    bool CaptureElement(const std::string& value, int position);

    // Scan element `position` of every entry and copy the lowest (or highest)
    // numeric one into `sendValue`. False when no entry has a number there.
    // Guard held.
    bool CaptureExtreme(int position, bool wantMax);

    // Reorder storage. `ascending` is Max's first argument (-1) against its
    // second (1); `element` is Max's second — -1 for the address, 0 or 1 for
    // the first element of the data, n for the nth. Guard held.
    void Sort(bool ascending, int element);

    // Whether sort key `a` comes before sort key `b`. Numbers before symbols;
    // an absent element before both.
    static bool SortKeyLess(const SortKey& a, const SortKey& b);

    // Exchange the addresses of the entries at `a` and `b`, leaving the data
    // where it is — Max's swap. Guard held.
    void SwapAddresses(std::size_t a, std::size_t b);

    // Every numeric address at or above `first` goes up by one, which opens a
    // slot at `first` itself. This is both Max's renumber2 — which increments
    // the addresses it finds rather than re-sequencing them — and Max's
    // separate; the two messages differ only in their argument defaulting.
    // Guard held.
    void Increment(int first);

    // Give every numeric entry a consecutive address in storage order, starting
    // at `first`. Symbol addresses are left alone: they have no place in a
    // numeric sequence, and rewriting them would destroy the only way a patch
    // can reach those entries. Guard held.
    void Renumber(int first);

    // ─── the symbol aliases (issue #695) ───────────────────────────────────

    // Position of the entry the `length` characters at `text` already reach —
    // as a symbol address, or as the alias of a numeric one — or -1. `except`
    // is a storage position to skip, so re-associating the symbol an entry
    // already carries is not read as a collision with itself; pass -1 to search
    // every entry. This is `Find` restricted to the symbol half, and it exists
    // separately because `assoc` has to know *which* entry a symbol collides
    // with, not merely that the lookup would succeed. Guard held.
    int FindSymbol(const char* text, std::size_t length, int except) const;

    // Give `entry` the alias spelled by the `length` characters at `text`,
    // replacing whatever it had. The buffer was reserved when the store was
    // built, so this is an assign() into storage that exists. Guard held.
    static void SetAlias(Entry& entry, const char* text, std::size_t length);

    // Associate the symbol at `text` with the numeric address `index`, which is
    // Max's `assoc` and the second half of its `nstore`. An entry the symbol
    // already reaches is **removed** first — Max's own parenthetical, see the
    // aliasing section of the class documentation. False when there is no
    // numeric entry at `index` or when the symbol is not a symbol at all (a
    // numeric token would collide with the number space). Guard held.
    bool Associate(int index, const char* text, std::size_t length);

    // The alias half of the inlet: assoc, deassoc, nstore and subsym. A third
    // split for the reason there was a second — these all reason about a
    // symbol *and* a number, which none of the commands above do.
    bool HandleAliasCommand(const char* word, std::size_t wordLength, const char* text,
                            std::size_t argBegin, std::size_t argEnd);

    // What a completion carries back, so a read and a write can be told apart
    // in DeliverFileResult. Private to this object — the tag means nothing to
    // the scheduler.
    static constexpr int FILE_TAG_READ = 0;
    static constexpr int FILE_TAG_WRITE = 1;

    // The read / write half of the inlet. `name` is the argument the message
    // carried, or null for the `again` forms and for a bare `read` / `write`,
    // both of which reuse the last name given. False when there is nothing to
    // do — no patcher, no name yet, a name that does not fit, or a file table
    // that is full — in every case silently, since this may be the audio
    // thread.
    bool RequestFile(FILE_OP op, const char* name, std::size_t length);

    // Format the whole collection into `fileScratch` in Max's plain-text form,
    // one `<address>, <message>;` record per line. Takes the guard; allocates
    // nothing, because the scratch was reserved to FILE_TEXT_CAPACITY at
    // construction. False when the guard was held elsewhere.
    bool Serialize();

    // The other direction: replace the contents with the records in the
    // `length` bytes at `text`. Takes the guard; allocates nothing. False when
    // the guard was held elsewhere, in which case nothing was changed.
    bool LoadFrom(const char* text, std::size_t length);

    // Max's name argument — the shared context. Empty means a private store.
    std::string collName;

    // Max's `no-search` second argument. Accepted so a patch brought across
    // from Max builds, and read for nothing at all: it suppresses Max's hunt
    // for a file named after the collection, and there is no such hunt here.
    int noSearch = 0;

    // The address `store` is registered under, or empty while it is private.
    // Also the "has the binding changed?" key Rebind() compares against.
    std::string boundAddress;

    // The table. Never null once the object has been constructed, so no message
    // handler needs a null check.
    std::shared_ptr<collStore> store;

    // Whether this object is the one that brought `store` into existence — the
    // only one that restores saved contents into it. See the persistence
    // section of the class documentation.
    bool createdStore = true;

    // Where bang / next / prev point, in storage order. Per-object rather than
    // per-store, so two .coll objects on one name traverse it independently;
    // run-time state rather than a parameter, so it does not survive a save —
    // `.cycle`'s `thresh` and `.bucket`'s `freeze` are the same kind of thing.
    std::size_t pointer = 0;

    // What a send is made from, reserved at construction. Copies rather than
    // the entry itself: the send path is synchronous, so handing an outlet the
    // stored string would let a patch that writes to this object from
    // downstream mutate the very message still being fanned out.
    std::string sendValue;
    std::string sendAddress;
    bool sendNumeric = false;
    int sendIndex = 0;

    // Where `sub` / `nsub` splice an element before the result is written back
    // over the entry. Reserved to VALUE_CAPACITY at construction; a splice that
    // would overflow it is refused before a character is copied.
    std::string editScratch;

    // `sort`'s working set, all sized at construction. The keys are read once
    // per sort, the order is the permutation the insertion sort produces, and
    // `sortVisited` plus one scratch entry are what let that permutation be
    // applied by cycle-following instead of by shuffling whole entries around.
    // `scratchEntry` is `swap`'s spare key buffer too — both messages need one
    // entry's worth of room and neither can hold it across the other.
    std::vector<SortKey> sortKeys;
    std::vector<std::size_t> sortOrder;
    std::vector<char> sortVisited;
    Entry scratchEntry;

    // The last name each half of the file surface was given — what `readagain`
    // and `writeagain` reuse, and what a bare `read` / `write` falls back on
    // since there is no dialog to ask. Reserved to the scheduler's path bound
    // at construction, so remembering a name on a message path is an assign()
    // into storage that exists rather than an allocation.
    std::string readPath;
    std::string writePath;

    // Where a `write` is formatted before it is handed to the scheduler.
    // Reserved to FILE_TEXT_CAPACITY at construction for the same reason.
    std::string fileScratch;
  };

} // namespace PATCHER
} // namespace YSE
