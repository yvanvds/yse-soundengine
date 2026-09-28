Audio devices
=============

``YSE::device`` describes an audio device the engine found, and
``YSE::deviceSetup`` names the device to open with ``System().openDevice()``.
:doc:`/intro/sessions_and_devices` covers listing and opening devices, the
``-1`` device ID and ``0`` buffer size values, and what happens when an open
is refused.

.. doxygenfile:: device/device.hpp
   :project: libYSE

.. doxygenfile:: device/deviceInterface.hpp
   :project: libYSE

.. doxygenfile:: device/deviceSetup.hpp
   :project: libYSE
