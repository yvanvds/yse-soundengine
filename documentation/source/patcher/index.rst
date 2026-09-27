Patcher
=======

A :cpp:class:`YSE::patcher` is a graph of small objects wired together by
cords, in the style of Max and Pure Data. Each object has **inlets** that take
input and **outlets** that send output. ``Connect(from, outlet, to, inlet)``
draws a cord from one outlet to one inlet. The engine currently registers 305
object types: oscillators, filters, math, routing, lists, timing, data stores,
MIDI, GUI controls and more. :doc:`objects/index` lists every one of them.

Use a patcher when a sound is not a file to play but a network to evaluate:
procedural synthesis, parameter mapping, generative sequencing, or an effect
patched by hand. You build the graph in code (C++ or the C API), or load it
from a JSON patch file with ``ParseJSON``. You can edit it while it plays.

Signal objects and control objects
----------------------------------

The first character of an object's type name tells you which kind it is.

``~`` **signal objects** (``~sine``, ``~lp``, ``~*``, ``~dac`` …)
   Signal objects run once per audio block and pass whole buffers of samples
   along their cords. There are 17 of them. Many of their inlets also accept a
   plain number: the inlet of ``~sine`` takes either an audio buffer (for FM)
   or a float frequency.

``.`` **control objects** (``.r``, ``.mtof``, ``.metro``, ``.coll`` …)
   Control objects only do something when a message arrives, such as a bang,
   a number or a list. The object handles it and may send new messages from
   its outlets. All other objects are control objects, 287 of them.

``patcher`` (no prefix)
   A subpatcher: a box that groups part of a graph behind its own inlets and
   outlets. See :doc:`subpatchers`.

A graph usually mixes both kinds. Control objects compute *when* and *what*,
and signal objects make the sound:

.. code-block:: cpp

   YSE::patcher patch;
   patch.create(2);  // two output channels

   YSE::pHandle* note = patch.CreateObject(YSE::OBJ::G_RECEIVE, "note");  // ".r note"
   YSE::pHandle* mtof = patch.CreateObject(".mtof");
   YSE::pHandle* osc  = patch.CreateObject(YSE::OBJ::D_SINE, "440");     // "~sine 440"
   YSE::pHandle* dac  = patch.CreateObject(YSE::OBJ::D_DAC);             // "~dac"

   patch.Connect(note, 0, mtof, 0);
   patch.Connect(mtof, 0, osc, 0);   // a float sets the oscillator frequency
   patch.Connect(osc, 0, dac, 0);    // left
   patch.Connect(osc, 0, dac, 1);    // right

   patch.PassData(64.f, "note");     // the host talks to the ".r note" object

Type names can be passed as literal strings (``"~sine"``) or as the matching
``YSE::OBJ`` constant (``YSE::OBJ::D_SINE``). Both do the same thing.

Audio enters and leaves the graph through two objects. They have one pin per
channel, and the number of channels is the count you pass to
``patcher::create``:

- ``~dac`` is the graph's audio **output**. Each inlet is one output channel.
  A patch is silent until something reaches a ``~dac``.
- ``~adc`` is the graph's audio **input**. Each outlet carries one channel of
  the host's incoming audio. It only has something to carry when the patcher
  runs as an insert (see below).

Hosting a patcher
-----------------

The patcher does not render on its own. The audio thread renders it one block
at a time, and it only does so once the patcher is attached to something.
There are two ways to attach it.

Messages the host sends with ``PassData`` / ``PassBang`` go into a queue.
They reach the graph at the start of the next block it renders, so a patcher
that is not attached does not process them.

As a sound source
~~~~~~~~~~~~~~~~~

Give the patcher to ``sound::create``, and the ``~dac`` output becomes the
source of that sound. It can then be positioned, faded and routed like any
other :cpp:class:`YSE::sound`:

.. code-block:: cpp

   YSE::sound voice;
   voice.create(patch, &YSE::ChannelMaster());  // channel and volume are optional
   voice.play();

The rules:

- Call ``patcher::create`` before handing the patcher to a sound.
- The patcher must outlive the sound.
- A patcher can drive only one sound. A second ``sound::create`` with the
  same patcher is refused and logged.
- ``~adc`` objects stay silent in this mode because there is no incoming
  audio.

In the C API these are ``yse_patcher_create`` + ``yse_patcher_init`` and
``yse_sound_load_patcher``.

As an insert effect
~~~~~~~~~~~~~~~~~~~

:cpp:class:`YSE::DSP::patcherInsert` wraps a patcher as a
:cpp:class:`YSE::DSP::dspObject`. You can attach it anywhere an insert can
go: ``sound::setDSP`` for one sound, or ``channel::setDSP`` for everything
mixed into a channel. For every block, the insert:

1. feeds the host's audio to the ``~adc`` objects,
2. renders the graph,
3. writes the ``~dac`` output back over the host's audio.

This makes a hand-patched filter, delay or EQ usable as an effect:

.. code-block:: cpp

   YSE::patcher fx;
   fx.create(2);
   YSE::pHandle* in  = fx.CreateObject(YSE::OBJ::D_ADC);
   YSE::pHandle* lpL = fx.CreateObject("~lp", "800");
   YSE::pHandle* lpR = fx.CreateObject("~lp", "800");
   YSE::pHandle* out = fx.CreateObject(YSE::OBJ::D_DAC);
   fx.Connect(in, 0, lpL, 0);
   fx.Connect(in, 1, lpR, 0);
   fx.Connect(lpL, 0, out, 0);
   fx.Connect(lpR, 0, out, 1);

   YSE::DSP::patcherInsert insert(fx);  // borrows fx, does not own it
   YSE::ChannelFX().setDSP(&insert);

Build the graph before you attach the insert. Keep both the patcher and the
insert alive for as long as the insert is attached.

When the host buffer and the graph have different channel counts:

- ``~adc`` channels beyond the host's channel count get no input.
- Host channels beyond the graph's output count pass through unchanged
  (dry).

In the C API, create the insert with ``yse_dsp_patcher_insert_create`` and
attach it with ``yse_sound_set_dsp`` or ``yse_channel_set_dsp``.

Where to go next
----------------

Working with patches:

- :doc:`messages`: message types, hot and cold inlets, the order in which
  messages travel, logical events, and selector matching.
- :doc:`building`: creating, connecting and editing objects while the
  graph plays, and object IDs.
- :doc:`subpatchers`: grouping part of a graph behind its own inlets and
  outlets.
- :doc:`time`: delays, metronomes, ramps, tempo and clocks, and how
  accurate each one is.

Connecting a patch to the outside world:

- :doc:`host_io`: send/receive, the named bus, ``.print`` and ``.value``.
- :doc:`data`: collections, dictionaries and arrays, and sharing them by
  name.
- :doc:`files`: reading and writing files from a patch.
- :doc:`midi`: MIDI input, output and note handling.
- :doc:`gui`: the GUI value protocol, controls and presets.

Reference:

- :doc:`objects/index`: every object type, with its inlets, outlets and
  arguments.
- :doc:`file_format`: the JSON patch format that ``DumpJSON`` writes and
  ``ParseJSON`` reads.
- :doc:`realtime`: what runs on the audio thread and how edits stay
  glitch-free.
- :doc:`c_api`: embedding a patcher through the C API.
- :doc:`api`: the C++ classes, generated from the headers.
- :doc:`extending`: writing a new patcher object (for contributors).

For a hands-on start, see the :doc:`/tutorials/05_patcher` tutorial.

.. toctree::
   :hidden:
   :caption: Working with patches

   messages
   building
   subpatchers
   time

.. toctree::
   :hidden:
   :caption: Connecting to the outside

   host_io
   data
   files
   midi
   gui

.. toctree::
   :hidden:
   :caption: Reference

   objects/index
   file_format
   realtime
   c_api
   api
   extending
