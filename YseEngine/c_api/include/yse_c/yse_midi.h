/*
  yse_midi.h — MIDI file playback + MIDI device output + midiNote helper.
  C ABI mirror of YseEngine/midi/{midifile,midiNote,device}.hpp.

  MIDI device output is Windows/Linux-only upstream (RtMidi-backed).
  Querying device counts/names lives in yse_system.h
  (yse_system_num_midi_out_devices, yse_system_midi_out_device_name).

  Channels are 0..15 (use the same indexing as YSE::MIDI::M_CHANNEL).
  Pitches are 0..127 raw MIDI note numbers.
*/

#ifndef YSE_C_MIDI_H_INCLUDED
#define YSE_C_MIDI_H_INCLUDED

#include "yse_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct YseMidiFile YseMidiFile;
typedef struct YseMidiOut  YseMidiOut;
typedef struct YseMidiNote YseMidiNote;

/* ─── standard MIDI file playback ─────────────────────────────────── */

YSE_C_API YseMidiFile* yse_midi_file_create(void);
YSE_C_API void         yse_midi_file_destroy(YseMidiFile* f);
YSE_C_API YseStatus    yse_midi_file_load(YseMidiFile* f, const char* filename);
YSE_C_API void         yse_midi_file_play(YseMidiFile* f);
YSE_C_API void         yse_midi_file_pause(YseMidiFile* f);
YSE_C_API void         yse_midi_file_stop(YseMidiFile* f);

/* ─── MIDI device output ──────────────────────────────────────────── */

YSE_C_API YseMidiOut*  yse_midi_out_create(void);
YSE_C_API void         yse_midi_out_destroy(YseMidiOut* m);
YSE_C_API void         yse_midi_out_open(YseMidiOut* m, unsigned int port);

YSE_C_API void         yse_midi_out_note_on(YseMidiOut* m, int channel, int pitch, int velocity);
YSE_C_API void         yse_midi_out_note_off(YseMidiOut* m, int channel, int pitch, int velocity);
YSE_C_API void         yse_midi_out_poly_pressure(YseMidiOut* m, int channel, int pitch, int value);
YSE_C_API void         yse_midi_out_channel_pressure(YseMidiOut* m, int channel, int value);
YSE_C_API void         yse_midi_out_program_change(YseMidiOut* m, int channel, int value);
YSE_C_API void         yse_midi_out_control_change(YseMidiOut* m, int channel, int controller, int value);

YSE_C_API void         yse_midi_out_all_notes_off_channel(YseMidiOut* m, int channel);
YSE_C_API void         yse_midi_out_all_notes_off(YseMidiOut* m);
YSE_C_API void         yse_midi_out_reset_channel(YseMidiOut* m, int channel);
YSE_C_API void         yse_midi_out_reset(YseMidiOut* m);

YSE_C_API void         yse_midi_out_local_control(YseMidiOut* m, int on);
YSE_C_API void         yse_midi_out_omni(YseMidiOut* m, int on);
YSE_C_API void         yse_midi_out_poly(YseMidiOut* m, int on);

YSE_C_API void         yse_midi_out_raw3(YseMidiOut* m, unsigned char a, unsigned char b, unsigned char c);

/* ─── midiNote convenience value ──────────────────────────────────── */

YSE_C_API YseMidiNote* yse_midi_note_create(unsigned char note, unsigned int velocity);
YSE_C_API void         yse_midi_note_destroy(YseMidiNote* n);
YSE_C_API void         yse_midi_note_set_note(YseMidiNote* n, unsigned char note);
YSE_C_API unsigned char yse_midi_note_get_note(YseMidiNote* n);
YSE_C_API void         yse_midi_note_set_velocity(YseMidiNote* n, unsigned char velocity);
YSE_C_API unsigned char yse_midi_note_get_velocity(YseMidiNote* n);

#ifdef __cplusplus
}
#endif

#endif
