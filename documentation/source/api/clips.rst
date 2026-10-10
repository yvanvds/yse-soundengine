Clocks and clips
================

A **domain clock** is a named beat clock owned by the engine. You create and
steer it through ``YSE::System()``: ``createClock``, ``destroyClock``,
``clockExists``, ``setTempo``, ``beatPosition`` and ``currentTempo`` (see
:doc:`core`). A **clip** plays a looping list of beat-timed notes against one
of those clocks, into synths or MIDI output ports.

:doc:`/tutorials/15_clocks_and_clips` walks through both, and
:doc:`/patcher/time` shows how a patch uses the same clocks. The C API
equivalents are in :doc:`c_api` (``yse_clip.h`` and the clock functions in
``yse_system.h``).

.. doxygenfile:: clip/clip.hpp
   :project: libYSE
