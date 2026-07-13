Mixing with inserts and sends
=============================

Goal: build a console-style mixer — a channel with an insert effect chain,
and a shared reverb fed by aux sends.

:doc:`03_channels` introduced the channel tree for grouping sounds under
shared volume. Channels also carry the two routing primitives every mixer
needs:

- an **insert chain** — a pre-fader :cpp:class:`YSE::DSP::dspObject` chain
  that processes everything on the channel (:cpp:func:`YSE::channel::setDSP`);
- **aux sends** to a **return bus** — a scaled copy of the channel routed to
  a shared effect (:cpp:func:`YSE::channel::makeReturn`,
  :cpp:func:`YSE::channel::send`).

This tutorial walks through ``Demo21_Mixer``: a music channel running
EQ → compressor → chorus, plus a plate reverb both channels send into.

Source: `Demo21_Mixer.cpp
<https://github.com/yvanvds/yse-soundengine/blob/master/Demo.Windows.Native/Demo21_Mixer.cpp>`_.

Two source channels
-------------------

.. literalinclude:: ../../../Demo.Windows.Native/Demo21_Mixer.cpp
   :language: cpp
   :lines: 20-21

Building an insert chain
------------------------

Configure the effect modules, chain them with ``dspObject::link``, then
attach the head of the chain to the channel. The whole chain runs pre-fader
on the summed channel signal:

.. literalinclude:: ../../../Demo.Windows.Native/Demo21_Mixer.cpp
   :language: cpp
   :lines: 23-31

Each module is an ordinary :cpp:class:`YSE::DSP::dspObject`, so it has the
inherited ``bypass`` / ``impact`` (wet/dry) controls on top of its own
parameters. The mix-grade modules — :cpp:class:`parametric EQ
<YSE::DSP::MODULES::parametricEQ>`,
:cpp:class:`compressor <YSE::DSP::MODULES::compressor>`,
:cpp:class:`chorus <YSE::DSP::MODULES::chorus>` — are documented on the
:doc:`/api/effects` page.

A shared reverb via send / return
---------------------------------

Create a return bus with ``makeReturn``, attach an effect to it, then feed it
from any channel with ``send(slot, returnBus, level)``. Here both the music
and drum channels send into one plate reverb, post-fader:

.. literalinclude:: ../../../Demo.Windows.Native/Demo21_Mixer.cpp
   :language: cpp
   :lines: 33-38

A return bus is just a channel that has been flagged with ``makeReturn`` — it
keeps its own inserts and can even send onward to other returns (the graph
must stay acyclic).

Driving it live
---------------

Toggling an insert chain and riding the send levels is glitch-free — send
levels are ramped internally, so they are safe to set every control tick:

.. literalinclude:: ../../../Demo.Windows.Native/Demo21_Mixer.cpp
   :language: cpp
   :lines: 83-98

Passing ``nullptr`` to ``setDSP`` detaches the insert chain; the engine takes
no ownership of the modules, so they must outlive the channel.

Tearing down routing
--------------------

Because the audio thread walks the insert chain and sends every block, tear
the routing down — ``clearSend`` and ``setDSP(nullptr)`` — *before* the
channels and effect objects destruct:

.. literalinclude:: ../../../Demo.Windows.Native/Demo21_Mixer.cpp
   :language: cpp
   :lines: 59-68

What you learned
----------------

- ``channel::setDSP(head)`` installs a pre-fader insert chain; chain modules
  with ``dspObject::link``.
- ``channel::makeReturn`` turns a channel into a return bus;
  ``channel::send`` / ``setSendLevel`` feed it from other channels.
- The engine never owns insert or return effects — they must outlive the
  channel, and routing should be torn down before they destruct.

Next
----

- :doc:`/api/effects` — the mixing-effect module reference.
- :doc:`/api/channels` — channel routing, sends and metering.
- :doc:`10_per_note_3d` — spatial positioning per note.
