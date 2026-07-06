#include "yse_c/yse_music.h"
#include "yse_c/yse_synth.h" // YseSynth handle — the synth a player is bound to (#268)
#include "yse_c_internal.hpp"

#include "../music/note.hpp"
#include "../music/pNote.hpp"
#include "../music/scale/scaleInterface.hpp"
#include "../music/motif/motifInterface.hpp"
#include "../player/playerInterface.hpp"

#include <exception>

namespace {
  inline YSE::MUSIC::note* to_cpp(YseNote* n) {
    return reinterpret_cast<YSE::MUSIC::note*>(n);
  }
  inline YSE::MUSIC::pNote* to_cpp(YsePNote* n) {
    return reinterpret_cast<YSE::MUSIC::pNote*>(n);
  }
  inline YSE::scale* to_cpp(YseScale* s) {
    return reinterpret_cast<YSE::scale*>(s);
  }
  inline YSE::motif* to_cpp(YseMotif* m) {
    return reinterpret_cast<YSE::motif*>(m);
  }
  inline YSE::player* to_cpp(YsePlayer* p) {
    return reinterpret_cast<YSE::player*>(p);
  }
} // namespace

extern "C" {

// ─── note ──────────────────────────────────────────────────────────

YSE_C_API YseNote* yse_note_create(float pitch, float volume, float length, int channel) {
  try {
    return reinterpret_cast<YseNote*>(new YSE::MUSIC::note(pitch, volume, length, channel));
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return nullptr;
  } catch (...) {
    yse_c::set_last_error("note_create: unknown C++ exception");
    return nullptr;
  }
}
YSE_C_API void yse_note_destroy(YseNote* n) {
  if (n) delete to_cpp(n);
}

YSE_C_API void yse_note_set(YseNote* n, float pitch, float volume, float length, int channel) {
  if (n) to_cpp(n)->set(pitch, volume, length, channel);
}
YSE_C_API void yse_note_set_pitch(YseNote* n, float v) {
  if (n) to_cpp(n)->setPitch(v);
}
YSE_C_API void yse_note_set_volume(YseNote* n, float v) {
  if (n) to_cpp(n)->setVolume(v);
}
YSE_C_API void yse_note_set_length(YseNote* n, float v) {
  if (n) to_cpp(n)->setLength(v);
}
YSE_C_API void yse_note_set_channel(YseNote* n, int v) {
  if (n) to_cpp(n)->setChannel(v);
}
YSE_C_API float yse_note_get_pitch(YseNote* n) {
  return n ? to_cpp(n)->getPitch() : 0.0f;
}
YSE_C_API float yse_note_get_volume(YseNote* n) {
  return n ? to_cpp(n)->getVolume() : 0.0f;
}
YSE_C_API float yse_note_get_length(YseNote* n) {
  return n ? to_cpp(n)->getLength() : 0.0f;
}
YSE_C_API int yse_note_get_channel(YseNote* n) {
  return n ? to_cpp(n)->getChannel() : 0;
}

// ─── pNote ─────────────────────────────────────────────────────────

YSE_C_API YsePNote* yse_pnote_create(float position, float pitch, float volume, float length,
                                     int channel) {
  try {
    return reinterpret_cast<YsePNote*>(
        new YSE::MUSIC::pNote(position, pitch, volume, length, channel));
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return nullptr;
  } catch (...) {
    yse_c::set_last_error("pnote_create: unknown C++ exception");
    return nullptr;
  }
}
YSE_C_API void yse_pnote_destroy(YsePNote* n) {
  if (n) delete to_cpp(n);
}
YSE_C_API void yse_pnote_set_position(YsePNote* n, float v) {
  if (n) to_cpp(n)->setPosition(v);
}
YSE_C_API float yse_pnote_get_position(YsePNote* n) {
  return n ? to_cpp(n)->getPosition() : 0.0f;
}
YSE_C_API void yse_pnote_set_pitch(YsePNote* n, float v) {
  if (n) to_cpp(n)->setPitch(v);
}
YSE_C_API void yse_pnote_set_volume(YsePNote* n, float v) {
  if (n) to_cpp(n)->setVolume(v);
}
YSE_C_API void yse_pnote_set_length(YsePNote* n, float v) {
  if (n) to_cpp(n)->setLength(v);
}
YSE_C_API float yse_pnote_get_pitch(YsePNote* n) {
  return n ? to_cpp(n)->getPitch() : 0.0f;
}
YSE_C_API float yse_pnote_get_volume(YsePNote* n) {
  return n ? to_cpp(n)->getVolume() : 0.0f;
}
YSE_C_API float yse_pnote_get_length(YsePNote* n) {
  return n ? to_cpp(n)->getLength() : 0.0f;
}

// ─── scale ─────────────────────────────────────────────────────────

YSE_C_API YseScale* yse_scale_create(void) {
  try {
    return reinterpret_cast<YseScale*>(new YSE::scale());
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return nullptr;
  } catch (...) {
    yse_c::set_last_error("scale_create: unknown C++ exception");
    return nullptr;
  }
}
YSE_C_API void yse_scale_destroy(YseScale* s) {
  if (s) delete to_cpp(s);
}
YSE_C_API void yse_scale_add(YseScale* s, float pitch, float step) {
  if (s) to_cpp(s)->add(pitch, step);
}
YSE_C_API void yse_scale_remove(YseScale* s, float pitch, float step) {
  if (s) to_cpp(s)->remove(pitch, step);
}
YSE_C_API int yse_scale_has(YseScale* s, float pitch) {
  return s && to_cpp(s)->has(pitch) ? 1 : 0;
}
YSE_C_API float yse_scale_nearest(YseScale* s, float pitch) {
  return s ? to_cpp(s)->getNearest(pitch) : 0.0f;
}
YSE_C_API unsigned int yse_scale_size(YseScale* s) {
  return s ? to_cpp(s)->size() : 0;
}
YSE_C_API void yse_scale_clear(YseScale* s) {
  if (s) to_cpp(s)->clear();
}

// ─── motif ─────────────────────────────────────────────────────────

YSE_C_API YseMotif* yse_motif_create(void) {
  try {
    return reinterpret_cast<YseMotif*>(new YSE::motif());
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return nullptr;
  } catch (...) {
    yse_c::set_last_error("motif_create: unknown C++ exception");
    return nullptr;
  }
}
YSE_C_API void yse_motif_destroy(YseMotif* m) {
  if (m) delete to_cpp(m);
}
YSE_C_API void yse_motif_add(YseMotif* m, YsePNote* note) {
  if (m && note) to_cpp(m)->add(*to_cpp(note));
}
YSE_C_API void yse_motif_clear(YseMotif* m) {
  if (m) to_cpp(m)->clear();
}
YSE_C_API void yse_motif_set_length(YseMotif* m, float v) {
  if (m) to_cpp(m)->setLength(v);
}
YSE_C_API void yse_motif_set_length_auto(YseMotif* m) {
  if (m) to_cpp(m)->setLength();
}
YSE_C_API void yse_motif_transpose(YseMotif* m, float v) {
  if (m) to_cpp(m)->transpose(v);
}
YSE_C_API void yse_motif_set_first_pitch(YseMotif* m, YseScale* s) {
  if (m && s) to_cpp(m)->setFirstPitch(*to_cpp(s));
}
YSE_C_API float yse_motif_get_length(YseMotif* m) {
  return m ? to_cpp(m)->getLength() : 0.0f;
}
YSE_C_API int yse_motif_empty(YseMotif* m) {
  return m && to_cpp(m)->empty() ? 1 : 0;
}
YSE_C_API unsigned int yse_motif_size(YseMotif* m) {
  return m ? to_cpp(m)->size() : 0;
}

// ─── player ────────────────────────────────────────────────────────

YSE_C_API YsePlayer* yse_player_create(YseSynth* synth) {
  YSE::synth* instrument = yse_c::synth_from_handle(synth);
  if (instrument == nullptr) {
    yse_c::set_last_error("player_create: synth handle is NULL");
    return nullptr;
  }
  try {
    // Bind the player to its synth up front (player::create) — without this the
    // pimpl is never assigned and every subsequent call is an inert no-op
    // (issue #268). create() registers the player with PLAYER::Manager().
    auto* p = new YSE::player();
    p->create(*instrument);
    return reinterpret_cast<YsePlayer*>(p);
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return nullptr;
  } catch (...) {
    yse_c::set_last_error("player_create: unknown C++ exception");
    return nullptr;
  }
}
YSE_C_API void yse_player_destroy(YsePlayer* p) {
  if (p) delete to_cpp(p);
}
YSE_C_API void yse_player_play(YsePlayer* p) {
  if (p) to_cpp(p)->play();
}
YSE_C_API void yse_player_stop(YsePlayer* p) {
  if (p) to_cpp(p)->stop();
}
YSE_C_API int yse_player_is_playing(YsePlayer* p) {
  return p && to_cpp(p)->isPlaying() ? 1 : 0;
}

YSE_C_API void yse_player_set_minimum_pitch(YsePlayer* p, float t, float ft) {
  if (p) to_cpp(p)->setMinimumPitch(t, ft);
}
YSE_C_API void yse_player_set_maximum_pitch(YsePlayer* p, float t, float ft) {
  if (p) to_cpp(p)->setMaximumPitch(t, ft);
}
YSE_C_API void yse_player_set_minimum_velocity(YsePlayer* p, float t, float ft) {
  if (p) to_cpp(p)->setMinimumVelocity(t, ft);
}
YSE_C_API void yse_player_set_maximum_velocity(YsePlayer* p, float t, float ft) {
  if (p) to_cpp(p)->setMaximumVelocity(t, ft);
}
YSE_C_API void yse_player_set_minimum_gap(YsePlayer* p, float t, float ft) {
  if (p) to_cpp(p)->setMinimumGap(t, ft);
}
YSE_C_API void yse_player_set_maximum_gap(YsePlayer* p, float t, float ft) {
  if (p) to_cpp(p)->setMaximumGap(t, ft);
}
YSE_C_API void yse_player_set_minimum_length(YsePlayer* p, float t, float ft) {
  if (p) to_cpp(p)->setMinimumLength(t, ft);
}
YSE_C_API void yse_player_set_maximum_length(YsePlayer* p, float t, float ft) {
  if (p) to_cpp(p)->setMaximumLength(t, ft);
}
YSE_C_API void yse_player_set_voices(YsePlayer* p, unsigned int t, float ft) {
  if (p) to_cpp(p)->setVoices(t, ft);
}

YSE_C_API void yse_player_set_scale(YsePlayer* p, YseScale* s, float ft) {
  if (p && s) to_cpp(p)->setScale(*to_cpp(s), ft);
}
YSE_C_API void yse_player_add_motif(YsePlayer* p, YseMotif* m, unsigned int w) {
  if (p && m) to_cpp(p)->addMotif(*to_cpp(m), w);
}
YSE_C_API void yse_player_remove_motif(YsePlayer* p, YseMotif* m) {
  if (p && m) to_cpp(p)->removeMotif(*to_cpp(m));
}
YSE_C_API void yse_player_adjust_motif_weight(YsePlayer* p, YseMotif* m, unsigned int w) {
  if (p && m) to_cpp(p)->adjustMotifWeight(*to_cpp(m), w);
}
YSE_C_API void yse_player_play_partial_motifs(YsePlayer* p, float t, float ft) {
  if (p) to_cpp(p)->playPartialMotifs(t, ft);
}
YSE_C_API void yse_player_play_motifs(YsePlayer* p, float t, float ft) {
  if (p) to_cpp(p)->playMotifs(t, ft);
}
YSE_C_API void yse_player_fit_motifs_to_scale(YsePlayer* p, float t, float ft) {
  if (p) to_cpp(p)->fitMotifsToScale(t, ft);
}

} // extern "C"
