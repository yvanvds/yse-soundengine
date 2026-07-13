C ABI (language bindings)
=========================

libYSE ships a flat ``extern "C"`` API alongside the C++ one, folded into the
same shared library by the ``YSE_BUILD_C_API=ON`` CMake option (default on).
It exists so language bindings — Dart FFI, Python ctypes, etc. — can consume
``libyse.dll`` / ``libyse.so`` without C++ ABI compatibility.

Single entry point:

.. code-block:: c

   #include "yse_c/yse_all.h"

This umbrella header pulls in every subsystem header below.

Conventions
-----------

- Borrowed singletons (system, listener) are obtained via getters and **must
  not** be destroyed by the caller.
- Owned objects (sound, channel, reverb, patcher, device-setup, …) are
  created with ``yse_<type>_create*`` and released with
  ``yse_<type>_destroy``. ``destroy`` is always safe to call with ``NULL``.
- Callback typedefs are marked ``YSE_C_CALLBACK`` and run on engine-managed
  threads (often the audio thread). They must not block, allocate, or call
  back into the engine.
- All functions return either ``void``, an ``int`` (0 = success, non-zero =
  error code), or an owned/borrowed handle. Pointer parameters are
  documented as ``IN`` / ``OUT`` in the header.

Umbrella header
---------------

.. doxygenfile:: c_api/include/yse_c/yse_all.h
   :project: libYSE

System and listener
-------------------

.. doxygenfile:: c_api/include/yse_c/yse_system.h
   :project: libYSE

.. doxygenfile:: c_api/include/yse_c/yse_listener.h
   :project: libYSE

Sound objects, channels, and reverb
-----------------------------------

.. doxygenfile:: c_api/include/yse_c/yse_sound.h
   :project: libYSE

.. doxygenfile:: c_api/include/yse_c/yse_channel.h
   :project: libYSE

.. doxygenfile:: c_api/include/yse_c/yse_reverb.h
   :project: libYSE

Audio device
------------

.. doxygenfile:: c_api/include/yse_c/yse_device.h
   :project: libYSE

DSP and patcher
---------------

.. doxygenfile:: c_api/include/yse_c/yse_dsp.h
   :project: libYSE

.. doxygenfile:: c_api/include/yse_c/yse_dsp_modules.h
   :project: libYSE

.. doxygenfile:: c_api/include/yse_c/yse_patcher.h
   :project: libYSE

Synth and instruments
---------------------

.. doxygenfile:: c_api/include/yse_c/yse_synth.h
   :project: libYSE

.. doxygenfile:: c_api/include/yse_c/yse_instrument.h
   :project: libYSE

MIDI and music
--------------

.. doxygenfile:: c_api/include/yse_c/yse_midi.h
   :project: libYSE

.. doxygenfile:: c_api/include/yse_c/yse_music.h
   :project: libYSE

Logging and buffer I/O
----------------------

.. doxygenfile:: c_api/include/yse_c/yse_log.h
   :project: libYSE

.. doxygenfile:: c_api/include/yse_c/yse_buffer_io.h
   :project: libYSE

Common types and enums
----------------------

.. doxygenfile:: c_api/include/yse_c/yse_common.h
   :project: libYSE

.. doxygenfile:: c_api/include/yse_c/yse_enums.h
   :project: libYSE
