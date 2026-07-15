/*
  yse_midi.h — MIDI file playback + MIDI device output + MIDI device input
               + midiNote helper.
  C ABI mirror of YseEngine/midi/{midifile,midiNote,device}.hpp.

  MIDI device input and output are Windows/Linux-only upstream
  (RtMidi-backed). Querying device counts/names lives in yse_system.h
  (yse_system_num_midi_{in,out}_devices, yse_system_midi_{in,out}_device_name).

  Channels are 0..15 (use the same indexing as YSE::MIDI::M_CHANNEL).
  Pitches are 0..127 raw MIDI note numbers.

  Input callbacks fire on RtMidi's internal input thread. They MUST return
  quickly and MUST NOT block on I/O. The raw-bytes callback receives a
  malloc-allocated buffer that ownership transfers to the receiver — release
  it with yse_midi_in_free_message when finished. The parsed callback uses
  scalar arguments and transfers no ownership. The pre-decode of a parsed
  message takes the status nibble (bytes[0] & 0xF0) and channel nibble
  (bytes[0] & 0x0F); 1- and 2-byte messages report missing data bytes as 0.
*/

#ifndef YSE_C_MIDI_H_INCLUDED
#define YSE_C_MIDI_H_INCLUDED

#include "yse_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Owned — release with yse_midi_file_destroy. */
typedef struct YseMidiFile YseMidiFile;
/* Owned — release with yse_midi_out_destroy. */
typedef struct YseMidiOut YseMidiOut;
/* Owned — release with yse_midi_in_destroy. */
typedef struct YseMidiIn YseMidiIn;
/* Owned — release with yse_midi_note_destroy. */
typedef struct YseMidiNote YseMidiNote;
/* Opaque synth handle (defined in yse_synth.h) — target of
   yse_midi_file_connect_synth / yse_midi_in_connect_synth. */
typedef struct YseSynth YseSynth;

/* ─── standard MIDI file playback ─────────────────────────────────── */

YSE_C_API YseMidiFile* yse_midi_file_create(void);
YSE_C_API void yse_midi_file_destroy(YseMidiFile* f);
YSE_C_API YseStatus yse_midi_file_load(YseMidiFile* f, const char* filename);
YSE_C_API void yse_midi_file_play(YseMidiFile* f);
YSE_C_API void yse_midi_file_pause(YseMidiFile* f);
YSE_C_API void yse_midi_file_stop(YseMidiFile* f);

/* Route this file's playback into a synth (issue #372, mirrors
   YSE::MIDI::file::connect). While the file plays, every note / controller /
   pitch-bend event it contains is delivered to `synth` block-accurately on the
   audio thread; may be called for several synths (up to a small fixed cap) to
   drive them together. Calling it again for an already-connected synth is a
   no-op. `synth` is the opaque handle from yse_synth_create; a NULL file or a
   NULL / never-created synth is a no-op. connect only records the pointer in the
   file's lock-free synth table, so no loaded file or active playback is required
   here. The synth must outlive the connection — disconnect it (or destroy this
   file) before destroying the synth. This is a control-thread call; do not call
   it from the audio thread. */
YSE_C_API void yse_midi_file_connect_synth(YseMidiFile* f, YseSynth* synth);

/* Stop routing this file's playback into `synth` (issue #372). Safe to call for
   a synth that was never connected. */
YSE_C_API void yse_midi_file_disconnect_synth(YseMidiFile* f, YseSynth* synth);

/* ─── MIDI device output ──────────────────────────────────────────── */

YSE_C_API YseMidiOut* yse_midi_out_create(void);
YSE_C_API void yse_midi_out_destroy(YseMidiOut* m);
YSE_C_API void yse_midi_out_open(YseMidiOut* m, unsigned int port);

YSE_C_API void yse_midi_out_note_on(YseMidiOut* m, int channel, int pitch, int velocity);
YSE_C_API void yse_midi_out_note_off(YseMidiOut* m, int channel, int pitch, int velocity);
YSE_C_API void yse_midi_out_poly_pressure(YseMidiOut* m, int channel, int pitch, int value);
YSE_C_API void yse_midi_out_channel_pressure(YseMidiOut* m, int channel, int value);
YSE_C_API void yse_midi_out_program_change(YseMidiOut* m, int channel, int value);
YSE_C_API void yse_midi_out_control_change(YseMidiOut* m, int channel, int controller, int value);

YSE_C_API void yse_midi_out_all_notes_off_channel(YseMidiOut* m, int channel);
YSE_C_API void yse_midi_out_all_notes_off(YseMidiOut* m);
YSE_C_API void yse_midi_out_reset_channel(YseMidiOut* m, int channel);
YSE_C_API void yse_midi_out_reset(YseMidiOut* m);

YSE_C_API void yse_midi_out_local_control(YseMidiOut* m, int on);
YSE_C_API void yse_midi_out_omni(YseMidiOut* m, int on);
YSE_C_API void yse_midi_out_poly(YseMidiOut* m, int on);

YSE_C_API void yse_midi_out_raw3(YseMidiOut* m, unsigned char a, unsigned char b, unsigned char c);

/* ─── MIDI device input ───────────────────────────────────────────── */

/* Raw-bytes callback. `bytes` is malloc'd by the engine; the receiver owns
   it and must release with yse_midi_in_free_message. Length is in bytes. */
typedef void(YSE_C_CALLBACK* YseMidiInRawCallback)(double timestamp_sec, unsigned char* bytes,
                                                   size_t len, void* user_data);

/* Parsed callback. status/channel are pre-split from the first byte; data1
   and data2 are zero for messages shorter than 3 bytes. No ownership
   transfer. */
typedef void(YSE_C_CALLBACK* YseMidiInParsedCallback)(double timestamp_sec, unsigned char status,
                                                      unsigned char channel, unsigned char data1,
                                                      unsigned char data2, void* user_data);

YSE_C_API YseMidiIn* yse_midi_in_create(void);
YSE_C_API void yse_midi_in_destroy(YseMidiIn* m);
YSE_C_API void yse_midi_in_open(YseMidiIn* m, unsigned int port);
YSE_C_API void yse_midi_in_close(YseMidiIn* m);
YSE_C_API int yse_midi_in_is_open(YseMidiIn* m);

/* Register a raw callback. Pass NULL to detach. */
YSE_C_API void yse_midi_in_set_raw_callback(YseMidiIn* m, YseMidiInRawCallback cb, void* user_data);

/* Register a parsed callback. Pass NULL to detach. */
YSE_C_API void yse_midi_in_set_parsed_callback(YseMidiIn* m, YseMidiInParsedCallback cb,
                                               void* user_data);

/* Release a byte buffer previously delivered to a YseMidiInRawCallback. */
YSE_C_API void yse_midi_in_free_message(unsigned char* bytes);

/* Route incoming device MIDI into a synth (issue #155). Every channel-voice
   message received on the open port is mapped to the synth's normalized note
   API and pushed lock-free onto its inbox, on RtMidi's input thread.
   `channel_filter` is a 1..16 MIDI channel to accept, or 0 for every channel.
   May be called for several synths (up to a small fixed cap); calling it again
   for an already-connected synth just updates its channel filter. The synth
   must outlive the connection — disconnect it (or close/destroy this port)
   before destroying the synth. This is a control-thread call; do not call it
   from the input callbacks. On builds without MIDI device support it no-ops. */
YSE_C_API void yse_midi_in_connect_synth(YseMidiIn* m, YseSynth* synth, int channel_filter);

/* Stop routing incoming device MIDI into `synth` (issue #155). Safe to call for
   a synth that was never connected. */
YSE_C_API void yse_midi_in_disconnect_synth(YseMidiIn* m, YseSynth* synth);

/* ─── midiNote convenience value ──────────────────────────────────── */

YSE_C_API YseMidiNote* yse_midi_note_create(unsigned char note, unsigned int velocity);
YSE_C_API void yse_midi_note_destroy(YseMidiNote* n);
YSE_C_API void yse_midi_note_set_note(YseMidiNote* n, unsigned char note);
YSE_C_API unsigned char yse_midi_note_get_note(YseMidiNote* n);
YSE_C_API void yse_midi_note_set_velocity(YseMidiNote* n, unsigned char velocity);
YSE_C_API unsigned char yse_midi_note_get_velocity(YseMidiNote* n);

#ifdef __cplusplus
}
#endif

#endif
