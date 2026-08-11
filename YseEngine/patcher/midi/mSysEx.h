#pragma once
// `.sysexin` and `.sxformat` (issue #531) — the system-exclusive pair.
//
// The two halves are guarded differently, which is why they share a file rather
// than a base class. `.sxformat` builds a message out of numbers and opens no
// device, so it is compiled and registered everywhere, like the `.midiparse` /
// `.midiformat` codec of #530. `.sysexin` listens to a hardware input port, so
// it lives behind YSE_ENABLE_MIDI_DEVICE with the rest of the input family of
// #529 — a patch on a platform with no MIDI backend can still *build* a dump to
// write to a file, it just cannot receive one.
#include "headers/defines.hpp"

#include "../pObject.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#if YSE_ENABLE_MIDI_DEVICE
#include "mMidiIn.h"
#endif

namespace YSE {
  namespace PATCHER {

    /** @brief Placeholders `.sxformat` understands: `$i1` to `$i9`, Max's
     *         range, and therefore the most inlets the object can have. */
    constexpr int kSxMaxVars = 9;

    /**
     *  @brief `.sxformat` — builds a MIDI system-exclusive message from a
     *         template (issue #531).
     *
     *  ### What it is for
     *
     *  System exclusive is how a synthesiser's own parameters are reached: a
     *  voice dump, a patch request, a single parameter poke that no control
     *  change is defined for. Every one of those is the same message with two or
     *  three bytes changed, which is exactly what this object is — the constant
     *  bytes are the creation arguments, the changing ones are `$i` placeholders
     *  fed from inlets, and a bang or a value on the hot inlet emits the whole
     *  thing.
     *
     *  Without it a patch would have to spell out the manufacturer ID, the
     *  device ID, the address and the checksum with a `.pack` and a chain of
     *  arithmetic boxes for every message it wanted to send, and recompute the
     *  checksum by hand each time a byte moved.
     *
     *  ### The template
     *
     *  The creation arguments are the message, one token per byte:
     *
     *    - **a number, 0-255** — a constant byte, written as it stands. A
     *      well-formed message starts with 240 and ends with 247; that is not
     *      enforced, because a patch may legitimately build a fragment and put
     *      the wrapper on elsewhere.
     *    - **`$i1` .. `$i9`** — the value the matching inlet is holding. `$i1`
     *      is the leftmost (hot) inlet, and the object has one inlet past the
     *      highest placeholder its template mentions. A placeholder that has
     *      never been given a value stands for 0.
     *    - **`sumstart`** — emits nothing; marks where the checksum region
     *      begins.
     *    - **`sum`** — the Roland-style checksum of the region: the byte that
     *      makes the region's total a multiple of 128, `(128 - (total & 127)) &
     *      127`. With no `sumstart` in front of it the region is the whole
     *      message so far, minus a leading 240 — the sensible reading for the
     *      short parameter messages that carry a checksum at all.
     *
     *  A **negative** value on a `$i` placeholder emits *no byte at all*, which
     *  is Max's rule and what makes a variable-length message possible: one
     *  template with trailing optional bytes covers the short form and the long
     *  one. A value above 127 is clamped rather than dropped — a byte with its
     *  top bit set inside a dump would be read as a status byte by the receiving
     *  device and would end the message early, so passing it through would
     *  corrupt the stream rather than merely misreport a number.
     *
     *  ### What comes out
     *
     *  One complete message per hot-inlet event, as a **list of byte values in
     *  decimal**, which is `.midiformat`'s output shape and the shape `.seq` and
     *  `.midiparse` read (issue #530). Max sends its bytes out one at a time
     *  because Max's `midiout` reassembles them; here a whole message per list
     *  is both the local convention and the safer one, since a dump split across
     *  two blocks by something downstream would arrive at a device as two
     *  broken messages.
     *
     *  ### The maximum length
     *
     *  `MAX_BYTES` tokens. A message is built into a buffer reserved when the
     *  object is created, because building it happens on whichever thread sent
     *  the message — routinely the audio callback, where growing a buffer is not
     *  allowed. A template longer than that is **cut at the limit and reported
     *  to the log** when the arguments are parsed, on the control thread, rather
     *  than silently truncated at send time: a dump that is wrong by its tail is
     *  the kind of thing that corrupts a synthesiser's memory, so it must not be
     *  possible to discover it only by listening.
     *
     *  A bulk dump of thousands of bytes is not this object's job and never was
     *  — a template with four thousand tokens in it is not a template. That
     *  belongs in a `.coll` or a file, played out through `.midiformat`'s raw
     *  inlet.
     *
     *  ### Real-time behaviour
     *
     *  The template is compiled once, in `ParseParams` on the control thread,
     *  into a fixed array of tokens. `Calculate()` walks that array, appends
     *  decimal digits into the reserved string with the patcher's own
     *  `WriteInt`, and sends: no parsing, no allocation, no lock, no I/O and no
     *  locale state. Concurrent and re-entrant sends — a patch that wires the
     *  outlet back into an inlet — are refused by a test-and-set guard whose
     *  loser is counted on `Dropped()` rather than made to spin, which is
     *  `.midiformat`'s and `.zl`'s arrangement.
     */
    PATCHER_CLASS(mSxFormat, YSE::OBJ::M_SXFORMAT)
    _NO_MESSAGES
    _DO_CALCULATE

    _INT_IN(SetInt)
    _FLOAT_IN(SetFloat)
    _BANG_IN(SetBang)
    _LIST_IN(SetList)

    _PARM_CLEAR
    _PARM_PARSE

  public:
    /** @brief Bytes one message may carry, and tokens one template may hold.
     *         See the class comment for why a longer template is reported
     *         rather than quietly cut at send time. */
    static constexpr int MAX_BYTES = 256;

    /** @brief Sends refused because the object was already building a message
     *         on another thread, or because a patch wired its own outlet back
     *         into its inlet. Monotonic; diagnostics and tests only. */
    std::uint64_t Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

    /** @brief Tokens the current template compiled to. 0 for an object with no
     *         arguments, which emits nothing. */
    int TemplateSize() const {
      return programSize;
    }

    /** @brief Empty when the template parsed cleanly; otherwise the last
     *         complaint that was logged. Control thread only. */
    const std::string& CompileError() const {
      return compileError;
    }

  private:
    // One compiled template token. A Literal carries its byte, a Var the
    // placeholder index it reads, and the two markers carry nothing.
    struct token {
      enum class kind : unsigned char { Literal, Var, Checksum, SumStart };
      kind what = kind::Literal;
      unsigned char value = 0;
    };

    // Appends one byte to the message being built and to the checksum region.
    void Emit(unsigned char byte);

    bool Enter();
    void Leave();

    // The raw creation argument, one token per space. Registered as a LIST
    // param so the whole template survives Parameters::Set intact.
    std::vector<std::string> sysex;

    // The compiled template. Fixed size on purpose: Calculate() must not look
    // at anything that could have been reallocated under it.
    token program[MAX_BYTES];
    int programSize = 0;

    // Per-inlet stored values, sized for all nine placeholders so a template
    // compiled for fewer can never index past the end. Ints rather than floats:
    // a MIDI byte is an integer and the fractional part has nowhere to go.
    int vars[kSxMaxVars] = {};

    // Live state of one build. Members rather than locals only so Emit() can
    // reach them; nothing survives a call.
    int emitted = 0;
    int checksum = 0;

    // The outgoing message text. Reserved in the constructor; never grown.
    std::string scratch;

    std::string compileError;

    std::atomic<bool> busy{false};
    std::atomic<std::uint64_t> dropped{0};
  };

#if YSE_ENABLE_MIDI_DEVICE

  /**
   *  @brief `.sysexin` — system-exclusive messages straight off a MIDI input
   *         port (issue #531).
   *
   *  ### What it is for
   *
   *  Max's `sysexin`, and `.midiin` with everything that is not a dump taken
   *  out. A patch that asks a synthesiser for its current voice gets the
   *  answer here: the bytes of every system-exclusive message received on the
   *  port, one int at a time, from the leading 240 to the closing 247, with no
   *  note, clock or controller traffic mixed in.
   *
   *  That filtering is the whole object. A `.midiin` would deliver the dump
   *  too, but interleaved with whatever else the device is sending, and a
   *  patch would have to run its own state machine to tell the two apart —
   *  which is the state machine that lives here.
   *
   *  ### No maximum length, because nothing is stored
   *
   *  A dump is arbitrarily long — a DX7 bank is four thousand bytes — and this
   *  object never holds one. Bytes are forwarded as they arrive, so the only
   *  state is a single flag saying whether a message is open. There is
   *  therefore no buffer to overflow and no length to document: the object
   *  cannot truncate a dump, because it never has one in its hands. A patch
   *  that wants the message whole collects it downstream, where allocating is
   *  allowed, and watches for the 247 that ends it.
   *
   *  The one bound in the path is the transport's, and it is not this
   *  object's: `MIDI::inHub` holds a bounded queue per subscriber and drops
   *  and counts when the audio thread has stalled, saying so through the log.
   *  A dump arriving at the MIDI wire's 3125 bytes a second against a drain of
   *  a queue's worth per block is not close to that ceiling.
   *
   *  ### The rules it applies
   *
   *  - **240 opens** a message and is reported; **247 closes** it and is
   *    reported. Both are part of the dump as a device transmits it, so a
   *    patch that logs the bytes gets back what came off the wire.
   *  - **A real-time byte (248-255) is dropped**, and does not close the
   *    message. It may legally appear *between* two bytes of a dump; a device
   *    that sends a clock mid-transfer would otherwise put a 248 in the middle
   *    of the voice data. `.rtin` is where those belong.
   *  - **Any other status byte ends the message** without being reported.
   *    Hardware interrupted mid-dump simply stops sending and starts something
   *    else, and a state machine that waited for an EOX that is never coming
   *    would treat every later note as voice data.
   *  - **Everything outside a message is ignored**, which is what makes this
   *    object different from `.midiin`.
   *
   *  ### Real-time behaviour
   *
   *  Inherited whole from `mMidiInBase` (issue #529), which is why this class
   *  is eight lines long: the port, the subscription, the block poll and the
   *  bounded drain are the family's, and `Receive` here is a walk of at most
   *  eight bytes with one send each. Nothing allocates, locks or blocks.
   */
  class mSysExIn : public mMidiInBase {
  public:
    mSysExIn();
    const char* Type() const override {
      return YSE::OBJ::M_SYSEXIN;
    }
    CREATE(mSysExIn)

    /** @brief Whether a system-exclusive message is currently open — a dump
     *         has begun and has not yet been ended. Diagnostic surface: it is
     *         the object's only state, and invisible from the outlet. */
    bool InMessage() const {
      return inSysEx;
    }

  protected:
    void Receive(const YSE::MIDI::inEvent& event, YSE::THREAD thread) override;

  private:
    bool inSysEx = false;
  };

#endif // YSE_ENABLE_MIDI_DEVICE

} // namespace PATCHER
} // namespace YSE
