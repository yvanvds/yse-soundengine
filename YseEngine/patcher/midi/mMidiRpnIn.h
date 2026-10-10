#pragma once
// The RPN/NRPN *input* objects (issue #534) — `.rpnin` and `.nrpnin`.
//
// Built on the MIDI-input family's plumbing (#529): `mMidiInBase` owns the
// port, the `MIDI::inHub` subscription, the block poll and the bounded wait-free
// drain, and `mMidiChannelInBase` owns the channel filter. What is new here is
// the only real logic in the issue — a parameter-number sequence is not one
// message but four control changes, so these objects are small per-channel
// state machines rather than decoders.
//
// Guarded on YSE_ENABLE_MIDI_DEVICE alone, like mMidiIn.h and mMidiXIn.h and
// for the same reason: these objects listen to a hardware input port, and on a
// platform with no MIDI backend there is no port to listen to. When the option
// is OFF this whole file is empty and the objects are not registered.
#include "headers/defines.hpp"

#if YSE_ENABLE_MIDI_DEVICE

#include "mMidiIn.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Shared body of `.rpnin` and `.nrpnin` (issue #534).
     *
     *  ### Why this is a state machine and `.ctlin` is not
     *
     *  Every other MIDI input object decodes one message. A parameter number
     *  write is a *sequence* of four control changes: two that select a 14-bit
     *  parameter number, and two that write a 14-bit value into whatever is
     *  selected. Nothing in the wire format ties them together — the value
     *  bytes carry no parameter number of their own — so the only way to report
     *  "parameter 7 was set to 3000" is to remember what was selected when the
     *  value arrived.
     *
     *  That remembering is per channel, because two devices on one cable
     *  select independently and a `.rpnin` with no channel argument hears both.
     *  Sixteen fixed slots of four bytes each: read and written on the audio
     *  thread, so a container that could rehash or grow has no place here.
     *
     *  ### One selection register, not two
     *
     *  Registered and non-registered numbers write into the **same** selection.
     *  That is the specification's own model — it is precisely why an RPN Null
     *  exists — and it is what a real device does: controllers 101/100 and
     *  99/98 move one pointer, and the last pair used decides whether it points
     *  at a registered parameter or a non-registered one. So a `.rpnin` and a
     *  `.nrpnin` listening to the same port both track the whole sequence and
     *  each reports only when the selection is of its own kind. An interleaved
     *  stream — an NRPN write landing between two RPN writes — therefore comes
     *  out of the right box, and neither object reports a value that was
     *  written to the other's parameter.
     *
     *  ### Abandoned sequences
     *
     *  **RPN Null** (parameter number 16383, selected with 101 and 100) is the
     *  specification's "nothing is selected now", sent by well-behaved
     *  controllers after a write so a stray Data Entry cannot land somewhere
     *  unintended. It is honoured: selecting it clears the selection, and Data
     *  Entry that follows is reported by neither object. Non-registered
     *  parameter 16383 is *not* treated this way — the Null is defined for
     *  registered numbers only, and stealing a legal NRPN address would silence
     *  a device that happened to use it.
     *
     *  A sequence that is simply dropped — a controller unplugged after
     *  selecting and before writing — leaks nothing: the state is four bytes
     *  that the next selection overwrites.
     *
     *  ### When it emits
     *
     *  **On both halves of the value, not only on the fine one.** A Data Entry
     *  MSB emits at once with the fine byte read as 0, and the LSB that follows
     *  emits again with the refined value. This is `.xctlin`'s decision (#533)
     *  and it is made here for the same reason: a great many devices send the
     *  coarse byte alone, and an object that waited for the pair would be
     *  silent forever on all of them — a far worse failure than one coarse value
     *  immediately followed by its refinement.
     *
     *  ### Real-time behaviour
     *
     *  The port, the subscription, the block poll and the bounded drain are
     *  `mMidiInBase`'s. On top of them this object switches on a controller
     *  number, writes into a fixed array and sends ints out outlets. Nothing on
     *  the path allocates, locks or blocks.
     */
    class mRpnInBase : public mMidiChannelInBase {
    public:
      /** @brief MIDI channels, and so the number of selection slots kept. */
      static constexpr int CHANNELS = 16;

      /** @brief The widest a 14-bit parameter number or value can be, and the
       *         RPN Null's number. */
      static constexpr int MAX_14BIT = 16383;

      /** @brief The parameter number currently selected on `channel` (1-16) if
       *         it is of this object's kind, or -1 when nothing of this kind is
       *         selected there. Diagnostic / test surface: the selection is
       *         otherwise invisible from the outlets, which only fire when a
       *         value arrives. */
      int Selected(int channel) const;

    protected:
      /** @param registeredKind True for `.rpnin`, which reports registered
       *                        parameter numbers; false for `.nrpnin`. */
      explicit mRpnInBase(bool registeredKind);

      void Receive(const YSE::MIDI::inEvent& event, YSE::THREAD thread) override;

    private:
      // Which pair last moved the selection on a channel — or neither, which is
      // both the initial state and what an RPN Null leaves behind.
      enum class Selection : unsigned char { NONE, REGISTERED, NON_REGISTERED };

      struct ChannelState {
        Selection selection = Selection::NONE;
        unsigned char paramMsb = 0;
        unsigned char paramLsb = 0;
        // Last Data Entry MSB, so the LSB that follows can be paired with the
        // coarse byte it belongs to. Reset by every new selection: a fine byte
        // left over from the previous parameter is not this one's.
        unsigned char dataMsb = 0;
      };

      // Records a selection made by one of the two pairs and, for registered
      // numbers, honours the Null. Audio thread.
      void Select(ChannelState& state, bool registered, bool msb, int byte);

      // Sends parameter and value out, channel first. Audio thread.
      void Emit(const ChannelState& state, unsigned char nibble, int value, YSE::THREAD thread);

      ChannelState state[CHANNELS];
      const bool registered;
    };

    /**
     *  @brief `.rpnin` — registered parameter number input (issue #534).
     *
     *  Reports writes to the parameter numbers the MIDI specification itself
     *  assigns — 0 pitch-bend sensitivity, 1 fine tuning, 2 coarse tuning, 3
     *  tuning program select, 4 tuning bank select — selected on the wire with
     *  controllers 101 and 100. The receiving half of `.rpnout`.
     */
    class mRpnIn : public mRpnInBase {
    public:
      mRpnIn();
      const char* Type() const override {
        return YSE::OBJ::M_RPNIN;
      }
      CREATE(mRpnIn)
    };

    /**
     *  @brief `.nrpnin` — non-registered parameter number input (issue #534).
     *
     *  The same mechanism with controllers 99 and 98 selecting. What the
     *  numbers mean is the sending device's business rather than the
     *  specification's, which is exactly why they matter: a synthesiser's own
     *  parameters are addressed here and nowhere else. The receiving half of
     *  `.nrpnout`.
     */
    class mNrpnIn : public mRpnInBase {
    public:
      mNrpnIn();
      const char* Type() const override {
        return YSE::OBJ::M_NRPNIN;
      }
      CREATE(mNrpnIn)
    };

  } // namespace PATCHER
} // namespace YSE

#endif // YSE_ENABLE_MIDI_DEVICE
