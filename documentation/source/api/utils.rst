Utilities
=========

``utils/misc.hpp`` contains the engine's random number functions
(``Random``, ``RandomF``, ``BigRandom``). They use a per-thread xorshift128+
generator and are safe on any thread, the audio thread included. Without a
seed the sequence is the same on every run. Call ``Randomize()`` once at
startup for a time-based seed, or ``RandomSeed(n)`` for a fixed one. A host
that links libYSE as a shared library seeds the engine's generator through
the C API (``yse_randomize``, ``yse_random_seed``), because these inline
functions would seed only the host's own copy. See
:doc:`/intro/sessions_and_devices` for starting the engine itself.

.. doxygenfile:: utils/misc.hpp
   :project: libYSE

.. doxygenfile:: utils/vector.hpp
   :project: libYSE

.. doxygenfile:: utils/interpolators.hpp
   :project: libYSE

.. doxygenfile:: utils/fileFunctions.hpp
   :project: libYSE
