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
forming a tree rooted at ``MainMix``. Child channels dispatch their DSP work
to a thread pool, so spreading sounds across channels lets the engine
parallelise mixing across cores.

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

Patcher
-------

:cpp:class:`YSE::patcher` is a Max/MSP-style modular DSP graph: a
collection of small objects (oscillators, filters, math, MIDI, GUI
controls) wired together by inlets and outlets. Use it when a sound is
not a file to play but a network to evaluate — procedural synthesis,
parameter mapping, generative MIDI.

.. code-block:: cpp

    YSE::patcher p;
    p.create();
    auto osc  = p.CreateObject(YSE::OBJ::D_SINE, "440");
    auto out  = p.CreateObject(YSE::OBJ::D_DAC);
    p.Connect(osc, 0, out, 0);
    sound.create(p, ChannelMaster);  // play the patcher as a sound source

Graphs can be serialised with ``DumpJSON()`` and rebuilt with
``ParseJSON()`` — pair the engine with an external editor or ship
presets as plain text.

The full set of registered object types — every inlet, outlet,
parameter, and accepted message type — is listed on the
:doc:`../api/patcher_objects` reference page, generated directly from
the engine source so it can never drift.

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
