/*
  ==============================================================================

    midiSynthRouting.hpp
    Convert a raw MIDI channel-voice message into normalized YSE::synth calls.

    Shared by both MIDI input sources that feed a synth (issue #155):
      * MIDI file playback  — midifileImplementation.cpp
      * MIDI device input    — device.cpp (RtMidi dispatch)

    The synth interface takes device-agnostic, normalized ranges (velocity /
    controller / aftertouch in [0, 1]; pitch wheel in [-1, 1]) — see
    docs/design/synth_core.md §12. This header is the single place that maps
    raw 7-bit / 14-bit MIDI onto those ranges, so both input paths stay
    consistent and the mapping is unit-testable in isolation.

    Templated on the target so it can drive either the real YSE::synth (its
    public note API) or a recording sink in tests, with zero overhead and no
    coupling to the synth headers.

  ==============================================================================
*/

#ifndef YSE_MIDI_MIDISYNTHROUTING_HPP
#define YSE_MIDI_MIDISYNTHROUTING_HPP

namespace YSE {
  namespace MIDI {

    // MIDI status nibbles (high nibble of the status byte).
    enum : unsigned char {
      MSG_NOTE_OFF = 0x80,
      MSG_NOTE_ON = 0x90,
      MSG_POLY_AFTERTOUCH = 0xA0,
      MSG_CONTROL_CHANGE = 0xB0,
      MSG_PROGRAM_CHANGE = 0xC0,
      MSG_CHANNEL_AFTERTOUCH = 0xD0,
      MSG_PITCH_BEND = 0xE0,
    };

    /** True for the seven channel-voice status nibbles (0x80..0xE0). Everything
        else (system / real-time / meta) is not routed to the synth core. */
    inline bool isChannelVoiceStatus(unsigned char statusNibble) {
      return statusNibble >= MSG_NOTE_OFF && statusNibble <= MSG_PITCH_BEND;
    }

    /**
     *  Route one channel-voice MIDI message to a synth-like ``target``.
     *
     *  @param target       Anything exposing the YSE::synth note API
     *                       (noteOn/noteOff/controller/pitchWheel/aftertouch).
     *  @param statusNibble  High nibble of the status byte (0x80..0xE0).
     *  @param channelNibble Low nibble of the status byte (0..15). Mapped to a
     *                       1..16 synth channel to match the synth API.
     *  @param data1,data2   The two MIDI data bytes (0..127). ``data2`` is
     *                       ignored for the single-byte messages.
     *
     *  Non-channel-voice statuses (program change, system, meta) are ignored:
     *  the synth core has no concept of them. A NOTE_ON with velocity 0 is the
     *  conventional running-status NOTE_OFF and is routed as such.
     */
    template <class SynthTarget>
    inline void routeChannelVoiceMessage(SynthTarget& target, unsigned char statusNibble,
                                         unsigned char channelNibble, unsigned char data1,
                                         unsigned char data2) {
      const int channel = static_cast<int>(channelNibble) + 1; // synth channels are 1..16
      const int note = static_cast<int>(data1);

      switch (statusNibble & 0xF0) {
      case MSG_NOTE_ON:
        // Velocity 0 is a note-off by convention.
        if (data2 == 0) {
          target.noteOff(channel, note, 0.f);
        } else {
          target.noteOn(channel, note, static_cast<float>(data2) / 127.f);
        }
        break;

      case MSG_NOTE_OFF:
        target.noteOff(channel, note, static_cast<float>(data2) / 127.f);
        break;

      case MSG_CONTROL_CHANGE:
        // CC 64 / 66 / 67 are intercepted by the synth as the sustain /
        // sostenuto / soft pedals; every other CC is stored per channel. The
        // synth does that interception itself, so we forward every CC as-is.
        target.controller(channel, note, static_cast<float>(data2) / 127.f);
        break;

      case MSG_PITCH_BEND: {
        // 14-bit value, LSB then MSB, centre 8192. Normalize to [-1, 1].
        const int raw = (static_cast<int>(data2) << 7) | static_cast<int>(data1);
        target.pitchWheel(channel, static_cast<float>(raw - 8192) / 8192.f);
        break;
      }

      case MSG_POLY_AFTERTOUCH:
        // Per-note pressure: data1 = note, data2 = pressure.
        target.aftertouch(channel, note, static_cast<float>(data2) / 127.f);
        break;

      case MSG_CHANNEL_AFTERTOUCH:
        // Channel-wide pressure (data1 = pressure). note == -1 broadcasts to
        // every voice on the channel.
        target.aftertouch(channel, -1, static_cast<float>(data1) / 127.f);
        break;

      default:
        // Program change (0xC0) and anything else: not part of the synth core.
        break;
      }
    }

  } // namespace MIDI
} // namespace YSE

#endif // YSE_MIDI_MIDISYNTHROUTING_HPP
