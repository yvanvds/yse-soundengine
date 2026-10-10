MIDI
====

MIDI files and messages work in every build. MIDI device ports
(``midiIn``, ``midiOut``) exist only in builds with ``YSE_ENABLE_MIDI_DEVICE``.
:doc:`/intro/sessions_and_devices` explains how to list ports, and why a port
name can be empty.

.. doxygenfile:: midi/midifile.hpp
   :project: libYSE

.. doxygenfile:: midi/midiMessage.hpp
   :project: libYSE

.. doxygenfile:: midi/midiNote.hpp
   :project: libYSE

.. doxygenfile:: midi/device.hpp
   :project: libYSE
