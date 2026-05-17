/*
  yse_dsp.h — single-channel audio buffers + buffer subclasses.
  C ABI mirror of YseEngine/dsp/{buffer,drawableBuffer,fileBuffer,wavetable}.hpp.

  One opaque handle type YseDspBuffer covers all four subclasses; the
  subclass-specific entry points (drawLine, load/save, createSaw, ...)
  dynamic_cast<> internally and report YSE_ERR_INVALID_HANDLE when the
  handle isn't of the expected type. Callers always know which subclass
  they created, so the cast cost is only paid as a safety net.

  Custom DSP source objects (subclassing dspSourceObject) are not yet
  wrapped — that surface needs audio-thread callbacks, which lands in a
  later milestone alongside the patcher callback plumbing.
*/

#ifndef YSE_C_DSP_H_INCLUDED
#define YSE_C_DSP_H_INCLUDED

#include "yse_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct YseDspBuffer YseDspBuffer;

/* Constructors — one per subclass. The returned handle owns its native
   storage; pair with yse_dsp_buffer_destroy. */
YSE_C_API YseDspBuffer* yse_dsp_buffer_create(unsigned int length, unsigned int overflow);
YSE_C_API YseDspBuffer* yse_dsp_drawable_buffer_create(unsigned int length, unsigned int overflow);
YSE_C_API YseDspBuffer* yse_dsp_file_buffer_create(unsigned int length, unsigned int overflow);
YSE_C_API YseDspBuffer* yse_dsp_wavetable_create(unsigned int length);

YSE_C_API void          yse_dsp_buffer_destroy(YseDspBuffer* buf);

/* Common buffer accessors. */
YSE_C_API unsigned int  yse_dsp_buffer_length(YseDspBuffer* buf);
YSE_C_API unsigned int  yse_dsp_buffer_length_ms(YseDspBuffer* buf);
YSE_C_API float         yse_dsp_buffer_length_sec(YseDspBuffer* buf);
YSE_C_API int           yse_dsp_buffer_is_silent(YseDspBuffer* buf);
YSE_C_API float         yse_dsp_buffer_max_value(YseDspBuffer* buf);
YSE_C_API float         yse_dsp_buffer_get_back(YseDspBuffer* buf);
YSE_C_API float         yse_dsp_buffer_sample_rate_adjustment(YseDspBuffer* buf);
YSE_C_API void          yse_dsp_buffer_set_sample_rate_adjustment(YseDspBuffer* buf, float v);

/* Resize. value is used to initialise newly added samples. */
YSE_C_API void          yse_dsp_buffer_resize(YseDspBuffer* buf, unsigned int length, float value);

/* Bulk sample I/O. Copies count samples between the host array and the
   buffer storage starting at offset. Returns the actual number of samples
   copied (clamped to the buffer length). */
YSE_C_API unsigned int  yse_dsp_buffer_read(YseDspBuffer* buf, unsigned int offset, float* out, unsigned int count);
YSE_C_API unsigned int  yse_dsp_buffer_write(YseDspBuffer* buf, unsigned int offset, const float* in, unsigned int count);

/* Scalar fill / scalar math (operator= and operator+= et al on Flt). */
YSE_C_API void          yse_dsp_buffer_fill(YseDspBuffer* buf, float value);
YSE_C_API void          yse_dsp_buffer_add_scalar(YseDspBuffer* buf, float value);
YSE_C_API void          yse_dsp_buffer_mul_scalar(YseDspBuffer* buf, float value);

/* drawableBuffer-only. */
YSE_C_API YseStatus     yse_dsp_buffer_draw_line(YseDspBuffer* buf, unsigned int start, unsigned int stop, float start_value, float stop_value);
YSE_C_API YseStatus     yse_dsp_buffer_draw_flat(YseDspBuffer* buf, unsigned int start, unsigned int stop, float value);

/* fileBuffer-only. */
YSE_C_API YseStatus     yse_dsp_buffer_load_file(YseDspBuffer* buf, const char* filename, unsigned int channel);
YSE_C_API YseStatus     yse_dsp_buffer_save_file(YseDspBuffer* buf, const char* filename);

/* wavetable-only. */
YSE_C_API YseStatus     yse_dsp_wavetable_create_saw(YseDspBuffer* buf, int harmonics, int length);
YSE_C_API YseStatus     yse_dsp_wavetable_create_square(YseDspBuffer* buf, int harmonics, int length);
YSE_C_API YseStatus     yse_dsp_wavetable_create_triangle(YseDspBuffer* buf, int harmonics, int length);

#ifdef __cplusplus
}
#endif

#endif
