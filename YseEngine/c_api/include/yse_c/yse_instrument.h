/*
  yse_instrument.h — loadable instrument assets for the synth voice pool.
  C ABI mirror of the SFZ sampler instrument (YseEngine/synth/samplerVoice.hpp,
  YSE::SYNTH::samplerInstrument / samplerConfig, issue #174) and the DX7 SysEx
  bank importer (YseEngine/dsp/fm/dx7Sysex.hpp, YSE::SYNTH::dx7Bank / dx7SysEx,
  issue #177).

  These are the SHAREABLE, engine-lifetime-independent assets a synth renders:
  a loaded SFZ instrument (region table + resident PCM) and a parsed DX7 bank
  (a list of FM patches). Build one here, then hand it to a synth voice group
  with yse_synth_add_voices_sampler() / yse_synth_fm_set_patch() (see
  yse_synth.h). Loading and parsing happen on the CALLING (setup / control)
  thread — never the audio thread; both read files and allocate.

  Ownership: an instrument / bank handle is OWNED by the caller — release it
  with yse_sfz_destroy() / yse_dx7_destroy(). The handles are reference-counted
  assets independent of the engine: a voice group that renders one retains its
  own share, so it is safe to destroy the handle right after add-voices, and it
  is safe to hold or destroy a handle across yse_system_close(). Every handle is
  tracked in a live-handle registry, so a double free or a use of an
  already-destroyed handle is a LOGGED NO-OP, never a crash (the engine's
  warning log records the misuse and the call is ignored).

  Convention: every void-returning function is a null-safe no-op on a NULL
  handle; loaders return NULL on failure with yse_last_error() set; status
  queries return 0 / empty on NULL or invalid handles.
*/

#ifndef YSE_C_INSTRUMENT_H_INCLUDED
#define YSE_C_INSTRUMENT_H_INCLUDED

#include "yse_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Owned — release with yse_sfz_destroy. A reference-counted SFZ instrument
   (region table + resident PCM). Share it across any number of synth voice
   groups via yse_synth_add_voices_sampler; each group retains its own share,
   so destroying this handle afterwards does not free the PCM until the last
   group is gone. Safe to destroy across yse_system_close(). */
typedef struct YseSfzInstrument YseSfzInstrument;

/* Owned — release with yse_dx7_destroy. A parsed DX7 SysEx bank (a list of FM
   patches, 1 or 32). Select a patch into a synth's FM voice group with
   yse_synth_fm_set_patch; the patch is copied into the synth, so the bank may
   be destroyed afterwards. Safe to destroy across yse_system_close(). */
typedef struct YseDx7Bank YseDx7Bank;

/* ─── SFZ sampler instrument ─────────────────────────────────────────────── */

/* Load and preload an .sfz file into a shareable instrument. Parses the file
   and decodes every unique sample into RAM on the calling thread (off the
   audio thread). Returns NULL on failure (unreadable / empty file, no playable
   region) with yse_last_error() set. */
YSE_C_API YseSfzInstrument* yse_sfz_load(const char* path);

/* One-region convenience creator — the samplerConfig facade (spec §11) as a
   flat, ffigen-friendly param struct. Builds a single-region instrument around
   one sample file without an .sfz text file, decoding the sample on the calling
   thread. `name` may be NULL (identification only). `file` is the absolute
   sample path. `root` is the key that plays the sample untransposed; `low` /
   `high` bound the playable key range. `attack` / `release` are the amplitude
   envelope times in seconds; `max_length` caps a non-looping one-shot in
   seconds. Mirrors YSE::SYNTH::samplerConfig. */
typedef struct YseSamplerConfig {
  const char* name; /* Instrument label (may be NULL). */
  const char* file; /* Absolute path to the sample file (sample=). */
  int root; /* Root note (key that plays untransposed). */
  int low; /* Lowest playable key. */
  int high; /* Highest playable key. */
  float attack; /* Amplitude-envelope attack, seconds. */
  float release; /* Amplitude-envelope release, seconds. */
  float max_length; /* One-shot length cap, seconds (non-looping regions). */
} YseSamplerConfig;

/* Build a one-region instrument from a YseSamplerConfig. Returns NULL on
   failure (NULL cfg, missing / unreadable sample) with yse_last_error() set. */
YSE_C_API YseSfzInstrument* yse_sfz_load_config(const YseSamplerConfig* cfg);

/* Whether the instrument is playable (valid region table + at least one
   resident sample). 0 on a NULL or already-destroyed handle. */
YSE_C_API int yse_sfz_is_valid(YseSfzInstrument* h);

/* Release an instrument handle. A double free or a NULL handle is a logged
   no-op, not a crash. */
YSE_C_API void yse_sfz_destroy(YseSfzInstrument* h);

/* ─── DX7 SysEx bank ─────────────────────────────────────────────────────── */

/* Load and parse a DX7 .syx file into a bank. A 32-voice packed bulk dump
   yields 32 patches; a single-voice dump yields 1. Reads the file on the
   calling thread. Returns NULL on failure (file not found / unreadable, bad
   header, wrong length, checksum mismatch) with yse_last_error() set. */
YSE_C_API YseDx7Bank* yse_dx7_import_sysex(const char* path);

/* Number of patches in the bank. 0 on a NULL or already-destroyed handle. */
YSE_C_API int yse_dx7_get_patch_count(YseDx7Bank* h);

/* Write the (space-trimmed) name of patch `index` into `buf` as a
   NUL-terminated string, snprintf-style; returns the length that would have
   been written (excluding the NUL), or 0 for an out-of-range index or a NULL /
   destroyed handle. `buf` may be NULL to query the length. */
YSE_C_API size_t yse_dx7_get_patch_name(YseDx7Bank* h, int index, char* buf, size_t cap);

/* Release a bank handle. A double free or a NULL handle is a logged no-op,
   not a crash. */
YSE_C_API void yse_dx7_destroy(YseDx7Bank* h);

#ifdef __cplusplus
}
#endif

#endif
