#include "yse_c/yse_midi.h"
#include "yse_c_internal.hpp"

#include "../midi/midifile.hpp"
#include "../midi/midiNote.hpp"
#include "../headers/enums.hpp"

#if YSE_ENABLE_MIDI_DEVICE
#include "../midi/device.hpp"
#define YSE_C_HAVE_MIDI_OUT 1
#else
#define YSE_C_HAVE_MIDI_OUT 0
#endif

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>

namespace {
  inline YSE::MIDI::file* to_cpp(YseMidiFile* f) {
    return reinterpret_cast<YSE::MIDI::file*>(f);
  }
  inline YSE::MIDI::midiNote* to_cpp(YseMidiNote* n) {
    return reinterpret_cast<YSE::MIDI::midiNote*>(n);
  }
#if YSE_C_HAVE_MIDI_OUT
  inline YSE::midiOut* to_cpp(YseMidiOut* m) {
    return reinterpret_cast<YSE::midiOut*>(m);
  }
  inline YSE::MIDI::M_CHANNEL ch(int channel) {
    if (channel < 0) channel = 0;
    if (channel > 15) channel = 15;
    return static_cast<YSE::MIDI::M_CHANNEL>(channel);
  }

  // The C API needs to convert the C++ midiIn's zero-copy byte pointer into
  // a malloc'd buffer that the receiver owns (mirrors the log-callback
  // ownership contract). A wrapper struct carries that bridge state per
  // YseMidiIn handle.
  struct YseMidiInImpl {
    YSE::midiIn cpp;
    std::atomic<YseMidiInRawCallback> rawCb{nullptr};
    std::atomic<void*> rawUser{nullptr};
  };

  inline YseMidiInImpl* to_impl(YseMidiIn* m) {
    return reinterpret_cast<YseMidiInImpl*>(m);
  }

  // Bridge registered with YSE::midiIn::setRawCallback. user_data is the
  // YseMidiInImpl pointer; the user-facing callback + user_data live on the
  // impl as atomics so they can be swapped from the host thread while the
  // RtMidi input thread is dispatching.
  void c_raw_bridge(double ts, const unsigned char* bytes, std::size_t len, void* userData) {
    auto* impl = static_cast<YseMidiInImpl*>(userData);
    if (!impl || len == 0) return;
    auto cb = impl->rawCb.load(std::memory_order_acquire);
    if (!cb) return;
    auto user = impl->rawUser.load(std::memory_order_acquire);
    // Strings/buffers passed across NativeCallable.listener bridges to Dart
    // are marshalled by pointer value, not deep copy — by the time the Dart
    // handler runs, the std::vector backing the RtMidi-side pointer has been
    // re-used. malloc a copy and hand ownership to the receiver.
    auto* heap = static_cast<unsigned char*>(std::malloc(len));
    if (!heap) return;
    std::memcpy(heap, bytes, len);
    cb(ts, heap, len, user);
  }
#endif
} // namespace

extern "C" {

// ─── midi file ─────────────────────────────────────────────────────

YSE_C_API YseMidiFile* yse_midi_file_create(void) {
  try {
    return reinterpret_cast<YseMidiFile*>(new YSE::MIDI::file());
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return nullptr;
  } catch (...) {
    yse_c::set_last_error("midi_file_create: unknown C++ exception");
    return nullptr;
  }
}
YSE_C_API void yse_midi_file_destroy(YseMidiFile* f) {
  if (f) delete to_cpp(f);
}

YSE_C_API YseStatus yse_midi_file_load(YseMidiFile* f, const char* filename) {
  if (!f) return YSE_ERR_INVALID_HANDLE;
  if (!filename) return YSE_ERR_INVALID_ARGUMENT;
  try {
    if (!to_cpp(f)->create(filename)) {
      yse_c::set_last_error(std::string("midi file load failed for: ") + filename);
      return YSE_ERR_FILE_NOT_FOUND;
    }
    return YSE_OK;
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return YSE_ERR_EXCEPTION;
  } catch (...) {
    yse_c::set_last_error("midi_file_load: unknown C++ exception");
    return YSE_ERR_EXCEPTION;
  }
}

YSE_C_API void yse_midi_file_play(YseMidiFile* f) {
  if (f) to_cpp(f)->play();
}
YSE_C_API void yse_midi_file_pause(YseMidiFile* f) {
  if (f) to_cpp(f)->pause();
}
YSE_C_API void yse_midi_file_stop(YseMidiFile* f) {
  if (f) to_cpp(f)->stop();
}

YSE_C_API void yse_midi_file_connect_synth(YseMidiFile* f, YseSynth* synth) {
  if (!f) return;
  // synth_from_handle (defined in yse_synth.cpp) yields the engine synth backing
  // the opaque YseSynth; a NULL / never-created handle yields nullptr. connect()
  // only records the pointer in the file's fixed-size, lock-free synth table, so
  // no loaded file or active playback is required here. Mirrors the always-on
  // midifile surface (unlike midi device I/O, this is not RtMidi-gated).
  YSE::synth* s = yse_c::synth_from_handle(synth);
  if (s) to_cpp(f)->connect(*s);
}

YSE_C_API void yse_midi_file_disconnect_synth(YseMidiFile* f, YseSynth* synth) {
  if (!f) return;
  YSE::synth* s = yse_c::synth_from_handle(synth);
  if (s) to_cpp(f)->disconnect(*s);
}

// ─── midi out (Windows/Linux only) ─────────────────────────────────

#if YSE_C_HAVE_MIDI_OUT

YSE_C_API YseMidiOut* yse_midi_out_create(void) {
  try {
    return reinterpret_cast<YseMidiOut*>(new YSE::midiOut());
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return nullptr;
  } catch (...) {
    yse_c::set_last_error("midi_out_create: unknown C++ exception");
    return nullptr;
  }
}
YSE_C_API void yse_midi_out_destroy(YseMidiOut* m) {
  if (m) delete to_cpp(m);
}

YSE_C_API void yse_midi_out_open(YseMidiOut* m, unsigned int port) {
  if (m) to_cpp(m)->create(port);
}

YSE_C_API void yse_midi_out_note_on(YseMidiOut* m, int channel, int pitch, int velocity) {
  if (m)
    to_cpp(m)->NoteOn(ch(channel), static_cast<unsigned char>(pitch),
                      static_cast<unsigned char>(velocity));
}
YSE_C_API void yse_midi_out_note_off(YseMidiOut* m, int channel, int pitch, int velocity) {
  if (m)
    to_cpp(m)->NoteOff(ch(channel), static_cast<unsigned char>(pitch),
                       static_cast<unsigned char>(velocity));
}
YSE_C_API void yse_midi_out_poly_pressure(YseMidiOut* m, int channel, int pitch, int value) {
  if (m)
    to_cpp(m)->PolyPressure(ch(channel), static_cast<unsigned char>(pitch),
                            static_cast<unsigned char>(value));
}
YSE_C_API void yse_midi_out_channel_pressure(YseMidiOut* m, int channel, int value) {
  if (m) to_cpp(m)->ChannelPressure(ch(channel), static_cast<unsigned char>(value));
}
YSE_C_API void yse_midi_out_program_change(YseMidiOut* m, int channel, int value) {
  if (m) to_cpp(m)->ProgramChange(ch(channel), static_cast<unsigned char>(value));
}
YSE_C_API void yse_midi_out_control_change(YseMidiOut* m, int channel, int controller, int value) {
  if (m)
    to_cpp(m)->ControlChange(ch(channel), static_cast<unsigned char>(controller),
                             static_cast<unsigned char>(value));
}
YSE_C_API void yse_midi_out_all_notes_off_channel(YseMidiOut* m, int channel) {
  if (m) to_cpp(m)->AllNotesOff(ch(channel));
}
YSE_C_API void yse_midi_out_all_notes_off(YseMidiOut* m) {
  if (m) to_cpp(m)->AllNotesOff();
}
YSE_C_API void yse_midi_out_reset_channel(YseMidiOut* m, int channel) {
  if (m) to_cpp(m)->Reset(ch(channel));
}
YSE_C_API void yse_midi_out_reset(YseMidiOut* m) {
  if (m) to_cpp(m)->Reset();
}

YSE_C_API void yse_midi_out_local_control(YseMidiOut* m, int on) {
  if (m) to_cpp(m)->LocalControl(on != 0);
}
YSE_C_API void yse_midi_out_omni(YseMidiOut* m, int on) {
  if (m) to_cpp(m)->Omni(on != 0);
}
YSE_C_API void yse_midi_out_poly(YseMidiOut* m, int on) {
  if (m) to_cpp(m)->Poly(on != 0);
}

YSE_C_API void yse_midi_out_raw3(YseMidiOut* m, unsigned char a, unsigned char b, unsigned char c) {
  if (m) to_cpp(m)->Raw(a, b, c);
}

// ─── midi input ─────────────────────────────────────────────────────

YSE_C_API YseMidiIn* yse_midi_in_create(void) {
  try {
    return reinterpret_cast<YseMidiIn*>(new YseMidiInImpl());
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return nullptr;
  } catch (...) {
    yse_c::set_last_error("midi_in_create: unknown C++ exception");
    return nullptr;
  }
}

YSE_C_API void yse_midi_in_destroy(YseMidiIn* m) {
  if (m) delete to_impl(m);
}

YSE_C_API void yse_midi_in_open(YseMidiIn* m, unsigned int port) {
  if (m) to_impl(m)->cpp.create(port);
}

YSE_C_API void yse_midi_in_close(YseMidiIn* m) {
  if (m) to_impl(m)->cpp.close();
}

YSE_C_API int yse_midi_in_is_open(YseMidiIn* m) {
  return (m && to_impl(m)->cpp.isOpen()) ? 1 : 0;
}

YSE_C_API void yse_midi_in_set_raw_callback(YseMidiIn* m, YseMidiInRawCallback cb,
                                            void* user_data) {
  if (!m) return;
  auto* impl = to_impl(m);
  // Publish the user-facing callback first so the bridge can never observe
  // a half-installed state — release on both stores, acquire on read in
  // c_raw_bridge.
  impl->rawUser.store(user_data, std::memory_order_release);
  impl->rawCb.store(cb, std::memory_order_release);
  // Wire (or detach) the C++ side. The bridge carries impl* through the
  // user_data slot so multiple YseMidiIn instances stay independent.
  impl->cpp.setRawCallback(cb ? &c_raw_bridge : nullptr, impl);
}

YSE_C_API void yse_midi_in_set_parsed_callback(YseMidiIn* m, YseMidiInParsedCallback cb,
                                               void* user_data) {
  if (!m) return;
  // The parsed callback signature is layout-compatible between the C ABI
  // typedef and the C++ class typedef (same scalar args, same calling
  // convention), so pass straight through.
  to_impl(m)->cpp.setParsedCallback(reinterpret_cast<YSE::midiIn::ParsedCallback>(cb), user_data);
}

YSE_C_API void yse_midi_in_free_message(unsigned char* bytes) {
  if (bytes) std::free(bytes);
}

YSE_C_API void yse_midi_in_connect_synth(YseMidiIn* m, YseSynth* synth, int channel_filter) {
  if (!m) return;
  // synth_from_handle (defined in yse_synth.cpp) yields the engine synth backing
  // the opaque YseSynth; a NULL / never-created handle yields nullptr. connect()
  // only records the pointer in the port's lock-free subscriber table, so no
  // engine device or open port is required here.
  YSE::synth* s = yse_c::synth_from_handle(synth);
  if (s) to_impl(m)->cpp.connect(*s, channel_filter);
}

YSE_C_API void yse_midi_in_disconnect_synth(YseMidiIn* m, YseSynth* synth) {
  if (!m) return;
  YSE::synth* s = yse_c::synth_from_handle(synth);
  if (s) to_impl(m)->cpp.disconnect(*s);
}

#else
// Stub the midiOut surface on platforms without RtMidi so the C ABI is
// uniform across builds. Each call sets last_error and returns / no-ops.
YSE_C_API YseMidiOut* yse_midi_out_create(void) {
  yse_c::set_last_error("midiOut is Windows/Linux only");
  return nullptr;
}
YSE_C_API void yse_midi_out_destroy(YseMidiOut*) {}
YSE_C_API void yse_midi_out_open(YseMidiOut*, unsigned int) {}
YSE_C_API void yse_midi_out_note_on(YseMidiOut*, int, int, int) {}
YSE_C_API void yse_midi_out_note_off(YseMidiOut*, int, int, int) {}
YSE_C_API void yse_midi_out_poly_pressure(YseMidiOut*, int, int, int) {}
YSE_C_API void yse_midi_out_channel_pressure(YseMidiOut*, int, int) {}
YSE_C_API void yse_midi_out_program_change(YseMidiOut*, int, int) {}
YSE_C_API void yse_midi_out_control_change(YseMidiOut*, int, int, int) {}
YSE_C_API void yse_midi_out_all_notes_off_channel(YseMidiOut*, int) {}
YSE_C_API void yse_midi_out_all_notes_off(YseMidiOut*) {}
YSE_C_API void yse_midi_out_reset_channel(YseMidiOut*, int) {}
YSE_C_API void yse_midi_out_reset(YseMidiOut*) {}
YSE_C_API void yse_midi_out_local_control(YseMidiOut*, int) {}
YSE_C_API void yse_midi_out_omni(YseMidiOut*, int) {}
YSE_C_API void yse_midi_out_poly(YseMidiOut*, int) {}
YSE_C_API void yse_midi_out_raw3(YseMidiOut*, unsigned char, unsigned char, unsigned char) {}

// midi input stubs — same RtMidi gate as the output side.
YSE_C_API YseMidiIn* yse_midi_in_create(void) {
  yse_c::set_last_error("midiIn is Windows/Linux only");
  return nullptr;
}
YSE_C_API void yse_midi_in_destroy(YseMidiIn*) {}
YSE_C_API void yse_midi_in_open(YseMidiIn*, unsigned int) {}
YSE_C_API void yse_midi_in_close(YseMidiIn*) {}
YSE_C_API int yse_midi_in_is_open(YseMidiIn*) {
  return 0;
}
YSE_C_API void yse_midi_in_set_raw_callback(YseMidiIn*, YseMidiInRawCallback, void*) {}
YSE_C_API void yse_midi_in_set_parsed_callback(YseMidiIn*, YseMidiInParsedCallback, void*) {}
YSE_C_API void yse_midi_in_free_message(unsigned char* bytes) {
  if (bytes) std::free(bytes);
}
YSE_C_API void yse_midi_in_connect_synth(YseMidiIn*, YseSynth*, int) {}
YSE_C_API void yse_midi_in_disconnect_synth(YseMidiIn*, YseSynth*) {}
#endif

// ─── midiNote ──────────────────────────────────────────────────────

YSE_C_API YseMidiNote* yse_midi_note_create(unsigned char note, unsigned int velocity) {
  try {
    return reinterpret_cast<YseMidiNote*>(new YSE::MIDI::midiNote(note, velocity));
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return nullptr;
  } catch (...) {
    yse_c::set_last_error("midi_note_create: unknown C++ exception");
    return nullptr;
  }
}
YSE_C_API void yse_midi_note_destroy(YseMidiNote* n) {
  if (n) delete to_cpp(n);
}

YSE_C_API void yse_midi_note_set_note(YseMidiNote* n, unsigned char v) {
  if (n) to_cpp(n)->note(v);
}
YSE_C_API unsigned char yse_midi_note_get_note(YseMidiNote* n) {
  return n ? to_cpp(n)->note() : 0;
}
YSE_C_API void yse_midi_note_set_velocity(YseMidiNote* n, unsigned char v) {
  if (n) to_cpp(n)->velocity(v);
}
YSE_C_API unsigned char yse_midi_note_get_velocity(YseMidiNote* n) {
  return n ? to_cpp(n)->velocity() : 0;
}

} // extern "C"
