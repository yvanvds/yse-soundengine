#include "yse_c/yse_device.h"
#include "yse_c_internal.hpp"

#include "../device/deviceInterface.hpp"
#include "../device/deviceSetup.hpp"

#include <cstring>
#include <exception>
#include <string>

namespace {
  inline YSE::device* to_cpp(YseDevice* d) {
    return reinterpret_cast<YSE::device*>(d);
  }
  inline const YSE::device* to_cpp(const YseDevice* d) {
    return reinterpret_cast<const YSE::device*>(d);
  }
  inline YSE::deviceSetup* to_cpp(YseDeviceSetup* s) {
    return reinterpret_cast<YSE::deviceSetup*>(s);
  }

  // snprintf-style string copy: writes at most cap-1 bytes plus terminator
  // into buf, returns the full source length (excluding terminator).
  size_t copy_string(const std::string& src, char* buf, size_t cap) {
    if (buf != nullptr && cap > 0) {
      const size_t n = src.size() < cap - 1 ? src.size() : cap - 1;
      std::memcpy(buf, src.data(), n);
      buf[n] = '\0';
    }
    return src.size();
  }
}

extern "C" {

// ─── device descriptor ─────────────────────────────────────────────────────

YSE_C_API size_t yse_device_get_name(YseDevice* dev, char* buf, size_t cap) {
  if (!dev) { if (buf && cap > 0) buf[0] = '\0'; return 0; }
  return copy_string(to_cpp(dev)->getName(), buf, cap);
}

YSE_C_API size_t yse_device_get_type_name(YseDevice* dev, char* buf, size_t cap) {
  if (!dev) { if (buf && cap > 0) buf[0] = '\0'; return 0; }
  return copy_string(to_cpp(dev)->getTypeName(), buf, cap);
}

YSE_C_API unsigned int yse_device_num_output_channels(YseDevice* dev) {
  return dev ? to_cpp(dev)->getNumOutputChannelNames() : 0;
}

YSE_C_API size_t yse_device_get_output_channel_name(YseDevice* dev, unsigned int idx, char* buf, size_t cap) {
  if (!dev) { if (buf && cap > 0) buf[0] = '\0'; return 0; }
  try {
    return copy_string(to_cpp(dev)->getOutputChannelName(idx), buf, cap);
  } catch (const std::exception&) {
    if (buf && cap > 0) buf[0] = '\0';
    return 0;
  }
}

YSE_C_API unsigned int yse_device_num_input_channels(YseDevice* dev) {
  return dev ? to_cpp(dev)->getNumInputChannelNames() : 0;
}

YSE_C_API size_t yse_device_get_input_channel_name(YseDevice* dev, unsigned int idx, char* buf, size_t cap) {
  if (!dev) { if (buf && cap > 0) buf[0] = '\0'; return 0; }
  try {
    return copy_string(to_cpp(dev)->getInputChannelName(idx), buf, cap);
  } catch (const std::exception&) {
    if (buf && cap > 0) buf[0] = '\0';
    return 0;
  }
}

YSE_C_API unsigned int yse_device_num_sample_rates(YseDevice* dev) {
  return dev ? to_cpp(dev)->getNumAvailableSampleRates() : 0;
}
YSE_C_API double yse_device_get_sample_rate(YseDevice* dev, unsigned int idx) {
  return dev ? to_cpp(dev)->getAvailableSampleRate(idx) : 0.0;
}

YSE_C_API unsigned int yse_device_num_buffer_sizes(YseDevice* dev) {
  return dev ? to_cpp(dev)->getNumAvailableBufferSizes() : 0;
}
YSE_C_API int yse_device_get_buffer_size(YseDevice* dev, unsigned int idx) {
  return dev ? to_cpp(dev)->getAvailableBufferSize(idx) : 0;
}

YSE_C_API int yse_device_default_buffer_size(YseDevice* dev) { return dev ? to_cpp(dev)->getDefaultBufferSize() : 0; }
YSE_C_API int yse_device_output_latency(YseDevice* dev)      { return dev ? to_cpp(dev)->getOutputLatency()    : 0; }
YSE_C_API int yse_device_input_latency(YseDevice* dev)       { return dev ? to_cpp(dev)->getInputLatency()     : 0; }
YSE_C_API int yse_device_get_id(YseDevice* dev)              { return dev ? to_cpp(dev)->getID()               : 0; }

// ─── device setup ──────────────────────────────────────────────────────────

YSE_C_API YseDeviceSetup* yse_device_setup_create(void) {
  try {
    return reinterpret_cast<YseDeviceSetup*>(new YSE::deviceSetup());
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return nullptr;
  } catch (...) {
    yse_c::set_last_error("unknown C++ exception in yse_device_setup_create");
    return nullptr;
  }
}

YSE_C_API void yse_device_setup_destroy(YseDeviceSetup* setup) {
  if (!setup) return;
  delete to_cpp(setup);
}

YSE_C_API void yse_device_setup_set_input(YseDeviceSetup* setup, const YseDevice* dev) {
  if (!setup || !dev) return;
  to_cpp(setup)->setInput(*to_cpp(dev));
}

YSE_C_API void yse_device_setup_set_output(YseDeviceSetup* setup, const YseDevice* dev) {
  if (!setup || !dev) return;
  to_cpp(setup)->setOutput(*to_cpp(dev));
}

YSE_C_API void yse_device_setup_set_sample_rate(YseDeviceSetup* setup, double value) {
  if (setup) to_cpp(setup)->setSampleRate(value);
}

YSE_C_API void yse_device_setup_set_buffer_size(YseDeviceSetup* setup, int value) {
  if (setup) to_cpp(setup)->setBufferSize(value);
}

} // extern "C"
