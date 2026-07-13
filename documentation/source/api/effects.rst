Mixing effects
==============

Mix-grade effect modules for building channel strips and effect returns.
Each is a :cpp:class:`YSE::DSP::dspObject` subclass, so it chains with
``dspObject::link`` and mixes wet/dry through the inherited ``impact``.

Routing them is a channel-level concern:

- **Inserts** — attach a (chained) module to a channel's pre-fader insert
  slot with :cpp:func:`YSE::channel::setDSP`. Every sound on the channel is
  processed through the chain before the fader.
- **Sends and returns** — create a return bus with
  :cpp:func:`YSE::channel::makeReturn`, attach an effect to it with
  ``setDSP``, then feed it from any channel with
  :cpp:func:`YSE::channel::send` / :cpp:func:`YSE::channel::setSendLevel`.

Both are documented on the :doc:`channels` page. The
:doc:`/tutorials/09_mixing_inserts_sends` tutorial walks through a complete
console mixer built from these pieces.

Parametric EQ
-------------

.. doxygenfile:: dsp/modules/parametricEQ.hpp
   :project: libYSE

Compressor
----------

.. doxygenfile:: dsp/modules/compressor.hpp
   :project: libYSE

Chorus / flanger
----------------

.. doxygenfile:: dsp/modules/chorus.hpp
   :project: libYSE

Feedback delay
--------------

.. doxygenfile:: dsp/modules/delay/feedbackDelay.hpp
   :project: libYSE

Plate reverb
------------

A Dattorro-style plate reverb, well suited to a send/return bus.

.. doxygenfile:: dsp/modules/plateReverb.hpp
   :project: libYSE
