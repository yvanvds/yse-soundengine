#pragma once
#include "headers/defines.hpp"
// `.midiinfo` (issue #536) — the patcher's answer to "which MIDI ports does
// this machine have, and what are they called?".
//
// ### Why the patcher needed it
//
// Every MIDI object in the patcher addresses a device by a bare index:
// `.midiout 2`, `.notein 1`. Nothing in a patch could say what index 2 *is*,
// so a patch that moved between machines — or one whose author plugged the
// interface into a different socket — silently addressed the wrong hardware,
// and there was no way to choose a port from inside the patch at all. This
// object is the missing lookup: it reports the count, the indices and the names
// of the ports, so device selection can be driven by patch logic instead of by
// a number typed into a box.
//
// ### Guarded on YSE_ENABLE_MIDI_DEVICE
//
// Like `.midiout` and the input family, and for the same reason: with no RtMidi
// backend there are no ports to enumerate, and `MIDI::deviceManager` does not
// exist to ask. When the option is OFF this whole file is empty and the object
// is not registered. Following #754, the gate is the capability and nothing
// else — no platform check rides along with it.
#if YSE_ENABLE_MIDI_DEVICE
#include "../pObject.h"
#include "midiPortScanner.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief `.midiinfo` — report the machine's MIDI ports (issue #536).
     *
     *  ### What a bang produces
     *
     *  Three outlets, fired right to left as the patcher's convention has it:
     *
     *    2 count   how many ports there are, sent once, first of all
     *    1 index   the port's index — the number `.midiout` and `.notein` take
     *    0 name    the port's name, as the driver reports it
     *
     *  A bang sends the count, then one `index` / `name` pair per port in index
     *  order. The count comes first so a patch knows how many pairs to expect;
     *  the index comes before the name of the same port so that whatever the
     *  name outlet triggers downstream already knows which port it is looking
     *  at. A machine with no ports at all sends a count of 0 and nothing else,
     *  which is the honest report rather than silence.
     *
     *  The name goes out **whole**, spaces and all, exactly as the driver
     *  spells it ("Microsoft GS Wavetable Synth 0"). A patcher list is a string,
     *  so an object downstream that splits on whitespace will see several atoms
     *  — use `.midiinfo` to pair a name with its index, and match on the pair.
     *
     *  ### Input ports or output ports
     *
     *  The two sets are separate and are numbered separately: input port 1 and
     *  output port 1 are unrelated devices. Which set this object reports is the
     *  `direction` creation argument — `input` (the default) or `output` — and
     *  the messages `input` and `output` switch it live, so one box can serve
     *  both halves of a device-selection patch. Two boxes wired to the same
     *  sinks is the other way round, and neither is wrong.
     *
     *  Both sets are snapshotted regardless of which one is being reported, so
     *  switching direction needs no re-enumeration and reports the same moment
     *  in time as the other half.
     *
     *  `input` and `output` are accepted in **both** of the patcher's spellings
     *  of a word arriving at an inlet: as a message, which is what a `.message`
     *  box sends, and as a list, which is what everything else sends and the
     *  only one the C ABI can produce. One word can only be a command here —
     *  the object has no data input — so refusing either spelling would buy
     *  nothing and cost a patch its only way in from a binding.
     *
     *  ### The snapshot, which is the whole real-time story
     *
     *  Enumeration is not something the audio callback may do. `RtMidi`'s
     *  `getPortCount` and `getPortName` talk to the platform's MIDI service and
     *  return `std::string`s: they allocate, they can take a driver lock, and
     *  the first call constructs the backend. So the ports are read **once, on
     *  the control thread**, in `SetParent` — the moment the object joins its
     *  patcher, which is where the creation arguments are already parsed and
     *  where the object is not yet visible to any rendering graph. `.midiin`
     *  opens its port at exactly that moment and for exactly that reason.
     *
     *  What a bang then reads is a fixed-size snapshot: `PORTS_MAX` names of at
     *  most `NAME_CAPACITY` characters, in storage that was allocated with the
     *  object. Reporting copies a name into a scratch string reserved to that
     *  same bound at construction, so an `assign` into it reuses storage that
     *  already exists. Nothing on the bang path allocates, locks or blocks.
     *
     *  A machine with more than `PORTS_MAX` ports has the surplus dropped rather
     *  than growing the table; a name longer than `NAME_CAPACITY - 1` characters
     *  is truncated. Both bounds are far above what real hardware presents.
     *
     *  ### `refresh` — rescanning without touching the audio thread (#757)
     *
     *  A controller plugged in after the patch loaded would be invisible to a
     *  snapshot taken once, which is a real gap for a live set whose interface
     *  is connected after the patch is up. Max's `midiinfo` re-enumerates on
     *  every bang; this object does it on an explicit `refresh` instead,
     *  because a bang here may be on the audio callback and enumeration may
     *  never be.
     *
     *  The `refresh` handler itself does almost nothing: it asks
     *  `midiPortScanner` for a rescan, which is one CAS and one push onto the
     *  background pool's lock-free ring. RtMidi's `getPortCount` /
     *  `getPortName` — which allocate, take `MIDI::deviceManager`'s mutex and
     *  construct the backend on first use — run on the pool, into storage the
     *  scanner owns and that outlives this object. Nothing on the dispatching
     *  thread allocates, locks or blocks, whichever thread that turns out to
     *  be.
     *
     *  `refresh` is accepted in both of the patcher's spellings of a word at an
     *  inlet, exactly as `input` and `output` are, and for the same reason.
     *
     *  ### How the patch learns the rescan landed
     *
     *  The finished snapshot is collected in `Calculate()`, which is why this
     *  object asks for `WantsBlockPoll()` even though it has an inlet: a
     *  background result has no cord to arrive on. A poll that finds a new
     *  snapshot copies it in — a plain assignment of a fixed-size struct, no
     *  allocation — and **re-emits the whole listing**, so a patch already
     *  wired to the three outlets sees the new machine without a fourth outlet
     *  and without a bang of its own. Keeping the outlet count stable is the
     *  point: a saved patch's cords must not shift because the object learned
     *  to rescan.
     *
     *  A poll that finds nothing is one relaxed atomic load, which is what it
     *  costs to have this object in a patch at all. And a result lands only
     *  while the patcher renders, like every other deferred path here.
     *
     *  ### The re-entrancy guard
     *
     *  A bang handler runs on whichever thread dispatched the message, and the
     *  reporting walk uses one scratch string. Two threads banging at once — or
     *  a patch that wires the name outlet back into the inlet, which would
     *  otherwise recurse without bound — are refused by a test-and-set guard
     *  whose loser is counted on `Dropped()` rather than made to spin. That is
     *  `.mpeparse`'s and `.midiparse`'s arrangement, for the same reason.
     */
    class mMidiInfo : public pObject {
    public:
      /** @brief Ports one direction's snapshot holds. Surplus ports are
       *         dropped: the table cannot grow without allocating, and no real
       *         machine comes near this. */
      static constexpr int PORTS_MAX = midiPortSnapshot::PORTS_MAX;

      /** @brief Longest port name a slot carries, including the terminator.
       *         A longer name is truncated. */
      static constexpr std::size_t NAME_CAPACITY = midiPortSnapshot::NAME_CAPACITY;

      /** @brief `direction` = input: the ports a patch can *receive* from. */
      static constexpr int DIR_INPUT = midiPortSnapshot::DIR_INPUT;

      /** @brief `direction` = output: the ports a patch can *send* to. */
      static constexpr int DIR_OUTPUT = midiPortSnapshot::DIR_OUTPUT;

      mMidiInfo();
      ~mMidiInfo() override;

      mMidiInfo(const mMidiInfo&) = delete;
      mMidiInfo& operator=(const mMidiInfo&) = delete;
      mMidiInfo(mMidiInfo&&) = delete;
      mMidiInfo& operator=(mMidiInfo&&) = delete;

      const char* Type() const override {
        return YSE::OBJ::M_MIDIINFO;
      }
      CREATE(mMidiInfo)

      _DO_MESSAGES
      _DO_CALCULATE

      _BANG_IN(Report)
      _LIST_IN(SetDirection)

      /** @brief Control thread. Joining a patcher is what reads the ports: the
       *         creation arguments are parsed by now and the object is not yet
       *         published, so this is the one moment at which the platform's
       *         MIDI service may be asked. `.midiin` opens its port here. */
      void SetParent(pObject* newParent) override;

      /** @brief Yes — but for a background result rather than for a wire
       *         (issue #757). A rescan finishes on the pool and has no cord to
       *         arrive on, so the block poll is what collects it and re-emits
       *         the listing. See the class comment. */
      bool WantsBlockPoll() const override {
        return true;
      }

      /** @brief Which set is being reported: `DIR_INPUT` or `DIR_OUTPUT`. */
      int Direction() const {
        return direction.load() == DIR_OUTPUT ? DIR_OUTPUT : DIR_INPUT;
      }

      /** @brief Ports in the snapshot for the direction currently selected.
       *         What a bang sends out the count outlet. */
      int PortCount() const;

      /** @brief The snapshotted name of port @p index in the current direction,
       *         or `""` when there is no such port. Allocates a string, so
       *         diagnostics and tests rather than a patch path — a patch reads
       *         the same names off the name outlet. */
      std::string PortName(int index) const;

      /** @brief Bangs refused because the object was already reporting on
       *         another thread, or because a patch wired its own outlet back
       *         into its inlet. Monotonic, readable from any thread;
       *         diagnostics and tests only. */
      std::uint64_t Dropped() const {
        return dropped.load(std::memory_order_relaxed);
      }

      /** @brief Rescans that have landed and been re-emitted (issue #757).
       *         Monotonic, readable from any thread; diagnostics and tests
       *         only — a patch learns the same thing from the listing arriving
       *         on its outlets. */
      std::uint64_t Refreshed() const {
        return refreshed.load(std::memory_order_relaxed);
      }

    private:
      _PARM_PARSE

      // Copy `from` over the snapshot. Control thread only — it waits for the
      // guard rather than giving up, since a publish that silently did nothing
      // would leave the object reporting an empty machine forever.
      void Publish(const midiPortSnapshot& from);

      // Send the whole listing: the count, then an index and a name per port.
      // The guard below must already be held — it is what makes `scratch` this
      // walk's alone.
      void Emit(YSE::THREAD thread);

      // Non-blocking exclusive access to the snapshot and the scratch string.
      bool Enter();
      void Leave();

      // "input" / "output" / "refresh", from either spelling. Anything else is
      // ignored. None of the three can fail and none of them blocks.
      void Command(const std::string& text);

      // The default matters on its own: `Parameters::Set` returns before the
      // parse callback when there are no arguments at all, so a bare
      // `.midiinfo` never runs ParseParams and takes its direction from here.
      aInt direction{DIR_INPUT};
      std::string directionArg;

      midiPortSnapshot ports;

      // This object's slot in the process-wide scanner (issue #757), taken for
      // the object's whole life. 0 when the table was full, in which case
      // `refresh` is accepted and does nothing — the object still reports the
      // snapshot it took when it joined.
      midiPortScanner::Handle scan{0};

      // Where a name is staged on its way to the outlet. Reserved to
      // NAME_CAPACITY at construction, so assigning into it on a bang path
      // reuses storage that exists rather than allocating.
      std::string scratch;

      std::atomic<bool> busy{false};
      std::atomic<std::uint64_t> dropped{0};
      std::atomic<std::uint64_t> refreshed{0};
    };

  } // namespace PATCHER
} // namespace YSE

#endif // YSE_ENABLE_MIDI_DEVICE
