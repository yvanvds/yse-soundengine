Core
====

System
------

``YSE::System()`` starts and ends the engine session, opens audio devices and
sets the sample rate. :doc:`/intro/sessions_and_devices` explains the session
lifecycle and device handling, and :doc:`/intro/threading` says which calls
belong on the control thread.

.. doxygenfile:: system.hpp
   :project: libYSE

Listener
--------

.. doxygenfile:: listener.hpp
   :project: libYSE

Logging
-------

By default the engine writes its log to a file in the working directory.
``YSE::Log().setHandler()`` sends the lines to your own ``logHandler``
instead. Engine threads call the handler one line at a time, and
:doc:`/intro/threading` lists the rules a handler must follow.

.. doxygenfile:: log.hpp
   :project: libYSE

I/O
---

.. doxygenfile:: io.hpp
   :project: libYSE

.. doxygenfile:: BufferIO.hpp
   :project: libYSE

Reverb
------

.. doxygenfile:: reverb/reverb.hpp
   :project: libYSE

.. doxygenfile:: reverb/reverbInterface.hpp
   :project: libYSE
