#pragma once
#include "../math/gRandomSource.h"
#include "../pObject.h"
#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief A fixed-size array of numbers, addressed by index — ``.table``
     *         (issue #498).
     *
     *  Max's ``table``, "store and edit an array of numbers". The **dense**
     *  store, and the one the patcher had no answer for at all: ``.coll`` (#494)
     *  is keyed by arbitrary addresses and keeps storage order, ``.bag`` (#495)
     *  has no keys, ``.capture`` (#496) is a tape, and ``.funbuff`` (#497) is a
     *  sparse function of a handful of points. None of them is an *array*. A
     *  wavetable, a velocity curve, a probability distribution and a step
     *  sequence all want the same thing — N slots, every one of them present,
     *  reachable by index in constant time — and that is this object.
     *
     *  The consequence that shapes everything below: because address ``i``
     *  always lives in slot ``i``, nothing this object does can *move* an entry.
     *  That is what makes a ``dump`` here safe to walk item by item where
     *  ``.funbuff``'s had to be captured whole, and what makes ``quantile`` and
     *  ``inv`` single passes rather than searches.
     *
     *  ### The size, and where Max actually keeps it
     *
     *  Issue #498 says "fixed size from the creation argument". Max does not:
     *  its one creation argument is a **name** ("the argument gives a name to
     *  the table"), and the size is the ``size`` *attribute*, defaulting to 128.
     *  The patcher has no attribute mechanism — the reason ``.capture`` declined
     *  Max's ``size`` attribute outright — so the two are reconciled rather than
     *  one being dropped: a creation-argument token that reads as a number is
     *  the size, and a token that does not is Max's name. ``.table mytable`` is
     *  then exactly Max's object, ``.table 512`` is the issue's, and
     *  ``.table mytable 512`` is both. With no argument at all the size is
     *  Max's 128.
     *
     *  The array is allocated once at ``MAX_SIZE`` on the control thread and
     *  never resized; the size only decides how much of it is live. A larger
     *  request is clamped rather than honoured, since honouring it would mean a
     *  table a message path might later have to grow. There is deliberately no
     *  ``size`` *message*: Max's is an attribute, and a resize on a message path
     *  is exactly the allocation this object is built to avoid.
     *
     *  ### Naming, and why two ``.table``s do not share contents
     *
     *  Max shares by name — "if two or more table objects share the same names,
     *  they also share the same values" — and ``refer`` re-points one table at
     *  another's data. Issue #498 proposes doing that "via the existing
     *  ``INTERNAL::NamedBus`` naming scheme rather than a new registry", and
     *  that is not something ``NamedBus`` can do. It is a *publish/subscribe
     *  value bus*: a producer publishes a value to a name and every subscriber
     *  is handed a copy. It holds no storage, answers no "give me the object
     *  called X", and owns no lifetime for shared contents. Sharing an array
     *  between objects needs precisely the shared-name **registry** the issue
     *  says not to build — the same process-wide mutable state reachable from a
     *  message path that ``.coll``, ``.bag`` and ``.funbuff`` each deferred in
     *  turn.
     *
     *  So the name is accepted, held and saved — a ``.table mytable`` brought
     *  across from Max builds the object it names, and the argument the author
     *  typed survives a save unchanged — but each ``.table`` owns its own
     *  values, and ``refer`` is not ported. The departure is stated rather than
     *  quietly approximated, because a patch relying on two tables being one
     *  store would be wrong in a way nothing downstream could see.
     *
     *  Where the naming scheme *does* fit is Max's ``send`` message — "sends the
     *  value stored at the incoming address to all receive objects with that
     *  name" — which is exactly what ``.s`` / ``.r`` already are here. That one
     *  is ported, through the same in-patcher ``PassData`` plus global-bus
     *  publish that ``.forward`` (#485) uses for a destination that arrives as a
     *  message, and with ``.forward``'s pre-reserved strings so building the
     *  address allocates nothing.
     *
     *  ### The two inlets
     *
     *  Inlet 0 is the address and is hot; inlet 1 is the value and is cold —
     *  Max's split, and ``.funbuff``'s. Max on the right inlet: "stores the
     *  value at the next index number received at the left inlet". So the same
     *  hot inlet both writes and reads, and which one happens is decided by
     *  whether a value is waiting.
     *
     *  Max confirms the one-shot rule from the other side, with a message no
     *  other object in the family has: ``cancel`` "causes table to ignore a
     *  number received in the right inlet, so that the next number received in
     *  the left inlet will output a number, rather than storing a number at that
     *  address". A waiting value that is not consumed by exactly one address
     *  would need cancelling after *every* store, which is not what that message
     *  is for.
     *
     *  A two-number list stores directly and arms nothing: Max's list method is
     *  "the second number is stored at the address (index) specified by the
     *  first number". Anything past the second item is ignored — the multi-value
     *  form is ``set``, which Max gives its own start-address argument.
     *
     *  ### Out of range stays quiet
     *
     *  An address outside ``[0, size)`` reads nothing and writes nothing, and
     *  Max's reference does not say what it does. Staying quiet is the family's
     *  rule — ``.funbuff`` sends nothing below its lowest x, ``.bag`` sends
     *  nothing when empty — and it is the safer of the two candidates here:
     *  clamping to the last address would answer a wrong question with a
     *  plausible number, which a wavetable lookup could not detect, while
     *  silence is visible immediately.
     *
     *  ### One outlet, and the one Max has that cannot exist here
     *
     *  Max has two: the left one carries "all numbers sent out by table", and
     *  the right one bangs "when the contents of a table have been changed by an
     *  edit in the graphic editing window". The YSE patcher is headless and
     *  issue #498 names the editor a non-goal, so that outlet could never fire.
     *  It is left off rather than left dead, and deliberately **not**
     *  repurposed to fire on message-driven stores: Max does not bang for those,
     *  and a table that banged on every ``set`` — or on each of 4096 slots a
     *  ``const`` touches — would be a different object, fanning out from a path
     *  that may be the audio thread.
     *
     *  Everything the remaining outlet carries is an int: stored values,
     *  addresses (``inv``, ``quantile``, ``fquantile``), the length, the sum,
     *  the extremes and the bit fields. Max's ``fquantile`` takes a float and
     *  still answers with an address.
     *
     *  ### ``quantile``, and what ``bang`` is
     *
     *  Max: ``quantile`` "multiplies the incoming number by the sum of all the
     *  numbers in the table. This result is then divided by 2^15 (32,768). Then,
     *  table sends out the address at which the sum of all values up to that
     *  address is greater than or equal to the result." That turns the array
     *  into a **probability distribution** — the issue's own use case — and it
     *  is the reason a dense store earns its place next to ``.funbuff``.
     *  ``fquantile`` is the same with the multiplier given directly in [0, 1]
     *  instead of scaled by 32768.
     *
     *  A ``bang`` is "the same as a quantile message with a random number
     *  between 0 and 32,768", so a banged ``.table`` is a weighted random draw
     *  in one object. The draw comes from the patcher's own ``RandomSource``,
     *  which is real-time safe and per object; Max's ``table`` has no seed
     *  message and neither does this, so none is added.
     *
     *  Two edges Max does not state are decided here: with a total of zero every
     *  running sum is zero and the walk answers with address 0 on the first
     *  step, and a result larger than the total (reachable with ``quantile``
     *  arguments above 32768) answers with the last address rather than nothing,
     *  since a quantile is a position in the table and the table has an end.
     *
     *  ### ``load`` mode
     *
     *  Max: "in load mode, every number received in the left inlet gets stored
     *  in the table, beginning at address 0 and continuing until the table is
     *  filled (or until the table is taken out of load mode by a ``normal``
     *  message). If more numbers are received than will fit in the size of the
     *  table, additional numbers are ignored." It is how a stream fills a
     *  wavetable without the patch having to count addresses. Words still
     *  dispatch as messages while it is on — otherwise ``normal`` could not turn
     *  it off — and a numeric list fills successive addresses item by item.
     *
     *  ### ``getbits`` / ``setbits``, and the reading chosen
     *
     *  Max numbers bit locations "0 to 31, from the least significant bit to the
     *  most significant bit" and describes the count as "how many bits **to the
     *  right of** the starting bit location should be sent out". Written out as
     *  a binary number the most significant bit is leftmost, so "to the right"
     *  means towards the less significant end, and the field is read here as the
     *  ``count`` bits **ending at and including** ``start`` — ``getbits 0 7 8``
     *  is the low byte of address 0. Max's phrasing does not settle whether the
     *  start bit is itself included; the inclusive reading is the one that makes
     *  the documented example shapes come out as whole bytes and nibbles, and it
     *  is stated here rather than left to be inferred from the code. A start
     *  outside 0..31, a count below 1, or a count reaching past bit 0 is refused
     *  whole and silently.
     *
     *  ### Storage model
     *
     *  ``.coll``'s, for ``.coll``'s reason: a fixed array allocated whole at
     *  construction plus ``.value``'s non-blocking ``busy`` guard, claimed with a
     *  single ``exchange`` by a loser that **drops** rather than spinning. Not a
     *  copy-on-write ``GraphState`` publish, which assumes the writer is the
     *  control thread — a ``.table`` is written by whichever thread its message
     *  arrived on, and in-patcher delivery dispatches on ``T_DSP``.
     *
     *  ``dump`` walks item by item, taking the guard once per address and
     *  re-reading the size each step — ``.coll``'s walk, not ``.funbuff``'s
     *  capture-the-whole-burst. The sort is what forced the capture there: a
     *  re-entrant ``set`` could insert in front of ``.funbuff``'s cursor and
     *  shift every later pair, emitting one twice and skipping another. Nothing
     *  can shift here, because address ``i`` is slot ``i`` — a re-entrant write
     *  changes a value the walk has not reached yet but never its position — so
     *  the per-item walk is sound, and it avoids the 16 KB stack burst a
     *  captured snapshot of a full table would need on a path that may be the
     *  audio thread.
     *
     *  ``Calculate()`` does nothing, for the reason it does nothing in
     *  ``.route``, ``.value``, ``.coll``, ``.bag``, ``.capture`` and
     *  ``.funbuff``: the object is driven by its inlets, and one that emitted
     *  from ``Calculate()`` would re-send on every DSP tick.
     *
     *  ### What persists
     *
     *  The contents, by default — the third of the family's three answers, and
     *  the one Max gives this object. ``.coll`` saves unconditionally, ``.bag``
     *  and ``.capture`` never, ``.funbuff`` only when asked; Max's ``table`` has
     *  ``.funbuff``'s ``embed`` flag but with the opposite default: "toggles the
     *  ability to embed the table and save its data as part of the main patch.
     *  The default behavior is 1 (save the data)." Issue #498's "contents in the
     *  JSON round trip" and Max therefore agree, and the flag is what a patch
     *  turns *off*.
     *
     *  The flag itself is always written, contents only when it is on. Writing
     *  it unconditionally is what makes ``embed 0`` survive a reload: if nothing
     *  were written the object would come back at the default and start saving
     *  itself again. ``.funbuff`` could write nothing at all because *its*
     *  default is off, so silence and "off" mean the same thing there; here they
     *  do not.
     *
     *  The pointer, the armed value and load mode are not saved. They are
     *  run-time position, as ``.coll``'s pointer and ``.bucket``'s freeze are.
     *
     *  ### Deliberately not here
     *
     *  ``read`` and ``write``, which are file I/O on a path that may be the
     *  audio thread — and with them Max's "Max looks for a file of the same name
     *  to load", which is the other half of what the name argument does there.
     *
     *  ``open`` and ``wclose``, which open and close the graphic editing window,
     *  and with them the ``range``, ``signed`` and ``notename`` attributes:
     *  all three describe how that window *draws* and constrains values. Values
     *  are plain ints here and negative ones are always storable, which is what
     *  Max's ``signed 1`` buys.
     *
     *  ``refer``, for the reason the naming section gives.
     *
     *  ``flags`` is ported by halves, which is worth saying plainly. Max: "the
     *  first argument affects the Save with Patcher option, and the second
     *  argument affects the Don't Save option". The first is ``embed`` under
     *  another name and is honoured; the second concerns whether the data is
     *  written to the table's own **file**, and there are no files here, so it
     *  is read and ignored.
     */
    PATCHER_CLASS(gTable, YSE::OBJ::G_TABLE)
    _NO_MESSAGES
    _NO_CALCULATE

    _INT_IN(IntIn)
    _FLOAT_IN(FloatIn)
    _LIST_IN(ListIn)
    _BANG_IN(BangIn)

    _PARM_CLEAR
    _PARM_PARSE

    /**
     *  @brief Most addresses the array can hold — 4096.
     *
     *  The ceiling, not the default: the array is allocated at this size once and
     *  the creation argument only decides how much of it is live. Larger than the
     *  family's usual 256 because this is the *dense* store — 256 slots is a
     *  short step sequence but a coarse wavetable, and the issue names wavetables
     *  first. 4096 ints is 16 KB per object, in line with ``.capture``'s 512
     *  reserved strings, and nothing walks it that is not already bounded.
     */
    static constexpr std::size_t MAX_SIZE = 4096;

    /** @brief Addresses with no numeric creation argument — Max's ``size``
     *         attribute default, "128 values, indexed with numbers from 0 to
     *         127". */
    static constexpr std::size_t DEFAULT_SIZE = 128;

    /** @brief Fewest addresses a creation argument can ask for. A zero-address
     *         table could neither be read nor written, so 1 is the floor. */
    static constexpr std::size_t MIN_SIZE = 1;

    /** @brief Longest destination name ``send`` will accept. ``NamedBus``
     *         truncates a published name at its own capacity while the
     *         in-patcher delivery path does not, so a longer name would address
     *         two different receivers on the two paths; ``.forward``'s limit,
     *         pinned to the same constant in the .cpp. */
    static constexpr std::size_t MAX_NAME_LENGTH = 63;

    /** @brief Max's ``quantile`` scale — "divided by 2^15 (32,768)", and the
     *         upper bound of the random draw a ``bang`` makes. */
    static constexpr int QUANTILE_SCALE = 32768;

    /** @brief How many addresses are live — what ``length`` reports. */
    std::size_t Size() const;

    /** @brief The value at @p address, or 0 out of range. Diagnostics and tests:
     *         control thread only. */
    int ValueAt(std::size_t address) const;

    /** @brief Max's name argument, or ``""``. Held and saved, but each table owns
     *         its own values — see the class documentation. */
    std::string Name() const;

    /** @brief Whether the contents are written into a saved patch. On by
     *         default, which is Max's default for this object. */
    bool Embeds() const;

    /** @brief Where ``next`` and ``prev`` will read from. */
    std::size_t Pointer() const;

    /** @brief Whether a ``load`` is in progress — every number arriving on
     *         inlet 0 is being stored rather than read. */
    bool Loading() const;

    // The contents, into the object's "state" key of a DumpJSON. Control thread —
    // patcherImplementation::DumpJSON holds mtx — but the guard is still taken,
    // because a message may be arriving from a rendering graph while the patch is
    // being saved.
    void DumpState(nlohmann::json::value_type& json) override;

    // The other half: called from ParseJSON on the control thread, on a freshly
    // built object the audio thread cannot see yet. Creation parameters are
    // already applied by then, so the size is known.
    void RestoreState(const nlohmann::json::value_type& json) override;

    // `send` needs the patcher's name to build its bus address, and the patcher
    // hands itself to every object through this. .forward's hook, for the same
    // reason: the prefix is built once here rather than on a message path.
    void SetParent(pObject* newParent) override;

    // Rebuild that prefix after a patcher rename. Control thread only. Called
    // from patcherImplementation::SetName so a `send` keeps reaching the
    // receivers that just re-anchored under the new name (issue #699).
    void RefreshBusPrefix();

  private:
    /**
     *  @brief Non-blocking exclusive access to the array.
     *
     *  ``Held()`` is false when another thread had it — the caller then does
     *  nothing at all. Never waits, never allocates. ``.coll``'s ``storeGuard``
     *  and ``.value``'s ``valueSlotGuard``, for the reason both give: this
     *  object is reachable from the control thread and from a rendering graph
     *  alike, a mutex is out on the second of those, and there is no single
     *  writer to build a seqlock around.
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

    // Take the guard, read address `address`, release it, and send the value out
    // outlet 0. False when there is no such address — or when the guard was lost,
    // which ends a `dump` early rather than punching a hole in it — and that is
    // what stops the `dump` walk. The guard is deliberately not held across the
    // send: holding it
    // across a synchronous fan-out would make a patch that wires the outlet back
    // into this object lose its own message to the guard it is still holding.
    bool Output(std::size_t address, YSE::THREAD thread);

    // Apply one address on inlet 0: stores the armed value when one is waiting
    // and consumes it, stores into the load cursor while `load` is on, otherwise
    // sends the value at that address.
    void ApplyAddress(int address, YSE::THREAD thread);

    // Max's quantile: the first address whose running sum reaches `fraction` of
    // the total of every stored value, or the last address when none does. The
    // total and the walk happen under one acquisition of the guard, so the
    // fraction is measured against the table the answer comes from.
    void Quantile(double fraction, YSE::THREAD thread);

    // Max's send: the value at `address` to every .r of that name, and on the
    // global bus. Nothing when the name is empty or over-long, or the address is
    // out of range.
    void SendTo(const char* name, std::size_t nameLength, int address, YSE::THREAD thread);

    // Store one number the way load mode does: at the load cursor, advancing it,
    // and ignoring everything past the end of the table. Guard held.
    void LoadOne(int value);

    // The command half of inlet 0. Returns false when `text` is none of them,
    // leaving the caller to read the message as numbers.
    bool HandleCommand(const char* text, std::size_t length, const std::string& message,
                       std::size_t argOffset, YSE::THREAD thread);

    // Claimed with a single exchange by readers and writers alike; the loser
    // drops. Mutable so the const diagnostic accessors can take it.
    mutable std::atomic<bool> busy{false};

    // The array. Sized to MAX_SIZE at construction and never resized; only the
    // first `size` entries are live, and every one of them is always present —
    // which is what "dense" means and what makes address i slot i.
    std::vector<int> values;
    std::size_t size = DEFAULT_SIZE;

    // Inlet 1's waiting value, consumed by the next address on inlet 0 — Max's
    // "stores the value at the next index number received at the left inlet", and
    // the thing `cancel` cancels. Guarded rather than atomic: the flag and the
    // value have to be read and cleared together, and two atomics could be seen
    // half-updated by the hot inlet.
    int pending = 0;
    bool hasPending = false;

    // Where `next` and `prev` read from. Run-time position, so it does not
    // survive a save.
    std::size_t pointer = 0;

    // Max's load mode and its cursor: while `loading` is on every number arriving
    // on inlet 0 is stored at `loadAt` and the cursor advances.
    bool loading = false;
    std::size_t loadAt = 0;

    // Max's embed flag, on by default — "the default behavior is 1 (save the
    // data)". Always written into a saved patch, so turning it off survives.
    bool embed = true;

    // Max's name argument. Held so a `.table mytable` brought across from Max
    // builds and so the argument survives a save, but it addresses nothing:
    // tables do not share contents here. Control thread only.
    std::string tableName;

    // "<patcherName>.", built in SetParent and prefixed to a `send` destination.
    // Reserved for the longest destination this object accepts, so the message
    // path only ever refills the address rather than growing it.
    std::string busPrefix;
    std::string busAddress;
    // The destination `send` was handed, held as a member for the same reason:
    // PassData takes a std::string and building one per message would allocate.
    std::string sendName;

    // The weighted draw behind `bang`. Per object and real-time safe; Max's table
    // has no seed message, so neither does this.
    RandomSource random;

    // The creation arguments, as tokens. Control thread only: written by
    // Parameters::Set, read by ParseParams(), never by a message handler.
    std::vector<std::string> creationArgs;
  };

} // namespace PATCHER
} // namespace YSE
