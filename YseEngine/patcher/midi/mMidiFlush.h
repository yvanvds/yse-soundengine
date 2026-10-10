#pragma once
// `.midiflush` (issue #537) — the safety valve every MIDI patch needs.
//
// Deliberately *not* guarded on YSE_ENABLE_MIDI_DEVICE, for the reason
// `.midiparse` and `.midiformat` are not (see mMidiCodec.h): this object opens
// no device, holds no port and touches no backend. It reads bytes, remembers
// which notes they left sounding, and writes bytes. A patch that drives a
// software synth built out of patcher objects, or that records to a file
// through `.seq`, gets stuck notes exactly as a patch driving hardware does,
// and it would be a poor joke to lose the object that clears them on the
// platforms with no hardware to blame.
#include "../pObject.h"

#include <atomic>
#include <cstdint>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief `.midiflush` — send note-offs for every note left hanging (issue
     *         #537), Max's `midiflush`.
     *
     *  ### What it is for
     *
     *  A stuck note is the classic MIDI failure. A patch that is stopped
     *  mid-phrase — a `.metro` switched off, a sequence interrupted, a patcher
     *  cleared — has sent note-ons whose note-offs are never going to be sent,
     *  and the device holds them until someone power-cycles it. Nothing else in
     *  the patcher can clear them: the sender objects are stateless formatters,
     *  and `.midiout`'s `allnotesoff` is controller 123, which a great many
     *  devices ignore and which reaches only the one port that object holds.
     *
     *  This object sits **in the stream**, wherever the bytes already pass —
     *  between the senders and `.midiout`, or between `.midiin` and whatever
     *  reads it — and passes everything through untouched while remembering
     *  what is sounding. A bang then releases exactly those notes, on exactly
     *  the channels they were played on, and nothing else.
     *
     *  ### The inlet takes the stream and the bang both
     *
     *  One inlet, which is Max's shape: the bytes pass through it and a bang
     *  arriving at the same inlet is the flush. Both of the patcher's spellings
     *  of a MIDI byte list are read, which is `.midiout`'s arrangement since
     *  issue #748 and for the same reason — the older senders (`.noteon`,
     *  `.noteoff`, the `.x*out` family) build a message whose *characters are
     *  the bytes*, while `.midiformat`, `.sxformat` and `.seq` spell a byte as
     *  its decimal number. An object that read only one of them would be
     *  unusable in half the patches that need it. A bare int is a byte too, so
     *  the object also sits directly downstream of `.midiin`, which emits its
     *  stream one int at a time.
     *
     *  ### It decodes the stream properly rather than looking for 0x90
     *
     *  Note tracking is not pattern matching on status bytes: a note-on may
     *  arrive with no status byte in front of it at all (running status, which
     *  is how a chord is usually sent), a timing clock may land between the two
     *  data bytes of one, and a system-exclusive dump is full of bytes that
     *  would read as notes if they were not inside a dump. So the same three
     *  rules `.midiparse` keeps are kept here — running status, real-time
     *  interleaving, and system messages skipped by length — and only the
     *  note messages are acted on. The rest is counted through and forgotten.
     *
     *  Both of the spellings hardware uses for a release clear a note: a
     *  note-off message, and a note-on with velocity 0. That is the same
     *  folding `.midiparse` and `.notein` do, and here it is not cosmetic —
     *  an object that only understood one of them would flush notes the device
     *  had already released.
     *
     *  ### What a bang sends
     *
     *  One note-off per sounding note, as a numeric byte list — `128 60 0` —
     *  in channel then pitch order, and the set is emptied as they go. The
     *  numeric spelling rather than the binary one because it is the only one
     *  that survives a byte of 0x00 (which every one of these note-offs ends
     *  with) and 0x20 intact, and because it is what `.midiout` and
     *  `.midiparse` both read. A release velocity of 0 is the conventional
     *  "no velocity information" and is what a device that ignores release
     *  velocity — nearly all of them — expects.
     *
     *  A note is released exactly once: banging twice sends nothing the second
     *  time, because after the first bang nothing is sounding. Notes that
     *  arrive after a flush are tracked from scratch.
     *
     *  ### The set is fixed size, which is the whole real-time story
     *
     *  A message handler runs on whichever thread dispatched the message, and
     *  in-patcher delivery dispatches on `T_DSP` — routinely the audio
     *  callback. So what is sounding is kept as a bitmap of 16 channels x 128
     *  pitches, 256 bytes allocated with the object, and the outgoing text is
     *  built into a string reserved in the constructor. Nothing on the
     *  pass-through path or the flush path allocates, takes a lock or blocks,
     *  and the flush is bounded by the bitmap rather than by how many notes
     *  a patch managed to strand.
     *
     *  Two threads sending at once — or a patch that wires the outlet back
     *  into the inlet, which a flush would otherwise recurse through — are
     *  refused by a test-and-set guard whose loser is counted on `Dropped()`
     *  rather than made to spin. That is `.midiparse`'s arrangement and it is
     *  here for the same reason: the state machine is not re-entrant.
     *
     *  ### It fires on teardown too, and not by its own doing
     *
     *  A patcher cleared or destroyed while notes are sounding flushes them
     *  (issue #758): the patcher runs a stop pass over every object *before* it
     *  unwires any of them, and this object's `Teardown` is a flush. That
     *  ordering is the patcher's rather than this object's, which is why the
     *  object could not have it on its own — `patcherImplementation::Clear`
     *  used to unwire as it walked, so a `.midiflush` reached after its
     *  `.midiout` would have sent its note-offs into a cord that no longer
     *  existed. Deleting the `.midiflush` on its own flushes it too, for the
     *  same reason and through the same hook.
     *
     *  What the note-offs meet on the way out is a fully wired patch, so they
     *  behave exactly as a bang's do: `.midiout` sends them if its port is
     *  open, and drops them if the patch never opened one — in which case
     *  nothing was ever sent through it and there is nothing left sounding on
     *  it. A bang before the patch goes away is still the way to flush at any
     *  other moment.
     *
     *  ### What it does not do
     *
     *  Nothing at all can be promised for a process killed outright: no engine
     *  code runs then, and the device keeps whatever it was holding.
     */
    PATCHER_CLASS(mMidiFlush, YSE::OBJ::M_MIDIFLUSH)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(Flush)
    _INT_IN(StreamInt)
    _FLOAT_IN(StreamFloat)
    _LIST_IN(StreamList)

    // The patcher is about to unwire this object (issue #758): flush, which is
    // what a bang does, while the cord to `.midiout` is still there to flush
    // down. See the class notes.
    void Teardown(YSE::THREAD thread) override;

  public:
    /** @brief MIDI channels tracked, and so the height of the bitmap. */
    static constexpr int CHANNELS = 16;

    /** @brief Pitches per channel — the whole of the MIDI note range. */
    static constexpr int PITCHES = 128;

    /** @brief Notes currently sounding: what the next bang would release.
     *         Diagnostics and tests — a patch sees the same thing by banging. */
    int Held() const {
      return heldCount;
    }

    /** @brief Whether @p pitch is sounding on @p channel, which is 1-16 here as
     *         it is everywhere a patcher object reports a channel. Out of range
     *         is false rather than an error. Diagnostics and tests. */
    bool IsHeld(int channel, int pitch) const;

    /** @brief Messages refused — a concurrent or re-entrant send, or a numeric
     *         list that is not a MIDI message. Monotonic, readable from any
     *         thread; diagnostics and tests only. */
    std::uint64_t Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

  private:
    /** @brief 32-bit words a channel's 128 pitches take. */
    static constexpr int WORDS = PITCHES / 32;

    // One byte through the tracking state machine. Emits nothing: the
    // pass-through is the caller's, so that a message goes out whole and in the
    // spelling it arrived in rather than a byte at a time.
    void Byte(unsigned char value);

    // A complete channel-voice message. Only the note kinds move the bitmap.
    void Voice(unsigned char status);

    // Set or clear one bit, keeping `heldCount` with it. A repeated note-on for
    // a pitch already sounding is one note, and a release for a pitch that is
    // not sounding is nothing — which is what makes the count a count.
    void Mark(int channel, int pitch, bool on);

    // Build and send one note-off. Channel is the 0-15 wire nibble.
    void Emit(int channel, int pitch, YSE::THREAD thread);

    bool Enter();
    void Leave();

    // What is sounding. Fixed size and allocated with the object: this is
    // written from the audio thread, where a set that could rehash or grow has
    // no place.
    std::uint32_t held[CHANNELS][WORDS] = {};
    int heldCount = 0;

    // The stream decoder, which is `.midiparse`'s minus everything that only
    // mattered for reporting. Running status: the channel-voice status byte to
    // read the next data bytes under, or 0 when there is none.
    unsigned char runningStatus = 0;
    unsigned char data[2] = {0, 0};
    int dataCount = 0;

    // A system-common message part-way through its data bytes, and the length
    // still owed. Skipped rather than decoded — the point of following them is
    // that their data bytes are not notes.
    unsigned char systemStatus = 0;
    int systemNeeded = 0;

    bool inSysEx = false;

    // The outgoing note-off's text. Reserved in the constructor; never grown.
    std::string scratch;

    std::atomic<bool> busy{false};
    std::atomic<std::uint64_t> dropped{0};
  };

} // namespace PATCHER
} // namespace YSE
