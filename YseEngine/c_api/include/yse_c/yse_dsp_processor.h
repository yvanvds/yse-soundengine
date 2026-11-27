/**
  @file
  yse_dsp_processor.h — the shared handle for block-processing DSP classes.
  C ABI home of YseDspProcessor, the handle every wrapped YSE::DSP processing
  class of the MSP port (#1034, wrapped by #1173) crosses the ABI as. The
  classes themselves are declared by category: yse_dsp_filters.h,
  yse_dsp_filter_design.h, ...

  A processor is a standalone signal-processing object, not an effect: it is
  not attached to a sound or channel and the engine never runs it. The host
  drives it, one call per block of its own samples, from whatever thread it
  likes (offline rendering, analysis, its own audio callback). For a chainable
  insert effect see yse_dsp_modules.h; for a graph see yse_patcher.h.

  Conventions shared by every processor header:

  - Each class has its own constructor, yse_dsp_CLASS_create(); every
    handle is released with yse_dsp_processor_destroy().
  - Unlike YseDspBuffer and YseDspObject, the class of a handle IS checked.
    A handle passed to another class's function is treated like NULL: a
    setter does nothing, a getter returns 0, a process function returns 0.
  - A NULL handle follows the same rule: void functions are no-ops, queries
    return 0.
  - Samples cross as float arrays the host owns. A process function takes
    its inputs and writes its outputs as `length` samples each, and returns
    the number of samples written: `length`, or 0 when the handle is NULL or
    of another class or a required array is NULL (nothing is written then).
    An output array may be the same array as an input (in-place
    processing). Optional outputs are documented as such and may be NULL.
  - Any length is accepted. The engine classes process at most 128 samples
    (STANDARD_BUFFERSIZE) per call, so a longer array is processed as
    consecutive blocks of 128; a class that reads a setting "once per block"
    reads it once per 128 samples.
  - Process, sample and setter functions allocate nothing, take no lock and
    never set yse_last_error(): they are safe on a real-time thread.
    Constructors allocate. A processor is not thread-safe: call one handle
    from one thread at a time.
  - The classes read the live engine sample rate (48000 until a session
    opens at another one), and follow it when it changes.
*/

#ifndef YSE_C_DSP_PROCESSOR_H_INCLUDED
#define YSE_C_DSP_PROCESSOR_H_INCLUDED

#include "yse_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef YSE_C_HANDLE_YseDspProcessor
#define YSE_C_HANDLE_YseDspProcessor
/** Owned — release with yse_dsp_processor_destroy, whichever class's
   constructor produced it. The handle remembers its class: destroy runs the
   right destructor, and the class's own functions refuse a handle of another
   class. */
typedef struct YseDspProcessor YseDspProcessor;
#endif

/** Release a processor of any class. NULL is a no-op. */
YSE_C_API void yse_dsp_processor_destroy(YseDspProcessor* processor);

#ifdef __cplusplus
}
#endif

#endif
