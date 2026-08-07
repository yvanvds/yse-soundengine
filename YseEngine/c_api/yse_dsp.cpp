#include "yse_c/yse_dsp.h"
#include "yse_c_internal.hpp"

#include "../dsp/buffer.hpp"
#include "../dsp/drawableBuffer.hpp"
#include "../dsp/fileBuffer.hpp"
#include "../dsp/wavetable.hpp"

#include <algorithm>
#include <cstring>
#include <exception>

namespace {
  // ─── Handle ownership (issue #662) ─────────────────────────────────────────
  //
  // Four constructors hand out one opaque YseDspBuffer* for four different C++
  // types in the chain buffer <- drawableBuffer <- fileBuffer <- wavetable, and
  // a single destroy releases all of them. That chain is not polymorphic, so a
  // `delete` through DSP::buffer* is undefined behaviour ([expr.delete]/3): a
  // sized `operator delete` — the C++14 default — is handed sizeof(buffer) for
  // an allocation that is sizeof(fileBuffer) / sizeof(wavetable) bytes. The
  // sizes really do differ since fileBuffer gained its fileRate member (#637),
  // so allocators that trust the size hint free into the wrong size class.
  //
  // Giving DSP::buffer a virtual destructor would fix it but is rejected:
  // buffer is a plain value type on DSP paths and must not grow a vptr (nor
  // change layout for anything embedding it). So the vptr lives here instead —
  // the handle owns its buffer through a polymorphic wrapper that is private to
  // this translation unit and never crosses the ABI boundary. YseDspBuffer
  // stays an opaque typedef, so the C ABI is unchanged.
  struct bufferHandle {
    bufferHandle() = default;
    virtual ~bufferHandle() = default;
    bufferHandle(const bufferHandle&) = delete;
    bufferHandle& operator=(const bufferHandle&) = delete;

    // The buffer base subobject of the owned instance. Every non-destroying
    // entry point reaches the engine object through this.
    virtual YSE::DSP::buffer* get() noexcept = 0;
  };

  template <typename T> struct typedBufferHandle final : bufferHandle {
    T object;

    template <typename... Args> explicit typedBufferHandle(Args... args) : object(args...) {}

    YSE::DSP::buffer* get() noexcept override {
      return &object;
    }
  };

  inline bufferHandle* to_handle(YseDspBuffer* b) {
    return reinterpret_cast<bufferHandle*>(b);
  }

  inline YSE::DSP::buffer* to_cpp(YseDspBuffer* b) {
    return to_handle(b)->get();
  }

  // Allocate one wrapper holding a T, and return it as the opaque handle.
  // Destruction goes back through bufferHandle's virtual destructor, which
  // runs ~T() and frees sizeof(typedBufferHandle<T>) — the size that was
  // actually allocated.
  template <typename T, typename... Args> YseDspBuffer* make_handle(Args... args) {
    auto* h = new typedBufferHandle<T>(args...);
    return reinterpret_cast<YseDspBuffer*>(static_cast<bufferHandle*>(h));
  }

  // DSP::buffer has no virtual methods, so dynamic_cast is unavailable.
  // Subclass-specific entry points trust the caller: a YseDspBuffer*
  // created by yse_dsp_wavetable_create() is genuinely a wavetable and
  // safe to static_cast. Misuse is undefined behaviour — equivalent to
  // calling reinterpret_cast<wavetable*>() on a plain buffer in C++.
  template <typename T> T* as(YseDspBuffer* b) {
    return b ? static_cast<T*>(to_cpp(b)) : nullptr;
  }
} // namespace

namespace yse_c {
  YSE::DSP::buffer* buffer_from_handle(YseDspBuffer* h) {
    return h ? to_cpp(h) : nullptr;
  }
} // namespace yse_c

extern "C" {

YSE_C_API YseDspBuffer* yse_dsp_buffer_create(unsigned int length, unsigned int overflow) {
  try {
    return make_handle<YSE::DSP::buffer>(length, overflow);
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return nullptr;
  } catch (...) {
    yse_c::set_last_error("buffer_create: unknown C++ exception");
    return nullptr;
  }
}

YSE_C_API YseDspBuffer* yse_dsp_drawable_buffer_create(unsigned int length, unsigned int overflow) {
  try {
    return make_handle<YSE::DSP::drawableBuffer>(length, overflow);
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return nullptr;
  } catch (...) {
    yse_c::set_last_error("drawable_buffer_create: unknown C++ exception");
    return nullptr;
  }
}

YSE_C_API YseDspBuffer* yse_dsp_file_buffer_create(unsigned int length, unsigned int overflow) {
  try {
    return make_handle<YSE::DSP::fileBuffer>(length, overflow);
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return nullptr;
  } catch (...) {
    yse_c::set_last_error("file_buffer_create: unknown C++ exception");
    return nullptr;
  }
}

YSE_C_API YseDspBuffer* yse_dsp_wavetable_create(unsigned int length) {
  try {
    return make_handle<YSE::DSP::wavetable>(length);
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return nullptr;
  } catch (...) {
    yse_c::set_last_error("wavetable_create: unknown C++ exception");
    return nullptr;
  }
}

YSE_C_API void yse_dsp_buffer_destroy(YseDspBuffer* buf) {
  if (!buf) return;
  // Virtual destructor on the wrapper: runs the *most derived* engine
  // destructor and returns the size that was really allocated (issue #662).
  delete to_handle(buf);
}

YSE_C_API unsigned int yse_dsp_buffer_length(YseDspBuffer* buf) {
  return buf ? to_cpp(buf)->getLength() : 0;
}
YSE_C_API unsigned int yse_dsp_buffer_length_ms(YseDspBuffer* buf) {
  return buf ? to_cpp(buf)->getLengthMS() : 0;
}
YSE_C_API float yse_dsp_buffer_length_sec(YseDspBuffer* buf) {
  return buf ? to_cpp(buf)->getLengthSec() : 0.0f;
}
YSE_C_API int yse_dsp_buffer_is_silent(YseDspBuffer* buf) {
  return buf && to_cpp(buf)->isSilent() ? 1 : 0;
}
YSE_C_API float yse_dsp_buffer_max_value(YseDspBuffer* buf) {
  return buf ? to_cpp(buf)->maxValue() : 0.0f;
}
YSE_C_API float yse_dsp_buffer_get_back(YseDspBuffer* buf) {
  return buf ? to_cpp(buf)->getBack() : 0.0f;
}

YSE_C_API float yse_dsp_buffer_sample_rate_adjustment(YseDspBuffer* buf) {
  return buf ? to_cpp(buf)->getSampleRateAdjustment() : 0.0f;
}
YSE_C_API void yse_dsp_buffer_set_sample_rate_adjustment(YseDspBuffer* buf, float v) {
  if (buf) to_cpp(buf)->setSampleRateAdjustment(v);
}

YSE_C_API void yse_dsp_buffer_resize(YseDspBuffer* buf, unsigned int length, float value) {
  if (buf) to_cpp(buf)->resize(length, value);
}

YSE_C_API unsigned int yse_dsp_buffer_read(YseDspBuffer* buf, unsigned int offset, float* out,
                                           unsigned int count) {
  if (!buf || !out) return 0;
  const auto* cb = to_cpp(buf);
  const unsigned int len = cb->getLength();
  if (offset >= len) return 0;
  const unsigned int n = std::min(count, len - offset);
  std::memcpy(out, const_cast<YSE::DSP::buffer*>(cb)->getPtr() + offset, n * sizeof(float));
  return n;
}

YSE_C_API unsigned int yse_dsp_buffer_write(YseDspBuffer* buf, unsigned int offset, const float* in,
                                            unsigned int count) {
  if (!buf || !in) return 0;
  auto* cb = to_cpp(buf);
  const unsigned int len = cb->getLength();
  if (offset >= len) return 0;
  const unsigned int n = std::min(count, len - offset);
  std::memcpy(cb->getPtr() + offset, in, n * sizeof(float));
  return n;
}

YSE_C_API void yse_dsp_buffer_fill(YseDspBuffer* buf, float value) {
  if (buf) (*to_cpp(buf)) = value;
}
YSE_C_API void yse_dsp_buffer_add_scalar(YseDspBuffer* buf, float value) {
  if (buf) (*to_cpp(buf)) += value;
}
YSE_C_API void yse_dsp_buffer_mul_scalar(YseDspBuffer* buf, float value) {
  if (buf) (*to_cpp(buf)) *= value;
}

YSE_C_API YseStatus yse_dsp_buffer_draw_line(YseDspBuffer* buf, unsigned int start,
                                             unsigned int stop, float start_value,
                                             float stop_value) {
  auto* d = as<YSE::DSP::drawableBuffer>(buf);
  if (!d) {
    yse_c::set_last_error("buffer is not a drawableBuffer");
    return YSE_ERR_INVALID_HANDLE;
  }
  d->drawLine(start, stop, start_value, stop_value);
  return YSE_OK;
}

YSE_C_API YseStatus yse_dsp_buffer_draw_flat(YseDspBuffer* buf, unsigned int start,
                                             unsigned int stop, float value) {
  auto* d = as<YSE::DSP::drawableBuffer>(buf);
  if (!d) {
    yse_c::set_last_error("buffer is not a drawableBuffer");
    return YSE_ERR_INVALID_HANDLE;
  }
  d->drawLine(start, stop, value);
  return YSE_OK;
}

YSE_C_API YseStatus yse_dsp_buffer_load_file(YseDspBuffer* buf, const char* filename,
                                             unsigned int channel) {
  auto* f = as<YSE::DSP::fileBuffer>(buf);
  if (!f) {
    yse_c::set_last_error("buffer is not a fileBuffer");
    return YSE_ERR_INVALID_HANDLE;
  }
  if (!filename) return YSE_ERR_INVALID_ARGUMENT;
  if (!f->load(filename, channel)) {
    yse_c::set_last_error(std::string("file_buffer load failed for: ") + filename);
    return YSE_ERR_FILE_NOT_FOUND;
  }
  return YSE_OK;
}

YSE_C_API YseStatus yse_dsp_buffer_save_file(YseDspBuffer* buf, const char* filename) {
  auto* f = as<YSE::DSP::fileBuffer>(buf);
  if (!f) {
    yse_c::set_last_error("buffer is not a fileBuffer");
    return YSE_ERR_INVALID_HANDLE;
  }
  if (!filename) return YSE_ERR_INVALID_ARGUMENT;
  if (!f->save(filename)) {
    yse_c::set_last_error(std::string("file_buffer save failed for: ") + filename);
    return YSE_ERR_GENERIC;
  }
  return YSE_OK;
}

YSE_C_API YseStatus yse_dsp_wavetable_create_saw(YseDspBuffer* buf, int harmonics, int length) {
  auto* w = as<YSE::DSP::wavetable>(buf);
  if (!w) {
    yse_c::set_last_error("buffer is not a wavetable");
    return YSE_ERR_INVALID_HANDLE;
  }
  w->createSaw(harmonics, length);
  return YSE_OK;
}

YSE_C_API YseStatus yse_dsp_wavetable_create_square(YseDspBuffer* buf, int harmonics, int length) {
  auto* w = as<YSE::DSP::wavetable>(buf);
  if (!w) {
    yse_c::set_last_error("buffer is not a wavetable");
    return YSE_ERR_INVALID_HANDLE;
  }
  w->createSquare(harmonics, length);
  return YSE_OK;
}

YSE_C_API YseStatus yse_dsp_wavetable_create_triangle(YseDspBuffer* buf, int harmonics,
                                                      int length) {
  auto* w = as<YSE::DSP::wavetable>(buf);
  if (!w) {
    yse_c::set_last_error("buffer is not a wavetable");
    return YSE_ERR_INVALID_HANDLE;
  }
  w->createTriangle(harmonics, length);
  return YSE_OK;
}

} // extern "C"
