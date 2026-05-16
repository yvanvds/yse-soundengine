Mental model
============

Five concepts are enough to think clearly about libYSE.

System
------

:cpp:class:`YSE::system` is the engine itself — a singleton you access through
the free function ``YSE::System()``. It owns the audio device, the DSP
threads, and the scheduler that turns ``play()`` calls into samples on the
sound card.

The lifecycle is:

1. ``YSE::System().init()`` once at startup.
2. ``YSE::System().update()`` once per frame.
3. ``YSE::System().close()`` once at shutdown.

Between init and close, the engine is alive and accepting commands.

Listener
--------

:cpp:class:`YSE::listener` is a singleton representing the position and
orientation of the "ear" in the virtual scene. Update its position every
frame; the engine computes panning, distance attenuation, and doppler shift
relative to it.

.. code-block:: cpp

    YSE::Listener().pos(YSE::Pos(playerX, playerY, playerZ));
    YSE::Listener().orient(facingDirection, upVector);

Sound
-----

:cpp:class:`YSE::sound` is a playable instance — one ``sound`` object per
voice you want in the scene. The source can be a file, an in-memory buffer,
a custom DSP source, or a patcher graph. Sounds can move, loop, fade,
stop, and seek; the engine reuses buffers under the hood when the same
file is loaded into multiple sounds.

Channel
-------

:cpp:class:`YSE::channel` is a node in a mixing tree. Every sound is attached
to a channel; channels can themselves be attached to a parent channel,
forming a tree rooted at ``MainMix``. Each channel renders on its own DSP
thread, so spreading sounds across channels also spreads them across cores.

A small set of pre-built channels (``ChannelMaster``, ``ChannelMusic``,
``ChannelAmbient``, ``ChannelVoice``, ``ChannelGui``, ``ChannelFX``) is
created for you. Use them as-is or as roots for your own subtrees.

A typical scene looks like::

    ChannelMaster (root, all output flows here)
    ├── ChannelMusic       ← background tracks
    ├── ChannelAmbient     ← environment loops
    ├── ChannelVoice       ← dialogue
    ├── ChannelGui         ← UI feedback
    └── ChannelFX          ← short SFX

Reverb
------

:cpp:class:`YSE::reverb` is a positioned reverb zone — a sphere in the scene
that lends its parameters to any nearby listener. Multiple reverbs can
overlap; the engine blends them by proximity so the listener transitions
smoothly between cave, hall, and bathroom as they walk.

A "global" reverb (``YSE::System().getGlobalReverb()``) acts as the fallback
everywhere no positioned zone reaches.

Putting it together
-------------------

The interaction looks like this on every frame:

1. The application calls ``System().update()`` to advance engine state.
2. The application updates the ``Listener`` position to wherever the player /
   camera is.
3. Sounds attached to channels render through the channel tree.
4. Positioned reverbs nearest the listener are blended into the output.

That is the whole picture. Everything else — DSP processors, patchers, the
note ``player`` — is built on top of these five concepts.
