#pragma once
// `.midiparse` and `.midiformat` (issue #530) — the general-purpose MIDI codec
// pair.
//
// Deliberately *not* guarded on YSE_ENABLE_MIDI_DEVICE, unlike every other
// object in this directory. These two open no device, hold no port and touch
// no backend: they turn bytes into structure and structure back into bytes,
// which is arithmetic. A patch that reads a MIDI file through `.seq`, or drives
// a software synth built out of patcher objects, needs them on a platform with
// no MIDI hardware at all — and a patch that loads on Windows and silently
// loses two of its boxes on Android is exactly the failure the unconditional
// registration of the older `mMidi*` formatters already avoids.
#include "../pObject.h"

#include <atomic>
#include <cstdint>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief `.midiparse` — raw MIDI bytes in, structured messages out (issue
     *         #530).
     *
     *  ### What it is for
     *
     *  `.midiin` (issue #529) hands a patch the wire: an undecoded stream of
     *  bytes, status and data alike, in the order the device sent them. That is
     *  the honest thing for it to do and it is unusable on its own — reading a
     *  note off it means knowing that 0x90 is a note-on, that the two bytes
     *  after it belong to it, that a second pair of bytes with no status in
     *  front is *another* note on the same channel, and that a timing clock may
     *  arrive in the middle of all of it. This object knows those rules, and it
     *  is what makes the byte stream something a patch can wire.
     *
     *  It is also the only object that can decode a stream that did not come
     *  from a device. `.seq` records and plays raw bytes; a MIDI file read
     *  through it comes out as bytes; a byte stream synthesised by a patch is
     *  still a byte stream. The seven `.notein`-family objects decode a *port*,
     *  and this one decodes a *stream*, which is a strictly larger job.
     *
     *  ### The three things that make MIDI harder than it looks
     *
     *  **Running status.** A status byte may be omitted when it repeats, so a
     *  chord arrives as one 0x90 followed by pairs of data bytes. The status is
     *  remembered until another one replaces it; a system-common message clears
     *  it and a system real-time message does not.
     *
     *  **Interleaving.** A real-time byte (0xF8-0xFF) may appear *between* the
     *  bytes of any other message, including inside a system-exclusive dump.
     *  It is reported at once on the raw outlet and the message it interrupted
     *  carries on where it left off.
     *
     *  **Messages that do not fit.** System exclusive is arbitrarily long. It
     *  is collected into a fixed buffer and emitted in consecutive chunks when
     *  it outgrows one — the first beginning with 240 and the last ending with
     *  247, the same treatment `MIDI::inHub` already gives a message too long
     *  for one transport event. Nothing is dropped and nothing is allocated.
     *
     *  ### What comes out where
     *
     *  Max's outlet order, which is also `.midiformat`'s inlet order, so the
     *  pair wires straight across:
     *
     *    0 note (list: pitch, velocity)   4 aftertouch (int)
     *    1 poly pressure (list)           5 pitch bend (int)
     *    2 control change (list)          6 channel (int, 1-16)
     *    3 program change (int, 1-128)    7 everything else (list of bytes)
     *
     *  The channel outlet fires **before** the typed one, as the input family's
     *  outlets do, so whatever the value triggers downstream already knows
     *  which channel it belongs to.
     *
     *  Three readings are inherited from Max and shared with the `.notein`
     *  family so that a value means the same thing wherever a patch reads it:
     *  a note-off is reported as that pitch with **velocity 0** (which also
     *  folds together the two spellings hardware uses for a release), a program
     *  change is **1-128** rather than the wire's 0-127, and pitch bend is the
     *  **coarse byte alone**, 0-127 centred at 64. The last two of those are
     *  lossy in a round trip through `.midiformat` — a note-off comes back as a
     *  note-on with velocity 0, and a bend comes back with its fine byte zeroed
     *  — and that is Max's behaviour rather than an oversight here. Full bend
     *  resolution belongs to `.xbendin` (issue #533).
     *
     *  ### Real-time behaviour
     *
     *  A list handler runs on whichever thread sent the message, routinely the
     *  audio callback. The state machine is plain integers, the outgoing text
     *  is built into strings reserved once in the constructor, and nothing on
     *  the path allocates, locks or blocks. Two threads sending at once — or a
     *  patch that wires an outlet back into the inlet — are refused by a
     *  test-and-set guard whose loser is counted on `Dropped()` rather than
     *  made to spin, which is `.pack`'s and `.zl`'s arrangement and for the
     *  same reason.
     */
    PATCHER_CLASS(mMidiParse, YSE::OBJ::M_PARSE)
    _NO_MESSAGES
    _NO_CALCULATE

    _INT_IN(ParseInt)
    _FLOAT_IN(ParseFloat)
    _LIST_IN(ParseList)

  public:
    /** @brief Bytes of one system message the raw outlet emits at a time. A
     *         longer system-exclusive dump leaves in consecutive chunks of this
     *         size, which is what keeps the object's memory fixed. */
    static constexpr int RAW_CHUNK_BYTES = 128;

    /** @brief Messages refused because the object was already mid-message on
     *         another thread, or because a patch wired its own outlet back into
     *         its inlet. Monotonic, readable from any thread; diagnostics and
     *         tests only. */
    std::uint64_t Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

    /** @brief The channel-voice status byte running status is currently
     *         holding, or 0 when there is none. Diagnostic surface: it is the
     *         one piece of this object's behaviour that is invisible from the
     *         outlets alone. */
    int RunningStatus() const {
      return runningStatus;
    }

  private:
    // One byte through the state machine. Everything below runs under the
    // guard, on one thread at a time.
    void Byte(unsigned char value, YSE::THREAD thread);
    void Voice(unsigned char status, YSE::THREAD thread);
    void Pair(int outlet, int first, int second, YSE::THREAD thread);

    // The raw outlet's buffer: begin a message, append a byte (flushing a full
    // chunk as it goes), and send whatever is left.
    void RawBegin();
    void RawByte(unsigned char value, YSE::THREAD thread);
    void RawFlush(YSE::THREAD thread);

    bool Enter();
    void Leave();

    // Running status: the channel-voice status byte to read the next data bytes
    // under, or 0 when there is none. Cleared by a system-common message and
    // untouched by a system real-time one, which is the rule that lets a clock
    // pass through a chord without breaking it.
    unsigned char runningStatus = 0;
    unsigned char data[2] = {0, 0};
    int dataCount = 0;

    // A system-common message (0xF1-0xF6) part-way through its data bytes, or 0
    // when there is none. Collected on the raw outlet rather than decoded:
    // song position and MIDI time code are protocol a patch reads itself.
    unsigned char systemStatus = 0;
    int systemNeeded = 0;

    bool inSysEx = false;

    // Text of the raw outlet's current message, and how many bytes are in it.
    // Reserved in the constructor; never grown afterwards.
    std::string raw;
    int rawCount = 0;

    // Text of a typed list outlet, and of the one-byte raw messages that may
    // arrive in the middle of a longer one. Separate from `raw` precisely
    // because a real-time byte can interrupt a system-exclusive dump.
    std::string scratch;

    std::atomic<bool> busy{false};
    std::atomic<std::uint64_t> dropped{0};
  };

  /**
   *  @brief `.midiformat` — structured messages in, raw MIDI bytes out (issue
   *         #530).
   *
   *  The inverse of `.midiparse`, inlet for outlet, so the two wire straight
   *  across and a patch may take a stream apart, edit the part it cares about
   *  and put it back together without ever spelling a status byte itself.
   *  It is also the combined form of the six single-purpose `mMidi*`
   *  formatters (`.noteon`, `.noteoff`, `.controlchange`, `.polypressure`,
   *  `.channelpressure`, `.programchange`): one box with one channel setting
   *  instead of six with six.
   *
   *  ### The inlets
   *
   *    0 note (list: pitch, velocity)   4 aftertouch (int)
   *    1 poly pressure (list)           5 pitch bend (int)
   *    2 control change (list)          6 channel (int, 1-16, cold)
   *    3 program change (int, 1-128)    7 raw bytes (list, passed through)
   *
   *  Every inlet but the channel is hot: a message arriving at it produces a
   *  MIDI message at once, using the channel the right inlet is holding.
   *  Inlet 6 stores and emits nothing, which is Max's rule for it.
   *
   *  Inlet 7 is **not** Max's — Max's `midiformat` has seven inlets and no
   *  way to pass a system message through. It is here so that the pair is a
   *  true inverse: `.midiparse`'s rightmost outlet carries the system
   *  exclusive, song position and real-time traffic it could not decode, and
   *  without somewhere for that to go back in, a patch that took a stream
   *  apart could not put the whole of it back together. Putting it after the
   *  channel inlet rather than before keeps every one of Max's inlet numbers
   *  where Max has it, so a patch brought across from Max wires the same.
   *
   *  ### What comes out
   *
   *  One complete MIDI message per bang of an inlet, as a **list of byte
   *  values in decimal** — `144 60 100` for a note-on. Max sends its bytes
   *  one at a time as ints, because Max's `midiout` reassembles them; this
   *  patcher's transport is list text and its `.seq` already documents a
   *  numeric byte list as "the shape midiformat's output actually has here",
   *  so a whole message per list is both the local convention and the safer
   *  one — three bytes that travel together cannot be split across two blocks
   *  by anything downstream.
   *
   *  Values are clamped into the ranges MIDI has rather than refused, so a
   *  patch that scales a control into 0-140 produces sensible notes at the
   *  top of the range instead of silence. The exception is inlet 7, where a
   *  value outside 0-255 is not a byte at all and is dropped and counted:
   *  clamping a status byte would change which message it is.
   *
   *  ### Real-time behaviour
   *
   *  Identical to `.midiparse`'s: outgoing text is built into a string
   *  reserved once in the constructor, nothing on a message path allocates,
   *  locks or blocks, and concurrent or re-entrant sends are refused by a
   *  test-and-set guard and counted on `Dropped()`.
   */
  PATCHER_CLASS(mMidiFormat, YSE::OBJ::M_FORMAT)
  _NO_MESSAGES
  _NO_CALCULATE

  _INT_IN(FormatInt)
  _FLOAT_IN(FormatFloat)
  _LIST_IN(FormatList)

public:
  /** @brief Bytes one `raw` message may carry. A longer list is truncated
   *         and the surplus counted, the buffer behind it being fixed. */
  static constexpr int RAW_MAX_BYTES = 128;

  /** @brief The channel every message is formatted on, 1-16. */
  int Channel() const {
    return channel;
  }

  /** @brief Messages refused — a concurrent or re-entrant send, or a `raw`
   *         value that is not a byte. Monotonic, readable from any thread;
   *         diagnostics and tests only. */
  std::uint64_t Dropped() const {
    return dropped.load(std::memory_order_relaxed);
  }

private:
  // Two data bytes, one data byte: build the message and send it.
  void Emit(unsigned char status, int first, int second, YSE::THREAD thread);
  void Emit(unsigned char status, int first, YSE::THREAD thread);

  // The two-number inlets (note, poly pressure, control change), whose int
  // form is the first number with the second left at 0.
  void Two(int inlet, int first, int second, YSE::THREAD thread);

  bool Enter();
  void Leave();

  int channel = 1;

  // The outgoing message text. Reserved in the constructor; never grown.
  std::string scratch;

  std::atomic<bool> busy{false};
  std::atomic<std::uint64_t> dropped{0};
};

} // namespace PATCHER
} // namespace YSE
