#include "yse_c/yse_dsp_processor.h"
#include "yse_dsp_processor_internal.hpp"

extern "C" {

YSE_C_API void yse_dsp_processor_destroy(YseDspProcessor* processor) {
  // Through the virtual destructor: runs the wrapped class's destructor and
  // frees the size of the wrapper that was really allocated (see
  // yse_dsp_processor_internal.hpp).
  delete yse_c::processor::base(processor);
}

} // extern "C"
