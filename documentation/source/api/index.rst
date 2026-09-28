API Reference
=============

The libYSE public C++ API lives in the ``YSE`` namespace and is reached
through a single header:

.. code-block:: cpp

   #include "yse.hpp"

The pages below are generated from the source code by Doxygen + Breathe.

.. toctree::
   :maxdepth: 1
   :caption: Core

   core
   channels
   sounds
   devices
   midi

.. toctree::
   :maxdepth: 1
   :caption: Music

   music
   player
   clips

.. toctree::
   :maxdepth: 1
   :caption: Synthesis

   synth

.. toctree::
   :maxdepth: 1
   :caption: DSP

   dsp
   dsp_modules
   effects

The patcher has its own section. Its C++ reference is
:doc:`/patcher/api`, and every object type is listed in
:doc:`/patcher/objects/index`.

.. toctree::
   :maxdepth: 1
   :caption: Utilities

   types
   utils

Language bindings use the flat C API. Its page opens with a
:ref:`guide <c-api-guide>` to the rules every C function follows: handles
and ownership, error codes and the last error, strings, and which thread each
callback runs on.

.. toctree::
   :maxdepth: 1
   :caption: Language bindings

   c_api
