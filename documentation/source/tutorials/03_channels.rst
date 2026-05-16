Channels
========

Goal: group sounds into channels for shared volume control, and build a
custom channel hierarchy.

Walks through ``Demo04_Channels``: attach sounds to a pre-built channel
(``ChannelMusic``) and a custom-created child of ``ChannelMaster``, then
ride the gain of each independently.

Source: `Demo04_Channels.cpp
<https://github.com/yvanvds/yse-soundengine/blob/master/Demo.Windows.Native/Demo04_Channels.cpp>`_.

Why channels
------------

Every sound is attached to a channel — either implicitly to ``MainMix``
(equivalent to ``ChannelMaster()``) when no channel is supplied to
``sound::create``, or explicitly to one of the pre-built channels or to a
channel you create yourself. Channels do two things:

- **Shared mixing.** Set the volume on a channel and every sound inside
  it shifts together. No need to walk a list of sounds.
- **Threading.** Each channel renders its DSP on its own thread, so
  spreading sounds across several channels also spreads them across
  cores. Too few channels under-uses cores; too many spends time on
  thread overhead — the sweet spot is one channel per logical category
  (music, ambient, voice, GUI, SFX).

Creating a custom channel
-------------------------

The demo allocates a custom channel on the heap, parents it under
``ChannelMaster``, and attaches a sound to it:

.. literalinclude:: ../../../Demo.Windows.Native/Demo04_Channels.cpp
   :language: cpp
   :lines: 23-33

A few things are happening here:

- ``new YSE::channel`` — channels are dynamically allocated so they can be
  reparented and deleted without invalidating sound references.
- ``customChannel->create("myChannel", YSE::ChannelMaster())`` initialises
  the channel and attaches it under ``ChannelMaster``. The name is for
  logging.
- ``kick.create("...", customChannel, true)`` attaches the sound to the
  custom channel at load time. The ``true`` is the loop flag (see
  :doc:`02_sound_properties`).
- ``pulse.create("...", &YSE::ChannelMusic(), true)`` shows the other
  pattern: attach to a pre-built channel by passing its address.

Adjusting channel volume
------------------------

Volume is per-channel, multiplied through the tree:

.. literalinclude:: ../../../Demo.Windows.Native/Demo04_Channels.cpp
   :language: cpp
   :lines: 58-85

Notice the read-modify-write idiom: ``getVolume() + 0.1f``. Range is
[0, 1] — values above 1 clip the digital signal.

Deleting a channel
------------------

Custom channels can be torn down cleanly. Sounds and subchannels move up
to the parent:

.. literalinclude:: ../../../Demo.Windows.Native/Demo04_Channels.cpp
   :language: cpp
   :lines: 88-95

The destructor of ``DemoChannels`` also calls ``CustomDelete()`` — clean
up before ``YSE::System().close()`` runs.

What you learned
----------------

- The pre-built channels (``ChannelMaster``, ``ChannelMusic``,
  ``ChannelAmbient``, ``ChannelVoice``, ``ChannelGui``, ``ChannelFX``) cover
  the common categories.
- Build your own channel with ``new YSE::channel`` + ``create(name, parent)``.
- ``setVolume`` / ``getVolume`` work at every level; values cascade through
  the tree.
- Deleting a custom channel reparents its sounds and subchannels to the
  parent automatically.

Next
----

- :doc:`04_reverb` — add a positioned reverb effect to a channel.
- :cpp:class:`YSE::channel` — full channel API.
