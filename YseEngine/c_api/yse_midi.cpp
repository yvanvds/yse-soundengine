#include "yse_c/yse_midi.h"
#include "yse_c_internal.hpp"

#include "../midi/midifile.hpp"
#include "../midi/midiNote.hpp"
#include "../headers/enums.hpp"

#if defined(_WIN32) || defined(_WIN64) || defined(__linux__)
  #include "../midi/device.hpp"
  #define YSE_C_HAVE_MIDI_OUT 1
#else
  #define YSE_C_HAVE_MIDI_OUT 0
#endif

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
#endif
}

extern "C" {

// ─── midi file ─────────────────────────────────────────────────────

YSE_C_API YseMidiFile* yse_midi_file_create(void) {
  try { return reinterpret_cast<YseMidiFile*>(new YSE::MIDI::file()); }
  catch (const std::exception& e) { yse_c::set_last_error(e.what()); return nullptr; }
  catch (...) { yse_c::set_last_error("midi_file_create: unknown C++ exception"); return nullptr; }
}
YSE_C_API void yse_midi_file_destroy(YseMidiFile* f) { if (f) delete to_cpp(f); }

YSE_C_API YseStatus yse_midi_file_load(YseMidiFile* f, const char* filename) {
  if (!f) return YSE_ERR_INVALID_HANDLE;
  if (!filename) return YSE_ERR_INVALID_ARGUMENT;
  try {
    if (!to_cpp(f)->create(filename)) {
      yse_c::set_last_error(std::string("midi file load failed for: ") + filename);
      return YSE_ERR_FILE_NOT_FOUND;
    }
    return YSE_OK;
  } catch (const std::exception& e) { yse_c::set_last_error(e.what()); return YSE_ERR_EXCEPTION; }
    catch (...) { yse_c::set_last_error("midi_file_load: unknown C++ exception"); return YSE_ERR_EXCEPTION; }
}

YSE_C_API void yse_midi_file_play(YseMidiFile* f)  { if (f) to_cpp(f)->play(); }
YSE_C_API void yse_midi_file_pause(YseMidiFile* f) { if (f) to_cpp(f)->pause(); }
YSE_C_API void yse_midi_file_stop(YseMidiFile* f)  { if (f) to_cpp(f)->stop(); }

// ─── midi out (Windows/Linux only) ─────────────────────────────────

#if YSE_C_HAVE_MIDI_OUT

YSE_C_API YseMidiOut* yse_midi_out_create(void) {
  try { return reinterpret_cast<YseMidiOut*>(new YSE::midiOut()); }
  catch (const std::exception& e) { yse_c::set_last_error(e.what()); return nullptr; }
  catch (...) { yse_c::set_last_error("midi_out_create: unknown C++ exception"); return nullptr; }
}
YSE_C_API void yse_midi_out_destroy(YseMidiOut* m) { if (m) delete to_cpp(m); }

YSE_C_API void yse_midi_out_open(YseMidiOut* m, unsigned int port) {
  if (m) to_cpp(m)->create(port);
}

YSE_C_API void yse_midi_out_note_on(YseMidiOut* m, int channel, int pitch, int velocity) {
  if (m) to_cpp(m)->NoteOn(ch(channel), static_cast<unsigned char>(pitch), static_cast<unsigned char>(velocity));
}
YSE_C_API void yse_midi_out_note_off(YseMidiOut* m, int channel, int pitch, int velocity) {
  if (m) to_cpp(m)->NoteOff(ch(channel), static_cast<unsigned char>(pitch), static_cast<unsigned char>(velocity));
}
YSE_C_API void yse_midi_out_poly_pressure(YseMidiOut* m, int channel, int pitch, int value) {
  if (m) to_cpp(m)->PolyPressure(ch(channel), static_cast<unsigned char>(pitch), static_cast<unsigned char>(value));
}
YSE_C_API void yse_midi_out_channel_pressure(YseMidiOut* m, int channel, int value) {
  if (m) to_cpp(m)->ChannelPressure(ch(channel), static_cast<unsigned char>(value));
}
YSE_C_API void yse_midi_out_program_change(YseMidiOut* m, int channel, int value) {
  if (m) to_cpp(m)->ProgramChange(ch(channel), static_cast<unsigned char>(value));
}
YSE_C_API void yse_midi_out_control_change(YseMidiOut* m, int channel, int controller, int value) {
  if (m) to_cpp(m)->ControlChange(ch(channel), static_cast<unsigned char>(controller), static_cast<unsigned char>(value));
}
YSE_C_API void yse_midi_out_all_notes_off_channel(YseMidiOut* m, int channel) {
  if (m) to_cpp(m)->AllNotesOff(ch(channel));
}
YSE_C_API void yse_midi_out_all_notes_off(YseMidiOut* m) { if (m) to_cpp(m)->AllNotesOff(); }
YSE_C_API void yse_midi_out_reset_channel(YseMidiOut* m, int channel) { if (m) to_cpp(m)->Reset(ch(channel)); }
YSE_C_API void yse_midi_out_reset(YseMidiOut* m) { if (m) to_cpp(m)->Reset(); }

YSE_C_API void yse_midi_out_local_control(YseMidiOut* m, int on) { if (m) to_cpp(m)->LocalControl(on != 0); }
YSE_C_API void yse_midi_out_omni(YseMidiOut* m, int on)          { if (m) to_cpp(m)->Omni(on != 0); }
YSE_C_API void yse_midi_out_poly(YseMidiOut* m, int on)          { if (m) to_cpp(m)->Poly(on != 0); }

YSE_C_API void yse_midi_out_raw3(YseMidiOut* m, unsigned char a, unsigned char b, unsigned char c) {
  if (m) to_cpp(m)->Raw(a, b, c);
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
#endif

// ─── midiNote ──────────────────────────────────────────────────────

YSE_C_API YseMidiNote* yse_midi_note_create(unsigned char note, unsigned int velocity) {
  try { return reinterpret_cast<YseMidiNote*>(new YSE::MIDI::midiNote(note, velocity)); }
  catch (const std::exception& e) { yse_c::set_last_error(e.what()); return nullptr; }
  catch (...) { yse_c::set_last_error("midi_note_create: unknown C++ exception"); return nullptr; }
}
YSE_C_API void yse_midi_note_destroy(YseMidiNote* n) { if (n) delete to_cpp(n); }

YSE_C_API void          yse_midi_note_set_note(YseMidiNote* n, unsigned char v)     { if (n) to_cpp(n)->note(v); }
YSE_C_API unsigned char yse_midi_note_get_note(YseMidiNote* n)                      { return n ? to_cpp(n)->note() : 0; }
YSE_C_API void          yse_midi_note_set_velocity(YseMidiNote* n, unsigned char v) { if (n) to_cpp(n)->velocity(v); }
YSE_C_API unsigned char yse_midi_note_get_velocity(YseMidiNote* n)                  { return n ? to_cpp(n)->velocity() : 0; }

} // extern "C"
