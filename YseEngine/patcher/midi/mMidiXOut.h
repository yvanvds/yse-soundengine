#pragma once
// The extended-precision MIDI *sender* objects (issue #533) — `.xbendout`,
// `.xbendout2`, `.xctlout` and `.xnoteout`.
//
// Direct siblings of `.bendout` (#532) and the six senders it belongs to: one
// hot inlet that both stores and fires, cold inlets for the values that ride
// along with it, a `channel` creation argument rather than Max's cold inlet,
// and a raw three-byte string on the outlet — the shape `.midiout` reads and
// the reason these objects exist.
//
// Where each differs from its seven-bit sibling is only in what it puts in the
// data bytes: a bend as all fourteen of its bits rather than the coarse seven,
// a controller as the MSB/LSB pair MIDI defines for it, and a note-off with
// the release velocity the message has always had room for.
//
// The four classes are spelled out rather than opened with `PATCHER_CLASS`.
// That macro opens a class body, and clang-format cannot follow more than a
// couple of them in one file — it loses count of the braces and starts
// splitting `};` across two lines. Four in a row is past that point, so the
// boilerplate is written out and the file stays format-stable.
//
// **No platform guard.** None of these opens a device — they only format bytes
// onto their outlets — so all four are compiled and registered on every
// platform, like the seven senders they belong with. Issue #746 lifted the
// `#if YSE_WINDOWS` the family used to carry; only `.midiout`, which holds an
// RtMidi port, is still conditional (on `YSE_ENABLE_MIDI_DEVICE`).
#include "../pObject.h"

namespace YSE {
  namespace PATCHER {

    /** @brief Coarse controller numbers that have a fine partner, and so the
     *         range `.xctlout` can address. */
    constexpr int kXPairedControllers = 32;

    /**
     *  @brief `.xbendout` — pitch bend from one fourteen-bit number (issue
     *         #533).
     *
     *  `.bendout` sends the value as the coarse byte with the fine byte zeroed,
     *  which is Max's `bendout` and the 7-bit resolution the rest of the
     *  patcher's MIDI agrees on. This object sends both bytes, so a patch can
     *  bend in steps of about a fortieth of a cent instead of three cents and
     *  a slow bend stops sounding like a staircase.
     *
     *  Inlet 0 is hot: it stores the value *and* emits, so a sweep is a stream
     *  of values in and a stream of messages out. The value is clamped into
     *  0-16383 rather than refused, as everywhere in this family, so a patch
     *  scaling a controller into a wider range bends to the extremes instead
     *  of falling silent.
     */
    class mXBendOut : public pObject {
    public:
      mXBendOut();
      const char* Type() const override {
        return YSE::OBJ::M_XBENDOUT;
      }
      CREATE(mXBendOut)
      _NO_MESSAGES
      _DO_CALCULATE

      _INT_IN(SetIntValue)

    private:
      int cvalue;
      int channel;
    };

    /**
     *  @brief `.xbendout2` — pitch bend from its two bytes (issue #533).
     *
     *  The same message `.xbendout` sends, given as the coarse and fine bytes
     *  rather than as one number. It is `.xbendin2`'s partner: the two halves
     *  come out of that object and go into this one unchanged, so a wheel
     *  position round-trips byte for byte without ever being combined and
     *  re-split.
     *
     *  Inlet 0 (MSB) is hot and inlet 1 (LSB) is cold, which is Max's
     *  arrangement and the family's: the value that arrives last on the coarse
     *  byte is the one that fires the message.
     */
    class mXBendOut2 : public pObject {
    public:
      mXBendOut2();
      const char* Type() const override {
        return YSE::OBJ::M_XBENDOUT2;
      }
      CREATE(mXBendOut2)
      _NO_MESSAGES
      _DO_CALCULATE

      _INT_IN(SetIntMsb)
      _INT_IN(SetIntLsb)

    private:
      int msb;
      int lsb;
      int channel;
    };

    /**
     *  @brief `.xctlout` — a controller as the MSB/LSB pair MIDI defines for it
     *         (issue #533).
     *
     *  MIDI pairs controller *n* (0-31) with controller *n+32* as the coarse
     *  and fine halves of one fourteen-bit value. `.controlchange` sends one
     *  seven-bit message; this object sends **two** messages per event, the
     *  coarse one first and the fine one immediately after, which is the order
     *  the specification requires and the order every receiver expects.
     *
     *  Two messages means two sends on the outlet rather than one six-byte
     *  list: `.midiout` hands each list it receives to the device as a single
     *  MIDI message, so six bytes in one list would arrive as one malformed
     *  message rather than as two good ones.
     *
     *  Inlet 0 (value, 0-16383) is hot; inlet 1 (the coarse controller number,
     *  0-31) is cold. Only the 32 paired controllers can be addressed — a
     *  number above 31 has no fine half, and sending its "LSB" would be writing
     *  to an unrelated controller 32 higher up.
     */
    class mXCtlOut : public pObject {
    public:
      mXCtlOut();
      const char* Type() const override {
        return YSE::OBJ::M_XCTLOUT;
      }
      CREATE(mXCtlOut)
      _NO_MESSAGES
      _DO_CALCULATE

      _INT_IN(SetIntValue)
      _INT_IN(SetIntController)

    private:
      int cvalue;
      int controller;
      int channel;
    };

    /**
     *  @brief `.xnoteout` — notes with the release velocity the wire has room
     *         for (issue #533).
     *
     *  `.noteon` and `.noteoff` are two boxes and neither can send a release
     *  velocity: `.noteoff` sends a note-off with velocity 0. This object is
     *  both of them plus that number — velocity 0 on the middle inlet sends a
     *  real note-off (status 0x80) carrying the release velocity from the third
     *  inlet, and any other velocity sends a note-on.
     *
     *  That is Max's `xnoteout`, and it is the sending half of what `.xnotein`
     *  reports: a key lifted slowly and a key released sharply are different
     *  gestures, and on an ADSR-shaped voice they are audibly different
     *  releases.
     *
     *  Inlet 0 (pitch) is hot; inlets 1 (velocity) and 2 (release velocity) are
     *  cold and store until the next pitch, which is the shape `.noteon`
     *  already has.
     */
    class mXNoteOut : public pObject {
    public:
      mXNoteOut();
      const char* Type() const override {
        return YSE::OBJ::M_XNOTEOUT;
      }
      CREATE(mXNoteOut)
      _NO_MESSAGES
      _DO_CALCULATE

      _INT_IN(SetIntPitch)
      _INT_IN(SetIntVelocity)
      _INT_IN(SetIntRelease)

    private:
      int pitch;
      int velocity;
      int release;
      int channel;
    };

  } // namespace PATCHER
} // namespace YSE
