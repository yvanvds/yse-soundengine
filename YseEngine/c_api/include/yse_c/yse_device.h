/*
  yse_device.h — audio device descriptors + device-open configuration.
  C ABI mirror of YseEngine/device/deviceInterface.hpp + deviceSetup.hpp.

  Device descriptors are read-only and borrowed from the engine; never destroy
  one returned by yse_system_get_device(). deviceSetup objects are owned by
  the caller — pair yse_device_setup_create() with yse_device_setup_destroy().

  String-out functions use the snprintf convention: at most (cap - 1) bytes
  are copied into the buffer and a NUL terminator is appended. The return
  value is the full length of the string (excluding the NUL); pass cap=0 to
  query the required size before allocating.

  The indexed getters (channel names, sample rates, buffer sizes) are
  bound-checked: an index at or beyond the matching yse_device_num_*() count
  yields the same result as a NULL handle — an empty out buffer and a return
  of 0 / 0.0 — so iterating with a stale count is safe.

  The scalar getters below always report a defined value: a descriptor starts
  with the three size/latency fields at 0 and its device id at -1, and the
  engine only ever overwrites them with real data, so there is no indeterminate
  read to guard against.
*/

#ifndef YSE_C_DEVICE_H_INCLUDED
#define YSE_C_DEVICE_H_INCLUDED

#include "yse_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Borrowed — read-only descriptor enumerated from the engine via
   yse_system_get_device(). Never destroy. */
typedef struct YseDevice YseDevice;
/* Owned — release with yse_device_setup_destroy. */
typedef struct YseDeviceSetup YseDeviceSetup;

/* Device descriptor — read-only. */
YSE_C_API size_t yse_device_get_name(YseDevice* dev, char* buf, size_t cap);
YSE_C_API size_t yse_device_get_type_name(YseDevice* dev, char* buf, size_t cap);

YSE_C_API unsigned int yse_device_num_output_channels(YseDevice* dev);
YSE_C_API size_t yse_device_get_output_channel_name(YseDevice* dev, unsigned int idx, char* buf,
                                                    size_t cap);

YSE_C_API unsigned int yse_device_num_input_channels(YseDevice* dev);
YSE_C_API size_t yse_device_get_input_channel_name(YseDevice* dev, unsigned int idx, char* buf,
                                                   size_t cap);

YSE_C_API unsigned int yse_device_num_sample_rates(YseDevice* dev);
YSE_C_API double yse_device_get_sample_rate(YseDevice* dev, unsigned int idx);

YSE_C_API unsigned int yse_device_num_buffer_sizes(YseDevice* dev);
YSE_C_API int yse_device_get_buffer_size(YseDevice* dev, unsigned int idx);

/* 0 from yse_device_default_buffer_size() means "the host does not advertise
   one", not "zero frames": passed on to yse_device_setup_set_buffer_size() it
   asks the backend to choose. The PortAudio enumerator reports 0 for every
   device (PaDeviceInfo has no equivalent field), so read the negotiated size
   back from yse_system_get_active_buffer_size() once the device is open. */
YSE_C_API int yse_device_default_buffer_size(YseDevice* dev);
YSE_C_API int yse_device_output_latency(YseDevice* dev);
YSE_C_API int yse_device_input_latency(YseDevice* dev);

/* -1 from yse_device_get_id() means "no device", not "device index -1": it is
   PortAudio's paNoDevice, what a descriptor reports before the engine fills it
   in, and what a NULL handle reports. Every device from
   yse_system_get_device() carries a real, non-negative index. Handing a setup
   whose output device still reads -1 to yse_system_open_device() is refused
   with a log line and leaves the running stream alone. */
YSE_C_API int yse_device_get_id(YseDevice* dev);

/* deviceSetup — owned configuration object passed to yse_system_open_device. */
YSE_C_API YseDeviceSetup* yse_device_setup_create(void);
YSE_C_API void yse_device_setup_destroy(YseDeviceSetup* setup);
YSE_C_API void yse_device_setup_set_input(YseDeviceSetup* setup, const YseDevice* dev);
YSE_C_API void yse_device_setup_set_output(YseDeviceSetup* setup, const YseDevice* dev);
YSE_C_API void yse_device_setup_set_sample_rate(YseDeviceSetup* setup, double value);
YSE_C_API void yse_device_setup_set_buffer_size(YseDeviceSetup* setup, int value);

#ifdef __cplusplus
}
#endif

#endif
