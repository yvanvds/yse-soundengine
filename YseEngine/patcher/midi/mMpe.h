#pragma once
// The MPE family (issue #535) — `.mpeconfig`, `.mpeformat` and `.mpeparse`.
//
// ### What MPE is
//
// MPE (MIDI Polyphonic Expression, MMA RP-053) is not a new protocol. It is an
// agreement about how to *use* the sixteen channels MIDI already has: instead
// of one channel carrying a whole instrument, each sounding note is given a
// channel of its own, so pitch bend, channel pressure and controller 74 — all
// three of which are per-channel messages — become per-note ones. That is the
// entire idea, and it is why an ordinary keyboard can only bend a chord as a
// block while a Linnstrument, a Seaboard or an Osmose can bend one finger of it.
//
// A **zone** is what makes that shareable: one *master* channel carrying what
// applies to the whole instrument, plus a contiguous run of *member* channels
// carrying the notes. The specification defines two, and they grow away from
// each other so two instruments can share one cable:
//
//   * the **lower zone** — master channel 1, members counting up from 2;
//   * the **upper zone** — master channel 16, members counting down from 15.
//
// ### Where this family departs from Max, and why
//
// Max's four MPE objects are built around `poly~`, which this patcher does not
// have, and around a zone model that is Max's rather than the specification's.
// Three deviations follow, all deliberate:
//
//   * **Two zones, not seven.** Max's `mpeconfig` lets a patch place up to
//     seven zones anywhere among the sixteen channels via `masterchan` and
//     `chanrange`. MPE 1.0 defines exactly two, at fixed ends of the cable.
//     These objects implement the specification, because what a patch gains
//     here is talking to real hardware, and real hardware implements the
//     specification.
//   * **No `mpeevent`.** Max's `mpeformat` and `mpeparse` trade in an
//     `mpeevent` message carrying a zone index and a voice number alongside the
//     channel — Max's own voice-allocation layer for `poly~` and `vst~`, which
//     the specification has no concept of. Here the member channel *is* the
//     note's identity, which is what MPE itself says it is.
//   * **`.mpeparse` decodes MPE and nothing else.** Max's `mpeparse` is its
//     `midiparse` plus four MPE outlets. This patcher already has `.midiparse`
//     (#530), and an object that re-implemented it would drift from it, so
//     `.mpeparse` reports the four MPE dimensions and leaves program changes,
//     other controllers and system exclusive to `.midiparse` — wire both to the
//     same source and nothing is lost.
//
// `.polymidiin`, the fourth object issue #535 names, is deliberately **not**
// here. It is not a MIDI object at all in Max: it has no inlets, sits inside a
// `poly~` instance, and receives the `mpeevent` messages the enclosing `poly~`
// routes to that voice. Without a `poly~` equivalent there is nothing for it to
// be, and giving its name to some other object would collide with Max for every
// patch brought across. See the follow-up issue.
//
// ### No platform guard
//
// None of the three opens a device: two format bytes onto a list outlet and one
// decodes bytes off an inlet. All three are therefore compiled and registered on
// every platform, like `.midiparse` / `.midiformat` (#530) and `.sxformat`
// (#531). A patch that configures an MPE controller must load the same way on
// every machine, whether or not that machine has a MIDI backend to send through.
//
// ### Numeric byte lists, not the binary spelling
//
// The two senders emit their messages the way `.midiformat`, `.sxformat` and
// `.seq` do — `144 60 100`, one decimal number per byte — rather than the
// three-character binary strings the older senders (`.noteon`, `.bendout`, the
// `.x*out` and `.rpn*out` families) build. `.midiout` reads both (#748), so this
// is a free choice, and the numeric one is right for a new family: it is what a
// list is everywhere else in the patcher, it survives a byte of 0x00 or 0x20
// intact, and it is what `.mpeparse` reads — so `.mpeformat` into `.mpeparse` is
// a round trip a patch can actually wire, which the binary spelling would not
// give.
#include "../pObject.h"

#include <atomic>
#include <cstdint>

namespace YSE {
  namespace PATCHER {

    /** @brief The lower zone — master channel 1, members counting up from 2.
     *         The default, and the specification's recommended power-on state. */
    constexpr int MPE_ZONE_LOWER = 0;

    /** @brief The upper zone — master channel 16, members counting down from
     *         15. The second half of a cable shared by two instruments. */
    constexpr int MPE_ZONE_UPPER = 1;

    /** @brief Most member channels a zone can have: sixteen channels less the
     *         one its master takes. */
    constexpr int MPE_MEMBERS_MAX = 15;

    /** @brief The registered parameter number that carries an MPE
     *         Configuration Message — "00 06", so MSB 0 and LSB 6. */
    constexpr int MPE_CONFIG_RPN = 6;

    /** @brief Controller 74, MPE's third dimension: *slide*, or *timbre*, or
     *         the Y axis, depending on whose documentation you read. The
     *         forward-back position of a finger on a controller that has one. */
    constexpr int MPE_SLIDE_CONTROLLER = 74;

    // What a channel is doing in a zone, as the role outlet reports it.

    /** @brief The channel takes no part in this zone — traffic belonging to the
     *         other zone, or to a channel past the end of this one's members. */
    constexpr int MPE_ROLE_NONE = 0;

    /** @brief A member channel: whatever arrives on it belongs to the one note
     *         sounding there. */
    constexpr int MPE_ROLE_MEMBER = 1;

    /** @brief The master channel: whatever arrives on it applies to every note
     *         in the zone at once. */
    constexpr int MPE_ROLE_MASTER = 2;

    /** @brief Wire nibble (0-15) of a zone's master channel: 0 — channel 1 —
     *         for the lower zone, 15 — channel 16 — for the upper. */
    inline int MpeMasterNibble(int zone) {
      return zone == MPE_ZONE_UPPER ? 15 : 0;
    }

    /**
     *  @brief What wire nibble @p nibble is doing in a zone of @p members
     *         member channels.
     *
     *  The master channel is the master channel whether or not the zone has any
     *  members: a zone switched off still has one, and a device that has gone
     *  back to plain MIDI still sends on it. Everything else is a member only
     *  while it falls inside the run of channels the configuration reserved —
     *  which is what keeps an object watching a four-note lower zone from
     *  reading another instrument's channel 12 as one of its notes.
     */
    inline int MpeRole(int zone, int members, int nibble) {
      if (nibble == MpeMasterNibble(zone)) return MPE_ROLE_MASTER;
      if (members <= 0) return MPE_ROLE_NONE;
      if (zone == MPE_ZONE_UPPER) {
        return (nibble >= 15 - members && nibble <= 14) ? MPE_ROLE_MEMBER : MPE_ROLE_NONE;
      }
      return (nibble >= 1 && nibble <= members) ? MPE_ROLE_MEMBER : MPE_ROLE_NONE;
    }

    /** @brief Clamps a zone argument to one of the two zones MPE defines.
     *         Anything but 1 is read as the lower zone, which is the default
     *         and the zone a controller out of its box is using. */
    inline int MpeClampZone(int zone) {
      return zone == MPE_ZONE_UPPER ? MPE_ZONE_UPPER : MPE_ZONE_LOWER;
    }

    /** @brief Clamps a member-channel count into 0-15. */
    inline int MpeClampMembers(int members) {
      if (members < 0) return 0;
      if (members > MPE_MEMBERS_MAX) return MPE_MEMBERS_MAX;
      return members;
    }

    /**
     *  @brief `.mpeconfig` — turn an MPE zone on, off, or resize it (issue
     *         #535).
     *
     *  ### What an MPE Configuration Message is
     *
     *  MPE only works if the two ends agree on which channels are notes and
     *  which one carries the instrument as a whole. The MPE Configuration
     *  Message — the MCM — is that agreement, and it is the only message in the
     *  whole of MPE that is not ordinary MIDI being used in a particular way.
     *
     *  It is registered parameter number "00 06", so it is three control
     *  changes: controllers 100 and 101 select the parameter, and controller 6
     *  (Data Entry MSB) carries the number of member channels the zone is to
     *  have. Sent on the zone's master channel — 1 for the lower zone, 16 for
     *  the upper — because that is the one channel whose meaning does not depend
     *  on the answer.
     *
     *  **A count of 0 turns the zone off**, which is the specification's own
     *  way of saying "go back to plain MIDI". It is not a degenerate case to be
     *  refused: a patch that finishes with an MPE controller and hands it to
     *  something that is not MPE-aware has to send it.
     *
     *  ### Three messages, in the specification's order
     *
     *  Three separate lists, because `.midiout` sends each list it receives as a
     *  single MIDI message and nine bytes together would arrive as one malformed
     *  message rather than three good ones. `.rpnout`'s arrangement (#534) and
     *  `.xctlout`'s (#533), for exactly the same reason.
     *
     *  The **fine byte of the parameter number goes first** — controller 100
     *  carrying 6, then controller 101 carrying 0 — which is the reverse of
     *  `.rpnout`'s coarse-first order. That is not an oversight: it is the
     *  literal byte sequence the MPE specification prints for this message
     *  (`[Bn 64 06] [Bn 65 00] [Bn 06 mm]`), and a device that pattern-matches
     *  the MCM rather than running a general RPN state machine will only
     *  recognise it that way round. Order is immaterial to a correct receiver,
     *  so matching the specification costs nothing and buys the incorrect ones.
     *
     *  The Data Entry LSB that would follow on controller 38 is **not** sent:
     *  the MCM's fine byte is explicitly unused, and a fourth message that means
     *  nothing is a message a device can still get wrong.
     *
     *  ### What the receiver does with it
     *
     *  Worth knowing, because it is why a patch usually needs nothing else:
     *  receiving an MCM requires a device to set the master channel's pitch-bend
     *  range to ±2 semitones and every member channel's to ±48, and to stop
     *  ongoing notes and reset controllers on every channel entering or leaving
     *  the zone. The wide member range is what `.mpeformat`'s fourteen-bit bend
     *  inlet exists for.
     *
     *  ### Real-time behaviour
     *
     *  `Calculate()` writes three numbers into a stack buffer and hands each
     *  message to the outlet as a `std::string` short enough for small-string
     *  storage. Nothing on the path allocates, locks or blocks.
     */
    class mMpeConfig : public pObject {
    public:
      mMpeConfig();
      const char* Type() const override {
        return YSE::OBJ::M_MPECONFIG;
      }
      CREATE(mMpeConfig)
      _NO_MESSAGES
      _DO_CALCULATE

      _INT_IN(SetIntMembers)

      /** @brief Member channels the next message will ask for, 0-15.
       *         Diagnostic / test surface. */
      int Members() const {
        return members;
      }

      /** @brief The zone this object configures: 0 lower, 1 upper. */
      int Zone() const {
        return MpeClampZone(zone);
      }

    private:
      int members;
      int zone;
    };

    /**
     *  @brief `.mpeformat` — build the MIDI messages an MPE instrument reads
     *         (issue #535).
     *
     *  ### What it is for
     *
     *  MPE's four gestures are four ordinary MIDI messages sent on a channel
     *  that stands for one note. This is the box that puts them there: set the
     *  member channel once, then feed it notes, bends, pressures and slides and
     *  each leaves as a complete message addressed to that note. The inlets
     *  mirror `.mpeparse`'s outlets so the pair wires straight across — the
     *  discipline `.midiparse` and `.midiformat` already keep.
     *
     *  ### The inlets
     *
     *    0 note (list: pitch, velocity)   3 slide (int, controller 74)
     *    1 bend (int, 0-16383)            4 channel (int, 1-16, cold)
     *    2 pressure (int, 0-127)
     *
     *  Every inlet but the channel is hot. Inlet 4 stores and emits nothing,
     *  which is `.midiformat`'s rule for the same inlet and Max's.
     *
     *  Max's `mpeformat` instead has one inlet *per member channel* — sixteen of
     *  them by default — and emits `mpeevent` messages for `poly~` and `vst~`.
     *  Neither exists here, and an object whose inlet count is a creation
     *  argument has no stable inlet numbering, which is the same objection the
     *  MIDI-input family already makes to Max's disappearing channel outlet
     *  (#529). One channel inlet, as `.midiformat` has.
     *
     *  ### Where it differs from `.midiformat`, and why
     *
     *  **Bend is fourteen bits here, not seven.** `.midiformat` takes 0-127 and
     *  zeroes the fine byte, which is Max's `midiformat` and matches `.bendin`.
     *  That reading is unusable for MPE: receiving an MCM sets the per-note bend
     *  range to ±48 semitones rather than ±2, so a seven-bit bend would step in
     *  three quarters of a semitone and a glide would sound like a staircase.
     *  This object takes the full 0-16383 with 8192 at rest, which is
     *  `.xbendout`'s reading (#533) and `.mpeparse`'s.
     *
     *  **Pressure and slide are the per-note dimensions.** Channel pressure on a
     *  member channel is that note's pressure, not the keyboard's; controller 74
     *  on it is that note's slide. Neither is a different message from what
     *  plain MIDI sends — it is the channel that makes them per-note, and that
     *  is what the channel inlet is for.
     *
     *  **A release is a note-on with velocity 0.** `.midiformat`'s rule and
     *  `.mpeparse`'s reading, so a note round-trips through the pair as the pair
     *  reported it. A real note-off (status 128) is `.noteoff`'s and
     *  `.xnoteout`'s.
     *
     *  ### Real-time behaviour
     *
     *  A message is at most three bytes, so each is written into a stack buffer
     *  and handed to the outlet as a `std::string` short enough for small-string
     *  storage. Nothing on the path allocates, locks or blocks, and no state is
     *  shared between two inlets — so unlike `.midiformat` there is nothing here
     *  for a re-entrant send to corrupt and no guard is needed.
     */
    class mMpeFormat : public pObject {
    public:
      mMpeFormat();
      const char* Type() const override {
        return YSE::OBJ::M_MPEFORMAT;
      }
      CREATE(mMpeFormat)
      _NO_MESSAGES
      _NO_CALCULATE

      _INT_IN(FormatInt)
      _FLOAT_IN(FormatFloat)
      _LIST_IN(FormatList)

      /** @brief The member channel every message is addressed to, 1-16. */
      int Channel() const {
        return channel;
      }

    private:
      // One MIDI message onto the outlet, as a numeric byte list.
      void Emit(int status, int first, int second, YSE::THREAD thread);
      void Emit(int status, int first, YSE::THREAD thread);

      // A note, from either the list form or the bare-pitch one.
      void Note(int pitch, int velocity, YSE::THREAD thread);

      int channel = 1;
    };

    /**
     *  @brief `.mpeparse` — read the four MPE dimensions off a MIDI stream
     *         (issue #535).
     *
     *  ### What it decodes, and what it deliberately does not
     *
     *  Bytes arrive at the inlet — one at a time as `.midiin` and `.seq` send
     *  them, or as a numeric list as `.mpeformat` and `.midiformat` send them —
     *  and the four gestures MPE defines leave the four leftmost outlets, each
     *  accompanied by the channel it arrived on and what that channel is doing
     *  in the zone.
     *
     *  Program changes, other controllers, polyphonic key pressure and system
     *  exclusive are **not** reported. Max's `mpeparse` is its `midiparse` plus
     *  four MPE outlets; this patcher already has `.midiparse` (#530), and a
     *  second object re-implementing it would be two decoders to keep in step.
     *  Wire `.midiparse` to the same source and nothing in the stream is lost.
     *
     *  ### The channel is the note, and the role says whose it is
     *
     *  The whole of MPE's per-note expression is carried by *which channel* a
     *  perfectly ordinary message arrived on, so the channel outlet is this
     *  object's note identity — a patch routes on it to keep voices apart.
     *
     *  The role outlet is the second half of that answer. A message on the
     *  zone's **master** channel applies to every note at once (the
     *  specification requires a receiver to combine master and member data for
     *  each sounding note), a message on a **member** channel belongs to the one
     *  note there, and a message on a channel outside the zone belongs to
     *  neither — the other zone's traffic, or plain MIDI sharing the cable.
     *  Reporting all three rather than silently dropping the third is the same
     *  choice `.midiparse` makes with its raw outlet: a patch can filter, but it
     *  cannot recover what an object threw away.
     *
     *  ### It learns the zone from the stream
     *
     *  The `zone` and `members` creation arguments say what to assume, and the
     *  defaults are the specification's recommended power-on state — the lower
     *  zone with all fifteen member channels, which is what a controller out of
     *  its box sends. An MPE Configuration Message seen on the configured zone's
     *  master channel then **updates the member count live**, so a patch that
     *  sends `.mpeconfig` at one end reads the right roles at the other without
     *  being told twice. The MCM itself is consumed rather than reported: it is
     *  configuration, not expression.
     *
     *  ### Real-time behaviour
     *
     *  A list or int handler runs on whichever thread sent the message,
     *  routinely the audio callback. The state machine is plain integers and
     *  each outgoing list is built in a stack buffer, so nothing on the path
     *  allocates, locks or blocks. Two threads sending at once — or a patch that
     *  wires an outlet back into the inlet — are refused by a test-and-set guard
     *  whose loser is counted on `Dropped()` rather than made to spin, which is
     *  `.midiparse`'s arrangement and for the same reason: the running-status
     *  state machine is not re-entrant.
     */
    class mMpeParse : public pObject {
    public:
      mMpeParse();
      const char* Type() const override {
        return YSE::OBJ::M_MPEPARSE;
      }
      CREATE(mMpeParse)
      _NO_MESSAGES
      _NO_CALCULATE

      _INT_IN(ParseInt)
      _FLOAT_IN(ParseFloat)
      _LIST_IN(ParseList)

      /** @brief The zone being watched: 0 lower, 1 upper. */
      int Zone() const {
        return MpeClampZone(zone.load());
      }

      /** @brief Member channels the zone currently has, 0-15 — the creation
       *         argument until an MPE Configuration Message on the master
       *         channel replaces it. Diagnostic / test surface: the
       *         configuration is otherwise visible only through the role
       *         outlet. */
      int Members() const {
        return MpeClampMembers(members.load());
      }

      /** @brief Messages refused because the object was already mid-message on
       *         another thread, or because a patch wired its own outlet back
       *         into its inlet. Monotonic, readable from any thread;
       *         diagnostics and tests only. */
      std::uint64_t Dropped() const {
        return dropped.load(std::memory_order_relaxed);
      }

    private:
      // One byte through the state machine. Everything below runs under the
      // guard, on one thread at a time.
      void Byte(unsigned char value, YSE::THREAD thread);
      void Voice(unsigned char status, YSE::THREAD thread);

      // True when this control change was part of an MPE Configuration Message
      // on the watched zone's master channel, and so is not expression.
      bool Config(int nibble, int controller, int value);

      // Role and channel out first, then the value — right to left, so whatever
      // the leftmost outlet triggers downstream already knows whose note it is.
      void Report(int nibble, int outlet, int value, YSE::THREAD thread);
      void ReportNote(int nibble, int pitch, int velocity, YSE::THREAD thread);

      bool Enter();
      void Leave();

      // Which zone to watch, and how wide it is. Atomic because `members` is
      // written from two places — the creation arguments, on the control
      // thread, and a configuration message in the stream, on whichever thread
      // the bytes arrived on.
      aInt zone;
      aInt members;

      // Running status: the channel-voice status byte to read the next data
      // bytes under, or 0 when there is none. Cleared by a system-common
      // message and untouched by a system real-time one, which is the rule that
      // lets a clock pass through a chord without breaking it.
      unsigned char runningStatus = 0;
      unsigned char data[2] = {0, 0};
      int dataCount = 0;

      // Data bytes still owed to a system-common message, and whether a
      // system-exclusive dump is in progress. Neither is decoded — they are
      // tracked only so their data bytes cannot be mistaken for a voice
      // message's and desynchronise the stream that follows.
      int systemNeeded = 0;
      bool inSysEx = false;

      // The registered parameter number currently selected on the master
      // channel, so a Data Entry there can be recognised as a configuration
      // message. Only the master channel needs one: a member channel carries no
      // configuration.
      unsigned char rpnMsb = 0x7F;
      unsigned char rpnLsb = 0x7F;

      std::atomic<bool> busy{false};
      std::atomic<std::uint64_t> dropped{0};
    };

  } // namespace PATCHER
} // namespace YSE
