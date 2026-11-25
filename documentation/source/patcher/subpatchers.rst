Subpatchers
===========

A subpatcher groups part of a patch into one object. The parent patch
connects to the group by pin number, the way it connects to any other object,
and does not need to know what is inside. Use subpatchers to keep a large
patch readable and to build a piece once (a voice, a filter bank, an
envelope) and create it again wherever it is needed.

.. versionadded:: 3.0
   Subpatchers (issue `#545
   <https://github.com/yvanvds/yse-soundengine/issues/545>`_), and signal pins
   on their boundary (issue `#764
   <https://github.com/yvanvds/yse-soundengine/issues/764>`_).

The objects
-----------

Five object types are involved. :doc:`objects/subpatchers` has their full
reference.

.. list-table::
   :header-rows: 1
   :widths: 16 22 62

   * - Type
     - ``YSE::OBJ`` constant
     - What it is
   * - ``patcher``
     - ``PATCHER``
     - The subpatcher itself. It has no inlets or outlets of its own. Its one
       argument is a free-text **name** for a host to show as its title.
   * - ``.inlet``
     - ``G_INLET``
     - A message inlet of the subpatcher it is in. Bang, int, float and list
       pass through it unchanged.
   * - ``.outlet``
     - ``G_OUTLET``
     - A message outlet of the subpatcher it is in.
   * - ``~inlet``
     - ``D_INLET``
     - A signal inlet of the subpatcher it is in. Only audio passes; a message
       arriving on it is dropped, as on any signal inlet.
   * - ``~outlet``
     - ``D_OUTLET``
     - A signal outlet of the subpatcher it is in.

The four boundary objects take one argument, their **index**, which defaults
to 0. A subpatcher's inlet N is the ``.inlet`` or ``~inlet`` inside it with
index N. Outlet N is the ``.outlet`` or ``~outlet`` with index N. Messages and
signals share one numbering per side, so a ``.inlet 0`` next to a
``~inlet 1`` gives the subpatcher two inlets: a message inlet 0 and a signal
inlet 1.

Two boundary objects in one subpatcher that claim the same index are not an
error, but only one of them is used and which one is not defined. Give every
boundary object its own number.

.. versionchanged:: 3.0
   ``YSE::OBJ::D_OUT`` (``"~out"``) was removed (issue `#436
   <https://github.com/yvanvds/yse-soundengine/issues/436>`_). No object was
   ever registered under that name, so ``CreateObject`` always returned
   ``nullptr`` for it. Use ``~outlet`` for a signal that leaves a subpatcher
   and ``~dac`` for the audio output of the whole patch.

Putting objects inside
----------------------

A subpatcher is not a separate :cpp:class:`YSE::patcher`. You create its
contents with the same ``CreateObject`` on the same patcher, then place them
inside with ``SetContainer(object, subpatcher)``:

- ``SetContainer(obj, nullptr)`` moves an object back to the top level.
- ``GetContainer(obj)`` returns the subpatcher an object is directly inside,
  or ``nullptr`` at the top level.
- Subpatchers nest: pass a ``patcher`` object to ``SetContainer`` like any
  other object.

``SetContainer`` refuses the call, writes a log line and changes nothing when
the container is not a ``patcher`` (or ``~pfft`` or ``~poly``, see below)
object of this patcher, when the object
belongs to another patcher, or when the move would put a subpatcher inside
itself or inside one of its own subpatchers.

Moving an object changes no cords. Cords already drawn to or from it stay
where they are. Draw the cords after you have placed the objects, so that the
subpatcher's pin numbers resolve (see below).

Connecting to a subpatcher
--------------------------

Pass the subpatcher to ``Connect`` and ``Disconnect`` with a pin number, as
for any object. The patcher looks up the boundary object that claims that
number and draws the cord to it directly:

- ``Connect(source, 0, sub, 2)`` draws a cord from ``source`` to inlet 0 of
  the ``.inlet`` or ``~inlet`` with index 2 inside ``sub``.
- ``Connect(sub, 1, target, 0)`` draws a cord from outlet 0 of the
  ``.outlet`` or ``~outlet`` with index 1 inside ``sub``.
- When no boundary object in the subpatcher claims the number, the cord is
  refused with a log line, like any other invalid connection
  (:doc:`building`).

Only the subpatcher's **direct** contents count. A ``.inlet`` inside a nested
subpatcher is a pin of that nested subpatcher, not of the outer one.

After ``Connect`` returns, the cord is an ordinary cord between two ordinary
objects. That has some consequences:

- When you walk the graph, a cord drawn to a subpatcher reports the ID of the
  boundary object, not of the ``patcher`` object. ``DumpJSON`` saves it the
  same way (see :doc:`file_format`).
- Cords are attached to the boundary *object*, not to its index. Changing a
  boundary object's index later does not move cords that already exist.
- The patcher does not check that a cord respects the boundary. A cord drawn
  straight from an object inside a subpatcher to one outside it is accepted
  and works, but the parent cannot see it through the subpatcher's pins.
  Route through ``.outlet`` / ``~outlet`` to keep the group self-contained.

Crossing a boundary costs one pass-through object per boundary. A signal is
passed on by pointer, without a copy. The patcher stores every object in one
flat graph, whatever the nesting depth, so the audio thread's work does not
grow with nesting depth. The only subpatcher state the audio thread reads is
whether it is muted (see `Muting a subpatcher`_).

Reading a subpatcher's shape
----------------------------

A ``patcher`` object has no pins of its own, so ``pHandle::GetInputs()`` and
``GetOutputs()`` return 0 for it. Ask the patcher instead:

``SubpatcherInlets(sub)`` / ``SubpatcherOutlets(sub)``
   One more than the highest index claimed on that side, messages and
   signals together. A subpatcher whose only ``.inlet`` has index 2 reports 3
   inlets, and inlets 0 and 1 refuse a connection. For a handle that is not a
   subpatcher, the answer is 0.

The per-pin type queries on a subpatcher's handle resolve through the
boundary, on both sides:

- ``pHandle::IsDSPInput(n)`` answers for the boundary object behind inlet n
  (true for a ``~inlet``), and ``SetBang``, ``SetIntData``, ``SetFloatData``
  and ``SetListData`` deliver to it. A host can push a value into a
  subpatcher's inlet without knowing what is inside. For a pin no boundary
  object claims, ``IsDSPInput`` returns false and the setters do nothing.
- ``pHandle::OutputDataType(n)`` answers for the boundary object behind
  outlet n: ``OUT_TYPE::BUFFER`` for a ``~outlet``, ``OUT_TYPE::ANY`` for a
  ``.outlet``. For a pin no boundary object claims, it returns
  ``OUT_TYPE::INVALID``.

So an editor can tell a signal cord from a message cord at a subpatcher's
pins the same way it does at any other object's.

Worked example: a voice
-----------------------

This voice takes a MIDI note on inlet 0 and a level from 0 to 1 on inlet 1,
and sends its signal out of outlet 0. The code that builds it is a function,
so a patch can create as many voices as it needs:

.. code-block:: cpp

   //  inlet 0 (note)        inlet 1 (level)
   //    .inlet 0              .inlet 1
   //       |                     |
   //     .mtof                   |
   //       |                     |
   //     ~sine                   |
   //       |                     |
   //      ~* 0  <---------------+   (right inlet: gain)
   //       |
   //   ~outlet 0
   //  outlet 0 (signal)

   YSE::pHandle* CreateVoice(YSE::patcher& patch) {
     YSE::pHandle* voice = patch.CreateObject(YSE::OBJ::PATCHER);
     YSE::pHandle* pitch = patch.CreateObject(YSE::OBJ::G_INLET, "0");
     YSE::pHandle* level = patch.CreateObject(YSE::OBJ::G_INLET, "1");
     YSE::pHandle* mtof  = patch.CreateObject(".mtof");
     YSE::pHandle* osc   = patch.CreateObject("~sine");
     YSE::pHandle* amp   = patch.CreateObject("~*", "0");  // silent until a level arrives
     YSE::pHandle* out   = patch.CreateObject(YSE::OBJ::D_OUTLET, "0");

     for (YSE::pHandle* h : {pitch, level, mtof, osc, amp, out}) {
       patch.SetContainer(h, voice);
     }

     patch.Connect(pitch, 0, mtof, 0);
     patch.Connect(mtof, 0, osc, 0);
     patch.Connect(osc, 0, amp, 0);
     patch.Connect(level, 0, amp, 1);
     patch.Connect(amp, 0, out, 0);
     return voice;
   }

The parent patch wires the voice by its pin numbers only:

.. code-block:: cpp

   YSE::patcher patch;
   patch.create(2);

   YSE::pHandle* voice = CreateVoice(patch);
   YSE::pHandle* note  = patch.CreateObject(".r", "note");
   YSE::pHandle* level = patch.CreateObject(".r", "level");
   YSE::pHandle* dac   = patch.CreateObject(YSE::OBJ::D_DAC);

   patch.Connect(note, 0, voice, 0);
   patch.Connect(level, 0, voice, 1);
   patch.Connect(voice, 0, dac, 0);  // left
   patch.Connect(voice, 0, dac, 1);  // right

   patch.SubpatcherInlets(voice);    // 2
   patch.SubpatcherOutlets(voice);   // 1
   voice->GetInputs();               // 0: the pins belong to the boundary objects

   // Once the patcher is attached to a sound or an insert:
   patch.PassData(69.f, "note");     // through .r note into inlet 0: A4
   patch.PassData(0.5f, "level");    // into inlet 1: half level

   // The host can also skip the .r and push into the voice's pins directly:
   voice->SetFloatData(0, 72.f);

To build a bigger instrument, put voices inside another subpatcher. Call
``patch.SetContainer(voice, synth)`` where ``synth`` is a ``patcher`` object
with its own ``.inlet`` and ``~outlet`` objects, and wire those to the voice
inside it. The voice's pins work the same at any depth.

``DeleteObject(voice)`` removes the voice and everything inside it, including
nested subpatchers and their contents. Every object in the group gets the
same last chance to clean up that a single deleted object gets
(:doc:`building`).

Muting a subpatcher
-------------------

``~mute`` switches a subpatcher's signal processing off to save CPU, and on
again. Connect its outlet to any inlet of the subpatcher, by pin number as
for any other cord. One ``~mute`` can control as many subpatchers as it is
connected to.

- A number: non-zero mutes the signal objects directly inside the
  subpatcher, 0 unmutes them. Nested subpatchers keep running.
- A list ``<mute> <nested>``: the same, and with ``<nested>`` non-zero the
  signal objects in its nested subpatchers too, at any depth. ``1 1`` mutes
  everything inside and ``0 1`` unmutes everything inside. The two settings
  are separate: after ``1 1``, a plain ``0`` unmutes the direct contents and
  leaves the nested ones muted.

A muted signal object does not compute, and its signal outlets carry
silence, so the subpatcher's ``~outlet`` objects output silence rather than
its last block, and whatever the subpatcher feeds outside keeps running. An
oscillator inside resumes from where it stopped when it is unmuted. A
``~dac`` inside a muted subpatcher adds nothing to the output. Messages,
control objects and MIDI input inside keep working.

Nothing travels down the ``~mute`` cord, so the boundary object behind that
pin never sees the mute. A nested subpatcher can have a ``~mute`` of its own;
the two do not undo each other.

The mute is not saved: a loaded patch starts unmuted, and so does a
subpatcher rebuilt by ``SetParams``.

.. code-block:: cpp

   YSE::pHandle* mute = patch.CreateObject("~mute");
   patch.Connect(mute, 0, voice, 0);  // any inlet of the voice
   mute->SetIntData(0, 1);            // the voice goes silent and stops computing
   mute->SetIntData(0, 0);            // and plays again

``~pass`` and ``~begin`` exist so patches that use them keep working.
``~pass`` passes its input on and, inside a muted subpatcher, outputs
silence like every other muted object. ``~begin`` outputs a constant 0. See
:doc:`objects/subpatchers`.

Editing a subpatcher while it plays
-----------------------------------

An edit inside a subpatcher is an ordinary edit. ``CreateObject``,
``Connect``, ``Disconnect``, ``DeleteObject`` and ``SetParams`` on objects
inside a subpatcher publish a new graph snapshot exactly as they do at the
top level, and the rules in :doc:`building` apply unchanged.
``SetContainer`` changes no cord, but it publishes too, because the render
needs to know which subpatcher an object is in to mute it (see below).

Adding a boundary object is also an ordinary edit. Create a ``.inlet 2``
inside a subpatcher and the subpatcher has an inlet 2 from then on. The
``patcher`` object does not change.

Renumbering a boundary object takes effect at once. ``SetParams`` on a
boundary object changes its index before the call returns, whether or not
the patcher is rendering. The next ``Connect``, ``Disconnect``, ``pHandle``
setter and ``SubpatcherInlets`` / ``SubpatcherOutlets`` call uses the new
number. Unlike other number arguments, the index does not wait for the next
block (see :doc:`building`), because only these calls read it. Cords that
already end at the object stay attached to it.

Loading and saving
------------------

``DumpJSON`` writes a subpatcher as an ordinary object record of type
``"patcher"``. Its contents are ordinary records that carry a ``container``
key with the subpatcher's ID. There is no nested document. The
``Subpatchers`` section of :doc:`file_format` describes the key, the order in
which ``ParseJSON`` restores it, and what it refuses. A nested patch loads
back into the same shape, and saving it again gives the same bytes.

On load, ``.loadbang`` and ``.loadmess`` inside a subpatcher fire in the
same pass as those at the top level, after the whole graph has been
published. There is no "inner patchers first" order. If one initialisation
has to happen before another, wire them through a ``.trigger``, which works
across a boundary like any other cord (see :doc:`messages`).

Names are not local
-------------------

A subpatcher does not have a name scope of its own. ``.s`` / ``.r`` and named
stores such as ``.value``, ``.coll`` and ``.array`` are scoped by the name of
the whole :cpp:class:`YSE::patcher`, whatever subpatcher they are in (see
:doc:`host_io`). If you create the same voice twice and each copy contains a
``.r freq``, both copies receive every ``freq`` message. There is no
per-instance prefix that makes a name local to one copy. To address one
copy, send to its inlets, or give each copy's names a different argument when you build it.

The same in C
-------------

The C API has one function for each C++ call on this page. Handles are
borrowed from the patcher, as elsewhere:

.. code-block:: c

   YsePHandle* voice = yse_patcher_create_object(p, "patcher", NULL);
   YsePHandle* pitch = yse_patcher_create_object(p, ".inlet", "0");
   YsePHandle* out   = yse_patcher_create_object(p, "~outlet", "0");
   /* ... the rest of the voice as in C++ ... */

   yse_patcher_set_container(p, pitch, voice);
   yse_patcher_set_container(p, out, voice);

   YsePHandle* note = yse_patcher_create_object(p, ".r", "note");
   yse_patcher_connect(p, note, 0, voice, 0);   /* lands on the .inlet */

   int ins  = yse_patcher_subpatcher_inlets(p, voice);
   int outs = yse_patcher_subpatcher_outlets(p, voice);
   YsePHandle* owner = yse_patcher_get_container(p, pitch);  /* voice */
   yse_phandle_set_float(voice, 0, 69.f);                     /* into inlet 0 */

``yse_patcher_set_container`` with ``NULL`` as the container moves an object
back to the top level. ``yse_phandle_get_inputs`` returns 0 for a subpatcher,
as in C++. :doc:`c_api` covers embedding a patcher through the C API.

Spectral subpatchers: ``~pfft``
-------------------------------

A ``~pfft`` is a container like ``patcher``, but its contents run on
short-time spectra instead of blocks of samples. Each of its signal inlets is
cut into overlapping frames, windowed and Fourier transformed; the objects
inside work on the bins; and what they put out is transformed back, windowed
and overlap-added to the ``~pfft``'s outlets. Inside, ``~fftin N`` puts out
the spectrum of inlet N (real part, imaginary part, bin index) and
``~fftout N`` takes the spectrum outlet N is made from. ``~fftinfo`` reports
the frame. Objects go in with ``SetContainer``, as for ``patcher``:

.. code-block:: cpp

   YSE::pHandle* fx  = patch.CreateObject("~pfft", "fx 1024 4");  // name size overlap
   YSE::pHandle* in  = patch.CreateObject("~fftin", "0");
   YSE::pHandle* out = patch.CreateObject("~fftout", "0");
   YSE::pHandle* re  = patch.CreateObject("~*", "0.5");
   YSE::pHandle* im  = patch.CreateObject("~*", "0.5");
   for (YSE::pHandle* h : {in, out, re, im}) patch.SetContainer(h, fx);
   patch.Connect(in, 0, re, 0);
   patch.Connect(in, 1, im, 0);
   patch.Connect(re, 0, out, 0);
   patch.Connect(im, 0, out, 1);
   patch.Connect(source, 0, fx, 0);   // the ~pfft's own signal pins
   patch.Connect(fx, 0, dac, 0);

The arguments are a name (free text, for a host to show), the FFT size (512),
the overlap (2), an offset in samples (0), the full-spectrum flag (0) and the
number of signal inlets (1) and outlets (1). The pins are the ``~pfft``'s
own: unlike a ``patcher``, it does not take its pins from the objects inside.

How the frames are scheduled
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A frame of *size* samples starts every *hop* = size / overlap samples. A
frame completes at a sample, not at a block boundary: with 128-sample blocks,
a hop of 32 completes four frames in every block and a hop of 512 one frame
every four blocks. The ``~pfft`` handles each frame at the exact sample it
completes, inside the block, so the output does not depend on the block size,
and the objects inside run once per frame, never once per block. A
``~pfft`` whose hop is larger than a block leaves its contents idle in the
blocks where no frame completes.

What the objects inside see
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A frame holds size / 2 bins, DC up to the one below Nyquist, or all *size*
bins with the full-spectrum flag. The upper half of a real signal's spectrum
mirrors the lower half and is filled in on the way back. The Nyquist bin is
not part of a half-spectrum frame and is resynthesised as 0, so an unchanged
half spectrum gives back the input without its Nyquist-frequency component;
with the full spectrum it gives back the input exactly.

The objects inside are ordinary signal objects working on blocks of at most
128 samples. A frame of more than 128 bins is rendered as several *passes*
of 128 bins each, in bin order, all at the sample the frame completes:
sample i of a pass is bin *first* + i, and ``~fftin``'s right outlet gives
the bin index, which is how an object treats bins differently (``~<`` on
the index and ``~*`` make a brick-wall filter). An object that keeps state
from sample to sample, such as a filter, runs across the bins of a frame in
order. Frame-domain objects read which bins a pass holds directly (see
:doc:`extending`).

Stages
~~~~~~

Some objects need the whole frame before they can put out any of it: a
smoothing centred on each bin (``~framesmooth``) or a pitch or frequency
shift down (``~gizmo``, ``~fbinshift``) reads bins above the one it puts out, which arrive in a later
pass. Such a *frame-wide* object splits
the subpatch into *stages*. Everything that feeds it, and the object itself
taking in the frame, is in one stage; what it feeds is in the next. The
``~pfft`` renders every pass of the first stage, then every pass of the
second, and so on, all at the sample the frame completes. Without a
frame-wide object a subpatch is one stage and nothing changes.

Each object is in the stage after the latest frame-wide object upstream of
it, so a chain of two frame-wide objects makes three stages. A ``~fftout``
is collected after each pass of its own stage. A cord that reaches over a
stage, such as the phase of a ``~cartopol`` going around a ``~framesmooth``
on the magnitudes straight into a ``~poltocar``, is carried: the ``~pfft``
keeps what the cord brought in the earlier stage, a frame of it, and hands
the ``~poltocar`` each pass's bins in its own stage. That costs one frame of
memory per such cord and nothing else, so a patch can be drawn as if every
object saw the whole frame at once.

Inside a ``~pfft``, a ``~dac`` or ``~adc`` does nothing, and ``~send`` /
``~receive`` pairs are not ordered.

Latency, windows and gain
~~~~~~~~~~~~~~~~~~~~~~~~~~

The output starts the sample after a frame's last input sample, so a
``~pfft`` delays its input by exactly *size* samples, whatever the block
size and the overlap. That is the least any frame-based process that is
exact to the sample can have, and the timing ``~fft`` and ``~ifft`` use.

``~fftin`` sets the analysis window of its inlet and ``~fftout`` the
synthesis window of its outlet: ``hanning`` (the default), ``hamming``,
``blackman``, ``triangle`` or ``square`` (no window). The overlap-add is
scaled so that an unchanged spectrum comes back at the input's level: each
output sample is divided by the sum of analysis window times synthesis
window over the frames that overlap there, and by the size. This holds for
any window pair at an overlap of 2 or more, including Hann on both sides at
the default overlap of 2. With no overlap (1) and a window that falls to 0 at
its edges, the sum reaches 0, and the average gain is used instead: the edges
of each frame come out quieter. An overlap of 4 is the usual choice for
processing that changes the spectrum.

Signal cords and the boundary
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A signal cord cannot cross a ``~pfft`` boundary: ``Connect`` refuses one with
a log line, and a cord that a later ``SetContainer`` puts across the
boundary carries nothing. Signals go in through the ``~pfft``'s inlets and
``~fftin``, and out through ``~fftout`` and the ``~pfft``'s outlets. Message
cords cross freely. A ``patcher`` inside a ``~pfft`` groups as usual: its
contents still run in the ``~pfft``'s frames.

The C++ engine underneath is ``YSE::DSP::stft`` (``dsp/spectral/stft.hpp``),
usable without a patcher: a frame callback gets each spectrum and may change
it in place.

Voices: ``~poly``
-----------------

A ``~poly`` is a container that plays several copies of what is inside it:
the voices of a polyphonic instrument, or N copies of an effect. What you put
inside is the definition of one voice and does not run itself; the
``~poly`` plays ``voices`` copies of it, each with its own state. Inside,
``.in`` and ``~in`` receive what arrives at the ``~poly``'s inlets, ``~out``
adds a voice's signal to an outlet, ``.out`` sends a message out, and
``~thispoly`` says whether the voice is busy.

.. code-block:: cpp

   // ~poly <name> <voices> <inlets> <signal outlets> <message outlets> <steal>
   YSE::pHandle* synth = patch.CreateObject("~poly", "synth 8 1 1");
   YSE::pHandle* in    = patch.CreateObject(".in", "0");      // inlet 0's messages
   YSE::pHandle* parse = patch.CreateObject(".unpack", "0 0");
   YSE::pHandle* pitch = patch.CreateObject(".mtof");
   YSE::pHandle* osc   = patch.CreateObject("~sine");
   YSE::pHandle* level = patch.CreateObject("./", "127.");
   YSE::pHandle* env   = patch.CreateObject("~adsr", "10 100 0.7 300");
   YSE::pHandle* amp   = patch.CreateObject("~*");
   YSE::pHandle* busy  = patch.CreateObject("~thispoly");
   YSE::pHandle* out   = patch.CreateObject("~out", "0");      // signal outlet 0
   for (YSE::pHandle* h : {in, parse, pitch, osc, level, env, amp, busy, out})
     patch.SetContainer(h, synth);
   patch.Connect(in, 0, parse, 0);       // "pitch velocity"
   patch.Connect(parse, 0, pitch, 0);
   patch.Connect(pitch, 0, osc, 0);
   patch.Connect(parse, 1, level, 0);
   patch.Connect(level, 0, env, 0);      // the velocity starts the envelope, 0 ends it
   patch.Connect(osc, 0, amp, 0);
   patch.Connect(env, 0, amp, 1);
   patch.Connect(env, 0, busy, 0);       // busy while the envelope sounds
   patch.Connect(amp, 0, out, 0);
   patch.Connect(synth, 0, dac, 0);

   synth->SetListData(0, "midinote 60 100");  // to a free voice
   synth->SetListData(0, "midinote 60 0");    // to the voice playing 60

How it runs
~~~~~~~~~~~

The patcher makes the copies itself, one per voice, and keeps them out of
the patch: they are not in ``Objects()``, have no handles, and are not
saved. Each voice renders in the ``~poly``'s place in the block, one after
the other, and the ``~out`` signals of all voices are added up. An edit
inside the ``~poly`` (an object created, deleted, connected or given new
arguments) rebuilds every voice from the new contents, as a load would, so a
voice's running state starts over. Changing the ``~poly``'s own arguments
rebuilds it like any object with pins from its arguments.

Messages are routed rather than broadcast. A message for a voice goes to the
*target* (``target N``; 0, the default, means every voice). ``note`` and
``midinote`` choose a voice themselves: the lowest one that is not busy, or,
with ``steal`` on, the one started longest ago. ``~thispoly`` makes its voice
busy from a number or from a signal such as an envelope, and a voice that
has just been given a note counts as busy until the end of the next block,
so a chord spreads over several voices. ``mute <voice> 1`` stops a voice
computing until ``mute <voice> 0``; it resumes where it stopped. The full
list of messages is in :doc:`objects/subpatchers`. The allocation policy is
``YSE::DSP::voiceAllocator``, which ``.poly`` uses too and C++ code can use
without a patcher.

The boundary
~~~~~~~~~~~~

No cord crosses a ``~poly``: ``Connect`` refuses one between an object inside
and one outside. Signals go in through the ``~poly``'s inlets and ``~in``,
and out through ``~out`` and its outlets; messages through ``.in`` and
``.out``. Inlet N serves both ``~in N`` and ``.in N``. The outlets are the
signal outlets first, then the message outlets, then one that sends the
number of the voice whose ``.out`` speaks, just before the message.

A ``patcher`` or a ``~pfft`` inside a voice is copied with it. A ``~poly``
cannot be put inside another ``~poly`` or a ``~pfft``. A ``~dac`` or
``~adc`` inside a voice does nothing. Names are not local: a ``.r`` inside
receives in every voice. A loaded patch fires each voice's ``.loadbang``.

Where to go next
----------------

- :doc:`building`: creating, connecting and deleting objects, and how edits
  are published.
- :doc:`file_format`: how subpatchers are saved.
- :doc:`objects/subpatchers`: the reference for ``patcher``, ``.inlet``,
  ``.outlet``, ``~inlet``, ``~outlet``, ``~mute``, ``~pass``, ``~begin``,
  ``~poly``, ``.in``, ``.out``, ``~in``, ``~out`` and ``~thispoly``.
- :doc:`objects/spectral`: the reference for ``~pfft``, ``~fftin``,
  ``~fftout`` and ``~fftinfo``.
- :doc:`host_io`: send/receive and the named bus, and how names are scoped.
- :doc:`realtime`: how the flat graph reaches the audio thread.
