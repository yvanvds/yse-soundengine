#include "yse_c/yse_synth.h"
#include "yse_c_internal.hpp"
#include "yse_instrument_internal.hpp"

#include "../synth/synthInterface.hpp"
#include "../synth/sineVoice.hpp"
#include "../synth/samplerVoice.hpp"
#include "../synth/vaVoice.hpp"
#include "../synth/dspVoice.hpp"
#include "../synth/positionHandler.hpp"
#include "../synth/positionHandlers.hpp"
#include "../dsp/fm/fmVoice.hpp"
#include "../dsp/fm/fmPatch.hpp"
#include "../dsp/lfo.hpp"
#include "../sound/soundInterface.hpp"
#include "../channel/channelInterface.hpp"
#include "../utils/vector.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <memory>
#include <vector>

namespace {

  // A synth handle owns its YSE::synth plus the built-in voice prototypes
  // handed to addVoices(). The engine records a raw prototype pointer at
  // add-voices time and clones from it LATER, on the setup pool
  // (docs/design/synth_core.md §3/§8) — it never copies or owns the
  // prototype. So the C API must keep each prototype alive past setup; the
  // simplest correct owner is the handle itself, freeing them on destroy.
  // All allocation here happens on the control thread (never the audio
  // thread), matching the C++ contract.
  struct YseSynthImpl {
    YSE::synth synth;
    std::vector<std::unique_ptr<YSE::SYNTH::dspVoice>> prototypes;
    // The position-handler prototype (issue #171). Like a voice prototype, the
    // engine records a raw pointer at set-position-handler time and clones from
    // it LATER on the setup pool; it never copies or owns it. So the handle owns
    // the single attached prototype and frees it on destroy. Allocated on the
    // control thread only.
    std::unique_ptr<YSE::SYNTH::positionHandler> positionHandler;

    // The live, shared patch of the synth's VA / FM voice group (issue #178),
    // if one was added. add_voices_va / add_voices_fm record the prototype's
    // shared patch here so the yse_synth_va_set_* / yse_synth_fm_set_* setters
    // can steer it after the prototype has been cloned. The voice clones share
    // the same patch object, so a setter reaches every voice. Null until the
    // matching add-voices is called.
    std::shared_ptr<YSE::SYNTH::vaParams> vaPatch;
    std::shared_ptr<YSE::SYNTH::fmPatch> fmPatch;
  };

  inline YseSynthImpl* to_impl(YseSynth* h) {
    return reinterpret_cast<YseSynthImpl*>(h);
  }
  // (definition of yse_c::synth_from_handle appears after this namespace, once
  // to_impl / YseSynthImpl are in scope)
  inline YSE::sound* to_cpp(YseSound* s) {
    return reinterpret_cast<YSE::sound*>(s);
  }
  inline YSE::channel* to_cpp(YseChannel* ch) {
    return reinterpret_cast<YSE::channel*>(ch);
  }

} // namespace

// Cross-TU accessor declared in yse_c_internal.hpp: hand the engine synth out
// to yse_music.cpp so a player can be created bound to it (issue #268). Kept
// here, in the only TU that knows YseSynthImpl's layout.
namespace yse_c {
  YSE::synth* synth_from_handle(YseSynth* h) {
    return h ? &to_impl(h)->synth : nullptr;
  }
} // namespace yse_c

extern "C" {

// ─── lifecycle ─────────────────────────────────────────────────────────

YSE_C_API YseSynth* yse_synth_create(void) {
  try {
    auto* impl = new YseSynthImpl();
    impl->synth.create(); // register with the engine so the handle is live
    return reinterpret_cast<YseSynth*>(impl);
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return nullptr;
  } catch (...) {
    yse_c::set_last_error("yse_synth_create: unknown C++ exception");
    return nullptr;
  }
}

YSE_C_API void yse_synth_destroy(YseSynth* h) {
  if (h) delete to_impl(h);
}

YSE_C_API int yse_synth_is_valid(YseSynth* h) {
  return h && to_impl(h)->synth.isValid() ? 1 : 0;
}

YSE_C_API void yse_synth_set_name(YseSynth* h, const char* name) {
  // NULL is treated as "" (clear), so FFI callers can always pass through.
  // Runs on the control thread; the engine's name() does the bus
  // (de)registration and logs a duplicate-name rejection itself.
  if (h) to_impl(h)->synth.name(name ? name : "");
}

// ─── voice groups (built-in voices only) ───────────────────────────────

YSE_C_API YseStatus yse_synth_add_voices_sine(YseSynth* h, int num_voices, int channel,
                                              int lowest_note, int highest_note, float attack,
                                              float decay, float sustain, float release) {
  if (!h) return YSE_ERR_INVALID_HANDLE;
  if (num_voices <= 0) return YSE_ERR_INVALID_ARGUMENT;
  try {
    auto* impl = to_impl(h);
    // Build and configure the prototype, then hand ownership to the handle
    // BEFORE addVoices records its pointer — the pointed-to voice never moves
    // (only the unique_ptr does), so the pointer stays valid across the setup
    // pool's clone().
    auto proto = std::make_unique<YSE::SYNTH::sineVoice>();
    proto->attack(attack).decay(decay).sustain(sustain).release(release);
    YSE::SYNTH::dspVoice* raw = proto.get();
    impl->prototypes.push_back(std::move(proto));
    impl->synth.addVoices(*raw, num_voices, channel, lowest_note, highest_note);
    return YSE_OK;
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return YSE_ERR_EXCEPTION;
  } catch (...) {
    yse_c::set_last_error("yse_synth_add_voices_sine: unknown C++ exception");
    return YSE_ERR_EXCEPTION;
  }
}

// ─── instrument voice groups (issue #178) ──────────────────────────────

YSE_C_API YseStatus yse_synth_add_voices_sampler(YseSynth* h, YseSfzInstrument* instrument,
                                                 int num_voices, int channel, int lowest_note,
                                                 int highest_note) {
  if (!h) return YSE_ERR_INVALID_HANDLE;
  if (num_voices <= 0) return YSE_ERR_INVALID_ARGUMENT;
  // Validated against the instrument-handle registry; a NULL / destroyed handle
  // returns nullptr (and is logged) rather than dereferencing junk.
  auto inst = yse_c::sampler_instrument_from_handle(instrument);
  if (!inst) return YSE_ERR_INVALID_ARGUMENT;
  try {
    auto* impl = to_impl(h);
    // Same prototype-ownership discipline as the sine path: build the prototype,
    // hand ownership to the handle BEFORE addVoices records its raw pointer. The
    // clones share the immutable region table + resident PCM (never duplicated).
    auto proto = std::make_unique<YSE::SYNTH::samplerVoice>();
    proto->setInstrument(inst);
    YSE::SYNTH::dspVoice* raw = proto.get();
    impl->prototypes.push_back(std::move(proto));
    impl->synth.addVoices(*raw, num_voices, channel, lowest_note, highest_note);
    return YSE_OK;
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return YSE_ERR_EXCEPTION;
  } catch (...) {
    yse_c::set_last_error("yse_synth_add_voices_sampler: unknown C++ exception");
    return YSE_ERR_EXCEPTION;
  }
}

YSE_C_API YseStatus yse_synth_add_voices_va(YseSynth* h, int num_voices, int channel,
                                            int lowest_note, int highest_note) {
  if (!h) return YSE_ERR_INVALID_HANDLE;
  if (num_voices <= 0) return YSE_ERR_INVALID_ARGUMENT;
  try {
    auto* impl = to_impl(h);
    auto proto = std::make_unique<YSE::SYNTH::vaVoice>();
    // Record the shared patch so the va setters can steer it after cloning; the
    // clones share this same vaParams (carried across clone() by shared_ptr).
    impl->vaPatch = proto->patch();
    YSE::SYNTH::dspVoice* raw = proto.get();
    impl->prototypes.push_back(std::move(proto));
    impl->synth.addVoices(*raw, num_voices, channel, lowest_note, highest_note);
    return YSE_OK;
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return YSE_ERR_EXCEPTION;
  } catch (...) {
    yse_c::set_last_error("yse_synth_add_voices_va: unknown C++ exception");
    return YSE_ERR_EXCEPTION;
  }
}

YSE_C_API YseStatus yse_synth_add_voices_fm(YseSynth* h, int num_voices, int channel,
                                            int lowest_note, int highest_note) {
  if (!h) return YSE_ERR_INVALID_HANDLE;
  if (num_voices <= 0) return YSE_ERR_INVALID_ARGUMENT;
  try {
    auto* impl = to_impl(h);
    auto proto = std::make_unique<YSE::SYNTH::fmVoice>();
    impl->fmPatch = proto->patch();
    YSE::SYNTH::dspVoice* raw = proto.get();
    impl->prototypes.push_back(std::move(proto));
    impl->synth.addVoices(*raw, num_voices, channel, lowest_note, highest_note);
    return YSE_OK;
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return YSE_ERR_EXCEPTION;
  } catch (...) {
    yse_c::set_last_error("yse_synth_add_voices_fm: unknown C++ exception");
    return YSE_ERR_EXCEPTION;
  }
}

// ─── VA patch parameters (issue #178) ──────────────────────────────────

namespace {
  // The synth's VA patch, or nullptr for a NULL handle / no VA group. Setters
  // below no-op on nullptr, matching the header's null-safe contract.
  inline YSE::SYNTH::vaParams* va(YseSynth* h) {
    return h ? to_impl(h)->vaPatch.get() : nullptr;
  }
  inline bool oscInRange(int osc) {
    return osc >= 0 && osc < YSE::SYNTH::vaParams::kNumOsc;
  }
} // namespace

YSE_C_API void yse_synth_va_set_osc_wave(YseSynth* h, int osc, YseVaWaveform wave) {
  auto* p = va(h);
  if (p && oscInRange(osc))
    p->oscWave[osc].store(static_cast<int>(wave), std::memory_order_relaxed);
}
YSE_C_API void yse_synth_va_set_osc_detune(YseSynth* h, int osc, float semitones) {
  auto* p = va(h);
  if (p && oscInRange(osc)) p->oscDetune[osc].store(semitones, std::memory_order_relaxed);
}
YSE_C_API void yse_synth_va_set_osc_level(YseSynth* h, int osc, float level) {
  auto* p = va(h);
  if (p && oscInRange(osc)) p->oscLevel[osc].store(level, std::memory_order_relaxed);
}
YSE_C_API void yse_synth_va_set_osc_pulse_width(YseSynth* h, int osc, float width) {
  auto* p = va(h);
  if (p && oscInRange(osc)) p->oscPulseWidth[osc].store(width, std::memory_order_relaxed);
}
YSE_C_API void yse_synth_va_set_wavetable_position(YseSynth* h, float position) {
  if (auto* p = va(h)) p->wavetablePosition.store(position, std::memory_order_relaxed);
}
YSE_C_API void yse_synth_va_set_cutoff(YseSynth* h, float hz) {
  if (auto* p = va(h)) p->cutoff.store(hz, std::memory_order_relaxed);
}
YSE_C_API void yse_synth_va_set_resonance(YseSynth* h, float resonance) {
  if (auto* p = va(h)) p->resonance.store(resonance, std::memory_order_relaxed);
}
YSE_C_API void yse_synth_va_set_key_tracking(YseSynth* h, float amount) {
  if (auto* p = va(h)) p->keyTracking.store(amount, std::memory_order_relaxed);
}
YSE_C_API void yse_synth_va_set_filter_env_amount(YseSynth* h, float octaves) {
  if (auto* p = va(h)) p->filterEnvAmount.store(octaves, std::memory_order_relaxed);
}
YSE_C_API void yse_synth_va_set_filter_vel_amount(YseSynth* h, float octaves) {
  if (auto* p = va(h)) p->filterVelAmount.store(octaves, std::memory_order_relaxed);
}
YSE_C_API void yse_synth_va_set_amp_attack(YseSynth* h, float seconds) {
  if (auto* p = va(h)) p->ampAttack.store(seconds, std::memory_order_relaxed);
}
YSE_C_API void yse_synth_va_set_amp_decay(YseSynth* h, float seconds) {
  if (auto* p = va(h)) p->ampDecay.store(seconds, std::memory_order_relaxed);
}
YSE_C_API void yse_synth_va_set_amp_sustain(YseSynth* h, float level) {
  if (auto* p = va(h)) p->ampSustain.store(level, std::memory_order_relaxed);
}
YSE_C_API void yse_synth_va_set_amp_release(YseSynth* h, float seconds) {
  if (auto* p = va(h)) p->ampRelease.store(seconds, std::memory_order_relaxed);
}
YSE_C_API void yse_synth_va_set_amp_vel_amount(YseSynth* h, float amount) {
  if (auto* p = va(h)) p->ampVelAmount.store(amount, std::memory_order_relaxed);
}
YSE_C_API void yse_synth_va_set_filter_attack(YseSynth* h, float seconds) {
  if (auto* p = va(h)) p->filterAttack.store(seconds, std::memory_order_relaxed);
}
YSE_C_API void yse_synth_va_set_filter_decay(YseSynth* h, float seconds) {
  if (auto* p = va(h)) p->filterDecay.store(seconds, std::memory_order_relaxed);
}
YSE_C_API void yse_synth_va_set_filter_sustain(YseSynth* h, float level) {
  if (auto* p = va(h)) p->filterSustain.store(level, std::memory_order_relaxed);
}
YSE_C_API void yse_synth_va_set_filter_release(YseSynth* h, float seconds) {
  if (auto* p = va(h)) p->filterRelease.store(seconds, std::memory_order_relaxed);
}
YSE_C_API void yse_synth_va_set_lfo_type(YseSynth* h, YseLfoType type) {
  if (auto* p = va(h)) p->lfoType.store(static_cast<int>(type), std::memory_order_relaxed);
}
YSE_C_API void yse_synth_va_set_lfo_rate(YseSynth* h, float hz) {
  if (auto* p = va(h)) p->lfoRate.store(hz, std::memory_order_relaxed);
}
YSE_C_API void yse_synth_va_set_lfo_to_pitch(YseSynth* h, float semitones) {
  if (auto* p = va(h)) p->lfoToPitch.store(semitones, std::memory_order_relaxed);
}
YSE_C_API void yse_synth_va_set_lfo_to_cutoff(YseSynth* h, float octaves) {
  if (auto* p = va(h)) p->lfoToCutoff.store(octaves, std::memory_order_relaxed);
}
YSE_C_API void yse_synth_va_set_lfo_to_wavetable(YseSynth* h, float amount) {
  if (auto* p = va(h)) p->lfoToWavetable.store(amount, std::memory_order_relaxed);
}
YSE_C_API void yse_synth_va_set_gain(YseSynth* h, float gain) {
  if (auto* p = va(h)) p->gain.store(gain, std::memory_order_relaxed);
}

YSE_C_API void yse_synth_va_load_wavetable(YseSynth* h, int slot, const float* cycle,
                                           size_t length) {
  auto* p = va(h);
  if (!p || !cycle || length == 0 || slot < 0) return;
  // Setup-thread reshape of the morph bank; copy into a vector for the engine
  // API. Off the audio thread by contract (documented in the header).
  std::vector<Flt> cyc(cycle, cycle + length);
  p->loadWavetable(slot, cyc);
}

// ─── FM patch (issue #178) ─────────────────────────────────────────────

namespace {
  inline YSE::SYNTH::fmPatch* fm(YseSynth* h) {
    return h ? to_impl(h)->fmPatch.get() : nullptr;
  }
  // Clamp a C int into a DX7 byte field's valid range before storing, so a
  // stray C value can never overflow the uint8_t or index a routing table out
  // of bounds (the header documents the ranges; the engine also clamps).
  inline U8 clampByte(int v, int lo, int hi) {
    return static_cast<U8>(v < lo ? lo : (v > hi ? hi : v));
  }
  inline bool fmOpInRange(int op) {
    return op >= 0 && op < 6;
  }
} // namespace

YSE_C_API YseStatus yse_synth_fm_set_patch(YseSynth* h, YseDx7Bank* bank, int index) {
  if (!h) return YSE_ERR_INVALID_HANDLE;
  auto* p = fm(h);
  if (!p) return YSE_ERR_INVALID_HANDLE; // no FM group established
  const YSE::SYNTH::dx7Bank* b = yse_c::dx7_bank_from_handle(bank);
  if (!b) return YSE_ERR_INVALID_ARGUMENT; // NULL / destroyed bank (logged)
  if (index < 0 || static_cast<size_t>(index) >= b->voices.size()) {
    yse_c::set_last_error("yse_synth_fm_set_patch: patch index out of range");
    return YSE_ERR_INVALID_ARGUMENT;
  }
  *p = b->voices[static_cast<size_t>(index)]; // plain-data copy; next note-on bakes it
  return YSE_OK;
}

YSE_C_API void yse_synth_fm_set_algorithm(YseSynth* h, int algorithm) {
  if (auto* p = fm(h)) p->algorithm = clampByte(algorithm, 0, 31);
}
YSE_C_API void yse_synth_fm_set_feedback(YseSynth* h, int feedback) {
  if (auto* p = fm(h)) p->feedback = clampByte(feedback, 0, 7);
}
YSE_C_API void yse_synth_fm_set_transpose(YseSynth* h, int transpose) {
  if (auto* p = fm(h)) p->transpose = clampByte(transpose, 0, 48);
}
YSE_C_API void yse_synth_fm_set_lfo_speed(YseSynth* h, int speed) {
  if (auto* p = fm(h)) p->lfoSpeed = clampByte(speed, 0, 99);
}
YSE_C_API void yse_synth_fm_set_lfo_delay(YseSynth* h, int delay) {
  if (auto* p = fm(h)) p->lfoDelay = clampByte(delay, 0, 99);
}
YSE_C_API void yse_synth_fm_set_lfo_waveform(YseSynth* h, int waveform) {
  if (auto* p = fm(h)) p->lfoWaveform = clampByte(waveform, 0, 5);
}
YSE_C_API void yse_synth_fm_set_lfo_pitch_mod_depth(YseSynth* h, int depth) {
  if (auto* p = fm(h)) p->lfoPitchModDepth = clampByte(depth, 0, 99);
}
YSE_C_API void yse_synth_fm_set_lfo_amp_mod_depth(YseSynth* h, int depth) {
  if (auto* p = fm(h)) p->lfoAmpModDepth = clampByte(depth, 0, 99);
}
YSE_C_API void yse_synth_fm_set_pitch_mod_sens(YseSynth* h, int sensitivity) {
  if (auto* p = fm(h)) p->pitchModSens = clampByte(sensitivity, 0, 7);
}
YSE_C_API void yse_synth_fm_set_op_output_level(YseSynth* h, int op, int level) {
  auto* p = fm(h);
  if (p && fmOpInRange(op)) p->op[op].outputLevel = clampByte(level, 0, 99);
}
YSE_C_API void yse_synth_fm_set_op_freq_coarse(YseSynth* h, int op, int coarse) {
  auto* p = fm(h);
  if (p && fmOpInRange(op)) p->op[op].freqCoarse = clampByte(coarse, 0, 31);
}
YSE_C_API void yse_synth_fm_set_op_freq_fine(YseSynth* h, int op, int fine) {
  auto* p = fm(h);
  if (p && fmOpInRange(op)) p->op[op].freqFine = clampByte(fine, 0, 99);
}
YSE_C_API void yse_synth_fm_set_op_detune(YseSynth* h, int op, int detune) {
  auto* p = fm(h);
  if (p && fmOpInRange(op)) p->op[op].detune = clampByte(detune, 0, 14);
}
YSE_C_API void yse_synth_fm_set_op_osc_mode(YseSynth* h, int op, int mode) {
  auto* p = fm(h);
  if (p && fmOpInRange(op)) p->op[op].oscMode = clampByte(mode, 0, 1);
}
YSE_C_API void yse_synth_fm_set_op_enabled(YseSynth* h, int op, int enabled) {
  auto* p = fm(h);
  if (p && fmOpInRange(op)) {
    U8 bit = static_cast<U8>(1u << op);
    if (enabled)
      p->opEnabled = static_cast<U8>(p->opEnabled | bit);
    else
      p->opEnabled = static_cast<U8>(p->opEnabled & ~bit);
  }
}

YSE_C_API int yse_synth_get_num_voices(YseSynth* h) {
  return h ? to_impl(h)->synth.getNumVoices() : 0;
}

// ─── notes and control ─────────────────────────────────────────────────

YSE_C_API void yse_synth_note_on(YseSynth* h, int channel, int note_number, float velocity) {
  if (h) to_impl(h)->synth.noteOn(channel, note_number, velocity);
}
YSE_C_API void yse_synth_note_off(YseSynth* h, int channel, int note_number, float velocity) {
  if (h) to_impl(h)->synth.noteOff(channel, note_number, velocity);
}
YSE_C_API void yse_synth_all_notes_off(YseSynth* h, int channel) {
  if (h) to_impl(h)->synth.allNotesOff(channel);
}
YSE_C_API void yse_synth_pitch_wheel(YseSynth* h, int channel, float value) {
  if (h) to_impl(h)->synth.pitchWheel(channel, value);
}
YSE_C_API void yse_synth_controller(YseSynth* h, int channel, int number, float value) {
  if (h) to_impl(h)->synth.controller(channel, number, value);
}
YSE_C_API void yse_synth_aftertouch(YseSynth* h, int channel, int note_number, float value) {
  if (h) to_impl(h)->synth.aftertouch(channel, note_number, value);
}
YSE_C_API void yse_synth_sustain(YseSynth* h, int channel, int down) {
  if (h) to_impl(h)->synth.sustain(channel, down != 0);
}
YSE_C_API void yse_synth_sostenuto(YseSynth* h, int channel, int down) {
  if (h) to_impl(h)->synth.sostenuto(channel, down != 0);
}
YSE_C_API void yse_synth_soft_pedal(YseSynth* h, int channel, int down) {
  if (h) to_impl(h)->synth.softPedal(channel, down != 0);
}

YSE_C_API void yse_synth_set_note_callback(YseSynth* h, YseSynthNoteCallback cb) {
  if (!h) return;
  // The engine's onNoteEvent hook is a bare captureless function pointer with
  // no user_data slot, so a stateful atomic-swap bridge is impossible (a
  // stateless bridge could not recover which synth called it). Pass the
  // pointer straight through; YSE::synth stores it atomically (release-store,
  // audio-thread acquire-load — see synthImplementation::setNoteCallback), so
  // the atomic-swap discipline lives on the engine side and the C layer adds
  // no lock and no allocation. NULL clears the hook.
  //
  // The C typedef takes `int note_on` per the C-API bool-as-int convention;
  // the engine invokes with a C++ `bool` (always 0/1). The signatures are
  // otherwise identical, so reinterpret_cast the pointer (same precedent as
  // yse_midi.cpp's parsed-callback bridge).
  using EngineCb = void (*)(bool, float*, float*);
  to_impl(h)->synth.onNoteEvent(reinterpret_cast<EngineCb>(cb));
}

// ─── attachment ────────────────────────────────────────────────────────

YSE_C_API YseStatus yse_synth_attach_to_sound(YseSynth* h, YseSound* sound, YseChannel* channel,
                                              float volume) {
  if (!h) return YSE_ERR_INVALID_HANDLE;
  if (!sound) return YSE_ERR_INVALID_ARGUMENT;
  try {
    to_cpp(sound)->create(to_impl(h)->synth, to_cpp(channel), volume);
    // create() leaves the sound invalid if it could not attach the synth's
    // aggregate source; surface that as an error for C consumers.
    if (!to_cpp(sound)->isValid()) {
      yse_c::set_last_error("sound not created from synth (attach failed)");
      return YSE_ERR_GENERIC;
    }
    return YSE_OK;
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return YSE_ERR_EXCEPTION;
  } catch (...) {
    yse_c::set_last_error("yse_synth_attach_to_sound: unknown C++ exception");
    return YSE_ERR_EXCEPTION;
  }
}

// ─── per-note 3D positioning (issue #171) ──────────────────────────────

YSE_C_API YseStatus yse_synth_set_position_handler(YseSynth* h, YseSynthPositionHandler kind,
                                                   const YseSynthPositionParams* params) {
  if (!h) return YSE_ERR_INVALID_HANDLE;
  // Drift tripwire: if a built-in handler kind is added to yse_enums.h, this
  // dispatch (and the count) must grow to match. See yse_enums_check.cpp.
  static_assert(YSE_POSITION_HANDLER_COUNT == 3,
                "add a case below when a built-in position-handler kind is added");
  try {
    auto* impl = to_impl(h);
    std::unique_ptr<YSE::SYNTH::positionHandler> proto;
    switch (kind) {
    case YSE_POSITION_HANDLER_STATIC: {
      auto p = std::make_unique<YSE::SYNTH::staticHandler>();
      if (params) p->position(YSE::Pos(params->static_x, params->static_y, params->static_z));
      proto = std::move(p);
      break;
    }
    case YSE_POSITION_HANDLER_RANDOM_SPREAD: {
      auto p = std::make_unique<YSE::SYNTH::randomSpreadHandler>();
      if (params) p->radius(params->spread_radius).seed(params->spread_seed);
      proto = std::move(p);
      break;
    }
    case YSE_POSITION_HANDLER_ORBIT: {
      auto p = std::make_unique<YSE::SYNTH::orbitHandler>();
      if (params)
        p->radius(params->orbit_radius)
            .velocityRadius(params->orbit_velocity_radius)
            .aftertouchWiden(params->orbit_aftertouch_widen)
            .rate(params->orbit_rate)
            .height(params->orbit_height)
            .releaseSlow(params->orbit_release_slow);
      proto = std::move(p);
      break;
    }
    case YSE_POSITION_HANDLER_COUNT:
    default:
      yse_c::set_last_error("yse_synth_set_position_handler: unknown handler kind");
      return YSE_ERR_INVALID_ARGUMENT;
    }
    // Keep the prototype alive past the setup pool's clone() (the engine records
    // a raw pointer and clones LATER). The handle owns it; swapping in a new
    // prototype frees any earlier one — safe before setup (the engine has not
    // read it yet) and harmless after (the engine's per-slot clones are already
    // independent and the swap is engine-rejected). The pointed-to object never
    // moves (only the unique_ptr does), so the recorded pointer stays valid.
    YSE::SYNTH::positionHandler* raw = proto.get();
    impl->positionHandler = std::move(proto);
    impl->synth.positionHandler(*raw);
    return YSE_OK;
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return YSE_ERR_EXCEPTION;
  } catch (...) {
    yse_c::set_last_error("yse_synth_set_position_handler: unknown C++ exception");
    return YSE_ERR_EXCEPTION;
  }
}

YSE_C_API void yse_synth_handler_param(YseSynth* h, int index, float value) {
  if (h) to_impl(h)->synth.handlerParam(index, value);
}

YSE_C_API void yse_synth_note_position(YseSynth* h, int channel, int note_number, float x, float y,
                                       float z) {
  if (h) to_impl(h)->synth.notePosition(channel, note_number, YSE::Pos(x, y, z));
}

YSE_C_API void yse_synth_get_voice_position(YseSynth* h, int channel, int note_number, float* x,
                                            float* y, float* z) {
  YSE::Pos p = h ? to_impl(h)->synth.getVoicePosition(channel, note_number) : YSE::Pos(0.f);
  if (x) *x = p.x;
  if (y) *y = p.y;
  if (z) *z = p.z;
}

} // extern "C"
