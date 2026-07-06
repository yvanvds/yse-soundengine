#include "yse_c/yse_synth.h"
#include "yse_c_internal.hpp"

#include "../synth/synthInterface.hpp"
#include "../synth/sineVoice.hpp"
#include "../synth/dspVoice.hpp"
#include "../sound/soundInterface.hpp"
#include "../channel/channelInterface.hpp"

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

} // extern "C"
