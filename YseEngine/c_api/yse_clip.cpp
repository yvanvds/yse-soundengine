#include "yse_c/yse_clip.h"
#include "yse_c_internal.hpp"

#include "../clip/clip.hpp"
#if YSE_ENABLE_MIDI_DEVICE
#include "../midi/device.hpp"
#endif

#include <exception>
#include <string>
#include <vector>

namespace {
  inline YSE::clip* to_cpp(YseClip* c) {
    return reinterpret_cast<YSE::clip*>(c);
  }
#if YSE_ENABLE_MIDI_DEVICE
  // Same handle representation as yse_midi.cpp: a YseMidiOut* is a
  // reinterpret_cast YSE::midiOut*.
  inline YSE::midiOut* to_cpp(YseMidiOut* m) {
    return reinterpret_cast<YSE::midiOut*>(m);
  }
#endif
} // namespace

extern "C" {

YSE_C_API YseClip* yse_clip_create(void) {
  try {
    return reinterpret_cast<YseClip*>(new YSE::clip());
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return nullptr;
  } catch (...) {
    yse_c::set_last_error("yse_clip_create: unknown C++ exception");
    return nullptr;
  }
}

YSE_C_API void yse_clip_destroy(YseClip* c) {
  if (c) delete to_cpp(c);
}

YSE_C_API YseStatus yse_clip_bind(YseClip* c, const char* clock_name) {
  if (!c) {
    yse_c::set_last_error("yse_clip_bind: clip handle is NULL");
    return YSE_ERR_INVALID_HANDLE;
  }
  if (!clock_name) {
    yse_c::set_last_error("yse_clip_bind: clock_name is NULL");
    return YSE_ERR_INVALID_ARGUMENT;
  }
  return yse_c::guard("yse_clip_bind", YSE_ERR_EXCEPTION, [&] {
    if (!to_cpp(c)->create(clock_name)) {
      yse_c::set_last_error(std::string("yse_clip_bind: no live clock named \"") + clock_name +
                            "\"; the clip is now unbound");
      return YSE_ERR_INVALID_ARGUMENT;
    }
    return YSE_OK;
  });
}

YSE_C_API void yse_clip_set_events(YseClip* c, const YseClipEvent* events, size_t count) {
  if (!c) return;
  if (!events && count != 0) return;
  try {
    std::vector<YSE::clipEvent> list;
    list.reserve(count);
    for (size_t i = 0; i < count; ++i) {
      YSE::clipEvent e;
      e.startBeat = events[i].start_beat;
      e.durationBeats = events[i].duration_beats;
      e.channel = events[i].channel;
      e.pitch = events[i].pitch;
      e.velocity = events[i].velocity;
      e.pitchBend = events[i].pitch_bend;
      list.push_back(e);
    }
    to_cpp(c)->setEvents(list);
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
  } catch (...) {
    yse_c::set_last_error("yse_clip_set_events: unknown C++ exception");
  }
}

YSE_C_API void yse_clip_set_loop_length(YseClip* c, double beats) {
  if (c) to_cpp(c)->loopLength(beats);
}

YSE_C_API void yse_clip_connect_synth(YseClip* c, YseSynth* synth) {
  if (!c) return;
  YSE::synth* s = yse_c::synth_from_handle(synth);
  if (s) to_cpp(c)->connect(*s);
}

YSE_C_API void yse_clip_disconnect_synth(YseClip* c, YseSynth* synth) {
  if (!c) return;
  YSE::synth* s = yse_c::synth_from_handle(synth);
  if (s) to_cpp(c)->disconnect(*s);
}

YSE_C_API void yse_clip_connect_midi_out(YseClip* c, YseMidiOut* m) {
#if YSE_ENABLE_MIDI_DEVICE
  if (!c || !m) return;
  to_cpp(c)->connect(*to_cpp(m));
#else
  (void)c;
  (void)m;
  yse_c::set_last_error("clip MIDI-out is Windows/Linux only");
#endif
}

YSE_C_API void yse_clip_disconnect_midi_out(YseClip* c, YseMidiOut* m) {
#if YSE_ENABLE_MIDI_DEVICE
  if (!c || !m) return;
  to_cpp(c)->disconnect(*to_cpp(m));
#else
  (void)c;
  (void)m;
  yse_c::set_last_error("clip MIDI-out is Windows/Linux only");
#endif
}

YSE_C_API void yse_clip_play(YseClip* c) {
  if (c) to_cpp(c)->play();
}

YSE_C_API void yse_clip_stop(YseClip* c) {
  if (c) to_cpp(c)->stop();
}

YSE_C_API int yse_clip_is_playing(YseClip* c) {
  return (c && to_cpp(c)->isPlaying()) ? 1 : 0;
}

} // extern "C"
