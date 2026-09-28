Types, enums and constants
==========================

``yse.hpp`` pulls in three small headers that the rest of the API is written
in terms of:

- ``headers/types.hpp``: the fixed-size aliases (``Flt``, ``Dbl``, ``Int``,
  ``UInt`` …) and their atomic versions. They are in the global namespace.
- ``headers/enums.hpp``: the public enumerations, such as ``CHANNEL_TYPE``,
  ``REVERB_PRESET``, ``ERROR_LEVEL`` and the MIDI pitch and channel names.
- ``headers/constants.hpp``: the block size (``STANDARD_BUFFERSIZE``, 128
  samples) and the active sample rate (``SAMPLERATE``).

``headers/defines.hpp`` is also included but left out of this reference on
purpose. It holds the platform-detection macros (``YSE_WINDOWS``,
``YSE_LINUX`` …) and the ``API`` export macro, which the build sets up for
you.

Types
-----

.. doxygenfile:: headers/types.hpp
   :project: libYSE

Enums
-----

.. doxygenfile:: headers/enums.hpp
   :project: libYSE

Constants
---------

.. doxygenfile:: headers/constants.hpp
   :project: libYSE
