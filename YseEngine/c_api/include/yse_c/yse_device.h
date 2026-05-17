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
*/

#ifndef YSE_C_DEVICE_H_INCLUDED
#define YSE_C_DEVICE_H_INCLUDED

#include "yse_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct YseDevice      YseDevice;
typedef struct YseDeviceSetup YseDeviceSetup;

/* Device descriptor — read-only. */
YSE_C_API size_t        yse_device_get_name(YseDevice* dev, char* buf, size_t cap);
YSE_C_API size_t        yse_device_get_type_name(YseDevice* dev, char* buf, size_t cap);

YSE_C_API unsigned int  yse_device_num_output_channels(YseDevice* dev);
YSE_C_API size_t        yse_device_get_output_channel_name(YseDevice* dev, unsigned int idx, char* buf, size_t cap);

YSE_C_API unsigned int  yse_device_num_input_channels(YseDevice* dev);
YSE_C_API size_t        yse_device_get_input_channel_name(YseDevice* dev, unsigned int idx, char* buf, size_t cap);

YSE_C_API unsigned int  yse_device_num_sample_rates(YseDevice* dev);
YSE_C_API double        yse_device_get_sample_rate(YseDevice* dev, unsigned int idx);

YSE_C_API unsigned int  yse_device_num_buffer_sizes(YseDevice* dev);
YSE_C_API int           yse_device_get_buffer_size(YseDevice* dev, unsigned int idx);

YSE_C_API int           yse_device_default_buffer_size(YseDevice* dev);
YSE_C_API int           yse_device_output_latency(YseDevice* dev);
YSE_C_API int           yse_device_input_latency(YseDevice* dev);
YSE_C_API int           yse_device_get_id(YseDevice* dev);

/* deviceSetup — owned configuration object passed to yse_system_open_device. */
YSE_C_API YseDeviceSetup* yse_device_setup_create(void);
YSE_C_API void            yse_device_setup_destroy(YseDeviceSetup* setup);
YSE_C_API void            yse_device_setup_set_input(YseDeviceSetup* setup, const YseDevice* dev);
YSE_C_API void            yse_device_setup_set_output(YseDeviceSetup* setup, const YseDevice* dev);
YSE_C_API void            yse_device_setup_set_sample_rate(YseDeviceSetup* setup, double value);
YSE_C_API void            yse_device_setup_set_buffer_size(YseDeviceSetup* setup, int value);

#ifdef __cplusplus
}
#endif

#endif
