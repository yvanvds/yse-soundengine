/*
  yse_clip.h — clip transport (issue #250).
  C ABI mirror of YseEngine/clip/clip.hpp.

  A clip transport loops a flat, beat-timed note-event list against a domain
  clock (see yse_system_create_clock in yse_system.h), dispatched from the audio
  thread — so the FFI/UI side never dispatches a note. The event list is
  replaceable while playing; the swap is lock-free with respect to the audio
  thread and sounding notes still receive their note-off across the swap.

  Output targets one or more YseSynth instances (RT-safe internal event queues)
  and — on builds with MIDI device support — one or more external MIDI output
  ports (issue #350): the audio thread stamps fired messages with an absolute
  send time and a dedicated sender thread performs the RtMidi sends, so the
  audio callback never touches the device. Channels are 1..16; pitches are
  0..127; velocity and pitch bend are normalized (see YseClipEvent).
*/

#ifndef YSE_C_CLIP_H_INCLUDED
#define YSE_C_CLIP_H_INCLUDED

#include "yse_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Owned — release with yse_clip_destroy. */
typedef struct YseClip YseClip;
/* Opaque synth handle (defined in yse_synth.h). */
typedef struct YseSynth YseSynth;
/* Opaque MIDI-out handle (defined in yse_midi.h). */
typedef struct YseMidiOut YseMidiOut;

/* One timed note event, positioned in beats on the bound domain clock.
   Layout-compatible with YSE::clipEvent. */
typedef struct YseClipEvent {
  double start_beat; /* beat within the loop at which the note starts (>= 0) */
  double duration_beats; /* note length in beats */
  int channel; /* MIDI channel, 1..16 */
  int pitch; /* MIDI note number, 0..127 */
  float velocity; /* normalized to [0, 1] */
  float pitch_bend; /* optional per-note bend, [-1, 1]; 0 = none */
} YseClipEvent;

YSE_C_API YseClip* yse_clip_create(void);
YSE_C_API void yse_clip_destroy(YseClip* c);

/* Bind the clip to a live domain clock by name. Returns 1 on success, 0 if no
   live clock owns the name (or on NULL args). The bound clock must outlive the
   clip. */
YSE_C_API int yse_clip_bind(YseClip* c, const char* clock_name);

/* Replace the event list (copied). Takes effect at the next audio block
   boundary. `events` may be NULL only when `count` is 0 (clears the list). */
YSE_C_API void yse_clip_set_events(YseClip* c, const YseClipEvent* events, size_t count);

/* Loop length in beats. Values <= 0 disable looping (events fire once). */
YSE_C_API void yse_clip_set_loop_length(YseClip* c, double beats);

/* Route this clip's playback into a synth (may be called for several synths).
   The synth must outlive the connection. */
YSE_C_API void yse_clip_connect_synth(YseClip* c, YseSynth* synth);
YSE_C_API void yse_clip_disconnect_synth(YseClip* c, YseSynth* synth);

/* Route this clip's playback to an external MIDI output port (issue #350).
   `m` must already have opened a port (yse_midi_out_open); connecting an
   unopened handle is ignored. The underlying device port is engine-owned and
   stays open, so `m` itself may be destroyed after connecting. May be called
   for several ports. The audio thread stamps fired messages with an absolute
   send time and hands them to a dedicated sender thread over a bounded
   lock-free queue — timing stays block-accurate, and the audio callback never
   performs the send. On builds without MIDI device support these set
   last_error and no-op. */
YSE_C_API void yse_clip_connect_midi_out(YseClip* c, YseMidiOut* m);
YSE_C_API void yse_clip_disconnect_midi_out(YseClip* c, YseMidiOut* m);

YSE_C_API void yse_clip_play(YseClip* c);
YSE_C_API void yse_clip_stop(YseClip* c);
YSE_C_API int yse_clip_is_playing(YseClip* c);

#ifdef __cplusplus
}
#endif

#endif
