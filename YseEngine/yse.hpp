/*
  ==============================================================================

    yse.hpp
    Created: 27 Jan 2014 7:15:01pm
    Author:  yvan

  ==============================================================================
*/

#ifndef YSE_HPP_INCLUDED
#define YSE_HPP_INCLUDED

/**
 *  @namespace YSE
 *  @brief Public API of libYSE — sound playback, mixing, and 3D positional audio.
 *
 *  Entry points: ``YSE::System`` (lifecycle and audio device), ``YSE::Listener``
 *  (3D origin), ``YSE::sound`` (a playable source), ``YSE::channel`` (mixing
 *  tree), ``YSE::reverb`` (positioned reverb zone), ``YSE::patcher`` (modular
 *  DSP graph), ``YSE::player`` (note sequencer). Sub-namespaces group
 *  domain-specific types: ``YSE::DSP`` for signal processing, ``YSE::MIDI``
 *  for MIDI I/O, ``YSE::MUSIC`` for note / chord / motif primitives.
 */

/**
 *  @namespace YSE::DSP
 *  @brief Audio buffers, oscillators, filters, envelopes, and effect modules.
 *
 *  Build chains of ``DSP::dspObject`` to process a sound, ``DSP::dspSourceObject``
 *  to feed one. Single-channel audio data lives in ``DSP::buffer`` and its
 *  drawing / file / wavetable subclasses.
 */

/**
 *  @namespace YSE::MIDI
 *  @brief MIDI file playback and external device I/O.
 *
 *  ``MIDI::file`` plays back standard MIDI files; ``midiOut`` (Windows /
 *  Linux only) sends messages to a MIDI port. The ``MIDI::midiMessage`` /
 *  ``midiNote`` types wrap raw byte sequences.
 */

/**
 *  @namespace YSE::MUSIC
 *  @brief Music-theory primitives: notes, chords, positioned notes for motifs.
 *
 *  These types feed ``YSE::scale``, ``YSE::motif``, and ``YSE::player`` to
 *  drive generative composition.
 */

#define YSE_SOUND

#include <atomic>

// helpers
#include "headers/types.hpp"
#include "headers/defines.hpp"
#include "headers/enums.hpp"
#include "headers/constants.hpp"

// dsp
#include "dsp/buffer.hpp"
#include "dsp/drawableBuffer.hpp"
#include "dsp/fileBuffer.hpp"
#include "dsp/wavetable.hpp"
#include "dsp/sample_functions.hpp"
#include "dsp/delay.hpp"
#include "dsp/dspObject.hpp"
#include "dsp/filters.hpp"
#include "dsp/math.hpp"
#include "dsp/oscillators.hpp"
#include "dsp/ramp.hpp"
#include "dsp/envelope.hpp"
#include "dsp/ADSRenvelope.hpp"
#include "dsp/lfo.hpp"

#include "dsp/modules/hilbert.hpp"
#include "dsp/modules/ringModulator.hpp"
#include "dsp/modules/sineWave.hpp"
#include "dsp/modules/granulator.hpp"
#include "dsp/modules/phaser.hpp"
#include "dsp/modules/delay/basicDelay.hpp"
#include "dsp/modules/delay/highpassDelay.hpp"
#include "dsp/modules/delay/lowpassDelay.hpp"

#include "dsp/modules/filters/highpass.hpp"
#include "dsp/modules/filters/lowpass.hpp"
#include "dsp/modules/filters/bandpass.hpp"
#include "dsp/modules/fm/difference.hpp"
#include "dsp/modules/filters/sweep.hpp"

// patcher
#include "patcher/pObjectList.hpp"
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"

// patcher-as-insert adapter (issue #167)
#include "dsp/patcherInsert.hpp"

// utilities
#include "utils/misc.hpp"
#include "utils/vector.hpp"

// primary objects
#include "system.hpp"
#include "log.hpp"

#include "channel/channel.hpp"
#include "channel/channelInterface.hpp"

// #include "sound/sound.hpp"
#include "sound/soundInterface.hpp"

#include "synth/dspVoice.hpp"
#include "synth/sineVoice.hpp"
#include "synth/positionHandler.hpp"
#include "synth/positionHandlers.hpp"
#include "synth/synthInterface.hpp"

#include "midi/midifile.hpp"
#include "midi/midiMessage.hpp"
#include "midi/midiNote.hpp"
#include "midi/device.hpp"

#include "player/player.hpp"
#include "player/playerInterface.hpp"

#include "music/note.hpp"
#include "music/pNote.hpp"

#include "music/scale/scale.hpp"
#include "music/scale/scaleInterface.hpp"

#include "music/motif/motif.hpp"
#include "music/motif/motifInterface.hpp"

#include "reverb/reverb.hpp"
#include "reverb/reverbInterface.hpp"

#include "device/device.hpp"
#include "device/deviceInterface.hpp"
#include "device/deviceSetup.hpp"

#include "listener.hpp"
#include "io.hpp"
#include "BufferIO.hpp"

#endif // YSE_HPP_INCLUDED
