#pragma once
// The extended-precision MIDI *input* objects (issue #533) — `.xbendin`,
// `.xbendin2`, `.xctlin`, `.xnotein` and `.xmidiin`.
//
// Direct siblings of the seven-bit input family of #529 and built on exactly
// its plumbing: `mMidiInBase` owns the port, the `MIDI::inHub` subscription,
// the block poll and the bounded wait-free drain, and `mMidiChannelInBase`
// owns the channel filter. What is new here is only the reading of the bytes
// — a pitch bend as all fourteen of its bits rather than the coarse seven, a
// controller as an MSB/LSB pair, a note-off as the release velocity it
// actually carries, and a raw stream framed into whole messages.
//
// Guarded on YSE_ENABLE_MIDI_DEVICE alone, like mMidiIn.h and for the same
// reason: these objects listen to a hardware input port, and on a platform
// with no MIDI backend there is no port to listen to. When the option is OFF
// this whole file is empty and the objects are not registered.
#include "headers/defines.hpp"

#if YSE_ENABLE_MIDI_DEVICE

#include "mMidiIn.h"

#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief `.xbendin` — pitch bend at its full resolution, as one number
     *         (issue #533).
     *
     *  `.bendin` reports the coarse byte alone, 0-127 centred at 64, which is
     *  Max's reading and the one `.midiparse` shares. The wire carries
     *  fourteen bits, and this object reports all of them: 0-16383 centred at
     *  8192.
     *
     *  The difference is audible rather than academic. Over the usual
     *  two-semitone bend range, seven bits is a step of about three cents —
     *  a staircase on any bend slow enough to hear, and the reason a patch
     *  that bends expressively wants this object while one that only needs a
     *  modulation source does not. The two exist side by side rather than as
     *  one object with a mode because the *number* means something different
     *  in each: a patch reading 0-127 and a patch reading 0-16383 scale
     *  differently, and a flag that silently changed which one you got would
     *  be the worst of both.
     *
     *  Everything else is `.bendin`'s: the channel filter, the channel outlet
     *  that stays whether or not an argument was given, outlets firing right
     *  to left, and a drain that allocates, locks and blocks on nothing.
     */
    MIDI_IN_CLASS(mXBendIn, mMidiChannelInBase, YSE::OBJ::M_XBENDIN)

    /**
     *  @brief `.xbendin2` — pitch bend at its full resolution, as the two
     *         bytes that carry it (issue #533).
     *
     *  The same message as `.xbendin`, reported as the MSB and LSB rather
     *  than combined. A patch that wants the fourteen-bit number wants
     *  `.xbendin`; this one is for a patch that treats the coarse byte as the
     *  bend and the fine byte as a separate refinement — the shape `.xbendout2`
     *  takes back in, so the pair round-trips a wheel position byte for byte
     *  without ever combining and re-splitting it.
     */
    MIDI_IN_CLASS(mXBendIn2, mMidiChannelInBase, YSE::OBJ::M_XBENDIN2)

    /**
     *  @brief `.xnotein` — notes with the release velocity the wire carries
     *         (issue #533).
     *
     *  `.notein` reports a release as velocity 0, which is Max's rule and the
     *  thing that folds the two spellings of a note-off — a real 0x80 message
     *  and a 0x90 with velocity 0 — into one shape a patch can test once.
     *  What it costs is the release velocity: a 0x80 message carries a
     *  velocity byte of its own, saying how fast the key came *up*, and
     *  `.notein` throws it away.
     *
     *  This object keeps it on a third outlet. That matters for any voice
     *  whose release is shaped rather than fixed — which YSE's ADSR-based
     *  voices are — because it is the difference between a key lifted and a
     *  key released, and it is the only expressive gesture in MIDI that
     *  happens after a note is already over.
     *
     *  ### What each outlet says for each spelling of a note
     *
     *    - **note-on, velocity > 0** — pitch, that velocity, release 0.
     *    - **note-off (0x80)** — pitch, velocity 0, and the message's own
     *      velocity byte as the release. Velocity 0 rather than the release
     *      value so that `.notein`'s test (`.sel 0` on the velocity outlet)
     *      still reads a release as a release here.
     *    - **note-on, velocity 0** — pitch, velocity 0, release 0. A device
     *      that spells its releases this way sends no release velocity at
     *      all; reporting 64 for it would be inventing a number.
     *
     *  Outlets fire right to left, so whatever the pitch triggers downstream
     *  already sees the velocity, release velocity and channel that belong to
     *  it.
     */
    MIDI_IN_CLASS(mXNoteIn, mMidiChannelInBase, YSE::OBJ::M_XNOTEIN)

    /**
     *  @brief `.xctlin` — controllers at fourteen bits, from the MSB/LSB pair
     *         (issue #533).
     *
     *  ### The pairing, which is the whole object
     *
     *  MIDI defines controllers 0-31 as the coarse halves of controllers 0-31
     *  and controllers 32-63 as their fine halves: controller *n* carries the
     *  MSB and controller *n+32* the LSB of the same fourteen-bit value. A
     *  `.ctlin` sees those as two unrelated controllers moving; this object
     *  sees one, and reports 0-16383 on the controller number of the coarse
     *  half.
     *
     *  128 steps is audibly steppy on a filter sweep — the stepping a patch
     *  hears as zipper noise on a slow move — and this pairing is what a
     *  control surface with real resolution actually sends.
     *
     *  ### When it emits, which is the one real decision
     *
     *  **On both halves, not only on the LSB.** An MSB emits at once with the
     *  fine byte read as 0, and the LSB that follows emits again with the
     *  refined value. That is the MIDI specification's own recommended
     *  practice — an MSB resets the LSB — and it is chosen here over waiting
     *  for the pair because the failure modes are not comparable: a device
     *  that sends only the coarse half (which is most of them, and every plain
     *  7-bit knob on controllers 0-31) would leave an LSB-only object silent
     *  forever, while emitting twice merely produces one coarse value
     *  immediately followed by its refinement, a step no larger than a 7-bit
     *  controller's.
     *
     *  The stored MSB is kept per channel as well as per controller, so two
     *  keyboards sending the same controller on different channels cannot
     *  cross-contaminate each other's fine bytes.
     *
     *  Controllers 64-127 have no fine half and are **not** reported here at
     *  all: those are `.ctlin`'s, and passing them through with a fabricated
     *  fourteen-bit value would make a sustain pedal look like a 14-bit
     *  control.
     *
     *  ### Filters
     *
     *  `channel` as everywhere in the family, plus a `controller` argument
     *  naming the coarse controller number to accept, 0-31, defaulting to -1
     *  for all of them — `.ctlin`'s arrangement and for `.ctlin`'s reason:
     *  controller 0 is a real controller (Bank Select) and could not double as
     *  the wildcard. Neither filter removes an outlet.
     */
    class mXCtlIn : public mMidiChannelInBase {
    public:
      mXCtlIn();
      const char* Type() const override {
        return YSE::OBJ::M_XCTLIN;
      }
      CREATE(mXCtlIn)

      /** @brief Coarse controller numbers that have a fine partner, and
       *         therefore the number of MSB slots kept per channel. */
      static constexpr int PAIRED_CONTROLLERS = 32;

      /** @brief The controller filter: -1 for every paired controller, 0-31
       *         for one. */
      int ControllerFilter() const {
        return controller.load();
      }

    protected:
      void Receive(const YSE::MIDI::inEvent& event, YSE::THREAD thread) override;
      void ParseParams() override;

    private:
      aInt controller;

      // Last MSB seen per channel and coarse controller number, so an LSB can
      // be paired with the coarse byte it belongs to. A fixed array rather
      // than a map: this is read and written on the audio thread, where a
      // container that could rehash has no place. 512 bytes.
      unsigned char msb[16][PAIRED_CONTROLLERS] = {};
    };

    /**
     *  @brief `.xmidiin` — the raw byte stream, framed into whole messages
     *         (issue #533).
     *
     *  ### What it adds over `.midiin`
     *
     *  `.midiin` hands a patch one int per byte, in arrival order and with
     *  nothing interpreted. That is the honest thing for it to do, and it
     *  leaves the patch holding a stream with no boundaries in it: where one
     *  message ends and the next begins is something the patch has to work
     *  out, and a system-exclusive dump longer than the transport's event size
     *  arrives as consecutive chunks with nothing marking them as one message.
     *
     *  This object does that framing. Every complete message leaves the outlet
     *  as one list of byte values in decimal — `144 60 100` for a note-on —
     *  which is `.midiformat`'s output shape, `.midiparse`'s and `.seq`'s
     *  input shape, and the shape `.sxformat` builds. A dump split across four
     *  transport events arrives as one list, which is the "messages split
     *  across packets" of Max's `xmidiin` and the reason the object exists.
     *
     *  This is a deviation from Max, which spells `xmidiin` as a byte-at-a-time
     *  outlet like `midiin`'s and leaves the difference invisible. A list per
     *  message is both the local convention (every other whole-message object
     *  here emits one) and the only form in which the framing is actually
     *  *visible* to a patch — bytes handed out one at a time carry no boundary
     *  no matter who reassembled them.
     *
     *  ### The rules it frames by
     *
     *    - **A real-time byte (248-255)** is emitted at once as a one-byte
     *      list of its own and disturbs nothing: it may legally appear between
     *      any two bytes of any other message, including inside a dump.
     *    - **Running status** is honoured — a status byte may be omitted when
     *      it repeats, so a chord arrives as one 0x90 and then pairs of data
     *      bytes. Each pair is emitted as a whole message with the status put
     *      back in front, which is what makes the outlet's lists uniform.
     *    - **A new status byte abandons** an unfinished message rather than
     *      waiting for data that is not coming. Hardware interrupted mid-message
     *      simply starts sending something else.
     *    - **A data byte with no status** in front of it and no running status
     *      to inherit is dropped; there is no message it could belong to.
     *
     *  ### The one bound
     *
     *  A dump is arbitrarily long and this object's buffer is not: a message
     *  past `MAX_BYTES` is emitted in consecutive lists of that size, which is
     *  the treatment `.midiparse` gives the same case and what keeps the
     *  object's memory fixed. Nothing is dropped.
     *
     *  ### Real-time behaviour
     *
     *  The port, the subscription, the block poll and the bounded drain are
     *  `mMidiInBase`'s. On top of them this object walks bytes through a state
     *  machine of plain integers and appends decimal digits into a string
     *  reserved when the object was built. Nothing on the path allocates,
     *  locks, blocks or reads locale state.
     */
    class mXMidiIn : public mMidiInBase {
    public:
      mXMidiIn();
      const char* Type() const override {
        return YSE::OBJ::M_XMIDIIN;
      }
      CREATE(mXMidiIn)

      /** @brief Bytes of one message the outlet emits at a time. A longer
       *         system-exclusive dump leaves in consecutive lists of this
       *         size. */
      static constexpr int MAX_BYTES = 256;

      /** @brief Whether a system-exclusive message is currently open.
       *         Diagnostic surface — the framing state is otherwise invisible
       *         from the outlet. */
      bool InSysEx() const {
        return inSysEx;
      }

      /** @brief The status byte running status is currently holding, or 0 when
       *         there is none. Diagnostic surface. */
      int RunningStatus() const {
        return runningStatus;
      }

    protected:
      void Receive(const YSE::MIDI::inEvent& event, YSE::THREAD thread) override;

    private:
      // One byte through the framing state machine, and the send of whatever
      // has been collected. Audio thread only, one byte at a time.
      void Byte(unsigned char value, YSE::THREAD thread);
      void Flush(YSE::THREAD thread);

      unsigned char message[MAX_BYTES] = {};
      int count = 0;
      // Bytes the message in progress will hold once it is complete, status
      // byte included. Meaningless while `count` is 0 or `inSysEx` is set.
      int expected = 0;
      bool inSysEx = false;
      unsigned char runningStatus = 0;

      // The outgoing list text. Reserved in the constructor; never grown.
      std::string scratch;
    };

  } // namespace PATCHER
} // namespace YSE

#endif // YSE_ENABLE_MIDI_DEVICE
