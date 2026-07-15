Patcher
=======

.. doxygenfile:: patcher/patcher.hpp
   :project: libYSE

.. doxygenfile:: patcher/pHandle.hpp
   :project: libYSE

.. doxygenfile:: patcher/pObjectList.hpp
   :project: libYSE

Running a patcher as an insert
------------------------------

Wrap a built patcher graph in a ``DSP::patcherInsert`` to attach it anywhere
a ``DSP::dspObject`` chain goes — a channel or per-sound insert.

.. doxygenfile:: dsp/patcherInsert.hpp
   :project: libYSE

.. toctree::
   :maxdepth: 1
   :caption: Object reference

   patcher_objects
