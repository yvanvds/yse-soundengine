/**
  @file
  yse_dsp.h — single-channel audio buffers + buffer subclasses.
  C ABI mirror of YseEngine/dsp/{buffer,drawableBuffer,fileBuffer,wavetable}.hpp.

  One opaque handle type YseDspBuffer covers all four subclasses, which
  form a single non-polymorphic inheritance chain:

      buffer <- drawableBuffer <- fileBuffer <- wavetable

  DSP::buffer has no virtual members, so there is no runtime type check to
  perform: the subclass-specific entry points (drawLine, load/save,
  createSaw, ...) static_cast the handle and trust the caller. On those
  functions YSE_ERR_INVALID_HANDLE means the handle was NULL and nothing
  else — it is never a type-confusion guard, so do not build one on top of
  it.

  Because the chain is linear, a handle is valid for its own entry points
  and for those of every base: a wavetable handle may be passed to the
  drawable and fileBuffer functions, and a fileBuffer handle to the drawable
  ones. Narrowing the other way is undefined behaviour — a plain
  yse_dsp_buffer_create() handle passed to yse_dsp_buffer_draw_line(), or a
  fileBuffer handle passed to yse_dsp_wavetable_create_saw(), returns YSE_OK
  and writes through the static_cast, exactly as
  reinterpret_cast<wavetable*>() on a plain buffer would in C++. Bindings
  must enforce the subclass themselves by remembering which constructor
  produced the handle.

  Custom DSP source objects (subclassing dspSourceObject) are not wrapped:
  that surface runs a user callback on the audio thread and needs a design
  of its own (preallocated, allocation-free dispatch) before it can cross
  the C ABI. A patcher (yse_patcher.h) is the supported way to build a
  custom source from C.
*/

#ifndef YSE_C_DSP_H_INCLUDED
#define YSE_C_DSP_H_INCLUDED

#include "yse_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef YSE_C_HANDLE_YseDspBuffer
#define YSE_C_HANDLE_YseDspBuffer
/** Owned — release with yse_dsp_buffer_destroy, whichever of the four
   constructors produced it. The handle remembers which one did: internally it
   owns the engine object through a polymorphic wrapper, so destroy runs the
   most-derived destructor and frees the size that was really allocated. No
   type check is exposed and none is possible on the engine chain itself, which
   stays non-polymorphic (issue #662). */
typedef struct YseDspBuffer YseDspBuffer;
#endif

/** Constructors — one per subclass. The returned handle owns its native
   storage; pair with yse_dsp_buffer_destroy. */
YSE_C_API YseDspBuffer* yse_dsp_buffer_create(unsigned int length, unsigned int overflow);
YSE_C_API YseDspBuffer* yse_dsp_drawable_buffer_create(unsigned int length, unsigned int overflow);
YSE_C_API YseDspBuffer* yse_dsp_file_buffer_create(unsigned int length, unsigned int overflow);
YSE_C_API YseDspBuffer* yse_dsp_wavetable_create(unsigned int length);

YSE_C_API void yse_dsp_buffer_destroy(YseDspBuffer* buf);

/** Common buffer accessors. */
YSE_C_API unsigned int yse_dsp_buffer_length(YseDspBuffer* buf);
YSE_C_API unsigned int yse_dsp_buffer_length_ms(YseDspBuffer* buf);
YSE_C_API float yse_dsp_buffer_length_sec(YseDspBuffer* buf);
YSE_C_API int yse_dsp_buffer_is_silent(YseDspBuffer* buf);
YSE_C_API float yse_dsp_buffer_max_value(YseDspBuffer* buf);
YSE_C_API float yse_dsp_buffer_get_back(YseDspBuffer* buf);
YSE_C_API float yse_dsp_buffer_sample_rate_adjustment(YseDspBuffer* buf);
YSE_C_API void yse_dsp_buffer_set_sample_rate_adjustment(YseDspBuffer* buf, float v);

/** Resize. value is used to initialise newly added samples. */
YSE_C_API void yse_dsp_buffer_resize(YseDspBuffer* buf, unsigned int length, float value);

/** Bulk sample I/O. Copies count samples between the host array and the
   buffer storage starting at offset. Returns the actual number of samples
   copied (clamped to the buffer length). */
YSE_C_API unsigned int yse_dsp_buffer_read(YseDspBuffer* buf, unsigned int offset, float* out,
                                           unsigned int count);
YSE_C_API unsigned int yse_dsp_buffer_write(YseDspBuffer* buf, unsigned int offset, const float* in,
                                            unsigned int count);

/** Scalar fill / scalar math (operator= and operator+= et al on Flt). */
YSE_C_API void yse_dsp_buffer_fill(YseDspBuffer* buf, float value);
YSE_C_API void yse_dsp_buffer_add_scalar(YseDspBuffer* buf, float value);
YSE_C_API void yse_dsp_buffer_mul_scalar(YseDspBuffer* buf, float value);

/** Sample-wise buffer math (operator+= / -= / *= / /= on a buffer, issue #909):
   buf[i] op= other[i] over the first min(length(buf), length(other)) samples;
   samples past the shorter length are left untouched. Division by a zero
   sample yields 0, not inf. `buf` and `other` may be the same handle. A NULL
   `buf` or `other` is a no-op. */
YSE_C_API void yse_dsp_buffer_add_buffer(YseDspBuffer* buf, YseDspBuffer* other);
YSE_C_API void yse_dsp_buffer_sub_buffer(YseDspBuffer* buf, YseDspBuffer* other);
YSE_C_API void yse_dsp_buffer_mul_buffer(YseDspBuffer* buf, YseDspBuffer* other);
YSE_C_API void yse_dsp_buffer_div_buffer(YseDspBuffer* buf, YseDspBuffer* other);

/** Copy `count` samples from src[src_pos...] into dst[dst_pos...]
   (buffer::copyFrom, issue #909). Both ranges must lie inside their buffer's
   length: a range that runs past the end copies nothing and returns
   YSE_ERR_INVALID_ARGUMENT (the engine call would silently skip it). src and
   dst may be the same handle only for ranges that do not overlap. Returns
   YSE_ERR_INVALID_HANDLE for a NULL handle. */
YSE_C_API YseStatus yse_dsp_buffer_copy_from(YseDspBuffer* dst, YseDspBuffer* src,
                                             unsigned int src_pos, unsigned int dst_pos,
                                             unsigned int count);

/** Exchange the samples of two buffers of the same length (buffer::swap, issue
   #909). Each buffer keeps its own storage, sample-rate adjustment and
   subclass; only the sample values move. Returns YSE_ERR_INVALID_ARGUMENT
   (nothing swapped) when the lengths differ and YSE_ERR_INVALID_HANDLE for a
   NULL handle. Swapping a handle with itself is a successful no-op. */
YSE_C_API YseStatus yse_dsp_buffer_swap(YseDspBuffer* a, YseDspBuffer* b);

/** drawableBuffer-only. */
YSE_C_API YseStatus yse_dsp_buffer_draw_line(YseDspBuffer* buf, unsigned int start,
                                             unsigned int stop, float start_value,
                                             float stop_value);
YSE_C_API YseStatus yse_dsp_buffer_draw_flat(YseDspBuffer* buf, unsigned int start,
                                             unsigned int stop, float value);

/** fileBuffer-only. */
YSE_C_API YseStatus yse_dsp_buffer_load_file(YseDspBuffer* buf, const char* filename,
                                             unsigned int channel);

/** Writes the buffer to a mono 32-bit float WAV file. filename is used
   verbatim (no extension is appended). The file is written at the sample
   rate of the file this buffer was last loaded from, or at the engine rate
   if it was never loaded from one. Returns YSE_ERR_GENERIC (reason in
   yse_last_error) when the file cannot be created, the buffer is empty, or a
   custom BufferIO backend is active (yse_buffer_io_set_active(io, 1)): that
   backend is read-only, so save refuses rather than bypass it (#580, #637).
   File I/O — never call it from the audio callback. */
YSE_C_API YseStatus yse_dsp_buffer_save_file(YseDspBuffer* buf, const char* filename);

/** Native sample rate, in Hz, of the file this buffer was last loaded from;
   0 before a successful yse_dsp_buffer_load_file() and for a NULL handle.
   Unlike yse_dsp_buffer_sample_rate_adjustment() — a ratio against the
   engine rate in effect at load time — this does not change when the engine
   is re-initialised at another rate, so hosts can re-derive the playback
   speed against the live rate (#637). */
YSE_C_API float yse_dsp_buffer_get_file_sample_rate(YseDspBuffer* buf);

/** wavetable-only. */
YSE_C_API YseStatus yse_dsp_wavetable_create_saw(YseDspBuffer* buf, int harmonics, int length);
YSE_C_API YseStatus yse_dsp_wavetable_create_square(YseDspBuffer* buf, int harmonics, int length);
YSE_C_API YseStatus yse_dsp_wavetable_create_triangle(YseDspBuffer* buf, int harmonics, int length);

/* ─── multichannel source buffer (issue #909) ─────────────────────────────
   The C mirror of MULTICHANNELBUFFER: one sample buffer per channel, used to
   play a stereo / surround source held in memory through
   yse_sound_load_multi_buffer() (see yse_sound.h).

   The engine keeps a pointer to this storage for as long as a sound plays it,
   which is why it is its own owned handle rather than an argument list: the
   host decides when it is safe to free it. */

#ifndef YSE_C_HANDLE_YseDspMultiBuffer
#define YSE_C_HANDLE_YseDspMultiBuffer
/** Owned — release with yse_dsp_multi_buffer_destroy, and only once every sound
   loaded from it has been destroyed. */
typedef struct YseDspMultiBuffer YseDspMultiBuffer;
#endif

/** Build a multichannel buffer holding a COPY of `count` channel buffers, in
   order (channels[0] is the first output channel). The source handles are not
   retained: change or destroy them afterwards without affecting this one.
   Channels may differ in length; a sound plays the length of the shortest.
   Returns NULL with yse_last_error() set when `channels` is NULL, `count` is 0,
   or any entry is NULL. */
YSE_C_API YseDspMultiBuffer* yse_dsp_multi_buffer_create(YseDspBuffer* const* channels,
                                                         unsigned int count);
YSE_C_API void yse_dsp_multi_buffer_destroy(YseDspMultiBuffer* mb);

/** Number of channels; 0 on NULL. */
YSE_C_API unsigned int yse_dsp_multi_buffer_get_channel_count(YseDspMultiBuffer* mb);

#ifdef __cplusplus
}
#endif

#endif
