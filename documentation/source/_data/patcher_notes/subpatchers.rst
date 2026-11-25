.. Notes for the Subpatchers category. Format: _data/patcher_notes/README.md.

.. patcher-object:: patcher

A ``patcher`` object has no inlets or outlets of its own. Create its contents
with ``CreateObject`` on the same patcher and move them inside with
``SetContainer``. Its pins are the boundary objects among its direct contents:
inlet N is the ``.inlet`` or ``~inlet`` with index N, and outlet N the
``.outlet`` or ``~outlet`` with index N. Connect to the subpatcher by pin
number, as to any object.

The one argument is the subpatcher's name, free text for a host to show as its
title: ``patcher gain`` or ``patcher lead synth``. The object keeps the text as
given and does nothing else with it, so a subpatcher is addressed by its
handle, never by this name. Renaming one with ``SetParams`` keeps its contents
and its cords.

Subpatchers nest, and nesting costs the audio thread nothing. Deleting a
subpatcher deletes everything inside it. On load, the contents fire their
``.loadbang`` objects together with the rest of the patch, in no set order.

Send and receive names are not local: a ``.r freq`` inside a subpatcher hears
every ``freq`` in the whole patcher. :doc:`/patcher/subpatchers` explains how
to build, connect, save and edit subpatchers.

.. patcher-object:: .inlet

The argument is the index: which inlet of the enclosing subpatcher this object
is (default 0). Messages and signals share one numbering per side, so
``.inlet 0`` and ``~inlet 1`` give a subpatcher two inlets. Two boundary
objects with the same index are not an error, but only one of them is used,
and which one is not defined.

A cord is attached to the boundary object, not to its number. Changing the
index later does not move cords that already exist. These index rules apply
to all four boundary objects; see :doc:`/patcher/subpatchers`.

Bang, int, float and list cross unchanged. A message box's text arrives as
the bang, number or list it spells, and a word command such as ``stop``
arrives as a list.

.. patcher-object:: .outlet

The index rules and what crosses are as for ``.inlet``.

.. patcher-object:: ~inlet

The index and numbering rules are those of ``.inlet``. The signal is passed on
without being copied, so crossing a boundary adds no cost that grows with the
block size or the nesting depth. When the parent connects nothing to this
inlet, nothing comes out.

.. patcher-object:: ~outlet

The index and numbering rules are those of ``.inlet``, and the signal is
passed on as by ``~inlet``.

.. patcher-object:: ~mute

Connect the outlet to any inlet of a subpatcher, by pin number. One
``~mute`` can control several subpatchers.

- A number: non-zero mutes the signal objects directly inside the
  subpatcher, 0 unmutes them. Nested subpatchers keep running.
- A list ``<mute> <nested>``: the same, and with ``<nested>`` non-zero the
  signal objects in nested subpatchers too, at any depth. ``1 1`` mutes
  everything inside, ``0 1`` unmutes everything inside. The two settings are
  separate, so after ``1 1`` a plain ``0`` leaves the nested ones muted.

A muted signal object does not compute, and its signal outlets carry silence,
so the subpatcher outputs silence and whatever it feeds keeps running. A
``~dac`` inside adds nothing. Messages and control objects inside keep
working. Nothing is delivered down the cord: the boundary object behind the
pin never sees the mute, and a cord to anything that is not a subpatcher's
inlet does nothing. The mute is not saved. See :doc:`/patcher/subpatchers`.

.. patcher-object:: ~pass

Put it before a ``~outlet``. Unmuted, the input passes through without a
copy. Inside a muted subpatcher it outputs silence, as every muted object
does, so a muted subpatcher is already silent without it.

.. patcher-object:: ~begin

Connected to a signal inlet, it adds 0 and changes nothing. It does not
switch anything off; to stop a part of a patch computing, put that part in a
subpatcher and use ``~mute``.

.. patcher-object:: ~poly

A container like ``patcher``: put objects inside it with ``SetContainer``.
What is inside is the definition of one voice and does not run itself; the
``~poly`` plays ``voices`` copies of it, each with its own state, and adds up
what their ``~out`` objects put out. An edit inside (an object created,
deleted, connected or given new arguments) rebuilds every voice from the new
contents; a voice that was muted stays muted.

The pins come from the arguments, not from the objects inside:

- ``inlets`` inlets. Inlet N carries a signal to every voice's ``~in N``
  (an unconnected inlet is silence) and messages to the ``.in N`` objects.
- ``outlets`` signal outlets: outlet N is the sum, over the voices, of their
  ``~out N`` objects.
- ``messages`` message outlets after them: what the ``.out N`` objects send.
- One last outlet: the number of the voice whose ``.out`` sends, sent just
  before the message.

Messages on any inlet are delivered to that inlet's ``.in`` objects:

- ``target <voice>``: the voice the messages that follow go to, from 1. 0
  means every voice (the default), a negative number none, a number above
  the voice count the last voice.
- ``note <message>``: the message to the first voice that is not busy, the
  lowest one. A voice is busy while its ``~thispoly`` says so, and from the
  moment it is given a note until the end of the next block, so a chord sent
  in one go spreads over several voices. With ``steal`` on and every voice
  busy, the voice that was started longest ago gets it; with ``steal`` off it
  is dropped.
- ``midinote <pitch> <velocity>``: a velocity above 0 goes where ``note``
  would, and that voice then holds the pitch; ``<pitch> 0`` goes to the voice
  holding it. Both arrive at the ``.in`` as the list ``pitch velocity``.
- ``allnotesoff``: ``<pitch> 0`` to every voice holding a ``midinote``, lowest
  voice first.
- ``mute <voice> <0|1>``: mutes or unmutes a voice; voice 0 is every voice. A
  muted voice does not compute and adds nothing to the outlets. Its
  ``~thispoly`` reports the change.
- ``steal <0|1>``: changes the ``steal`` argument.
- ``setvalue <voice> <message>``: the message to that voice (0: every voice)
  without changing the target.
- ``list <message>``: the message to the target as it is, so it can start
  with one of these words.

Anything else (a bang, a number, a list, another word) goes to the target.
Messages ``note``, ``setvalue`` and ``list`` pass on are at most 512
characters.

Cords cannot cross the boundary: ``Connect`` refuses a cord between an object
inside and one outside, including the ``~poly`` itself. A ``~poly`` cannot be
put inside another ``~poly`` or a ``~pfft``; a ``patcher`` or a ``~pfft``
inside a voice is copied with it. A ``~dac`` or ``~adc`` inside does nothing.
Names are not local: a ``.r`` inside receives in every voice. A loaded patch
fires each voice's ``.loadbang``; voices rebuilt by an edit do not. See
:doc:`/patcher/subpatchers`.

.. patcher-object:: .in

Outputs, in its voice, whatever the ``~poly`` passes that voice on inlet
``index``: a bang, a number or a list, as it arrived. Outside a ``~poly`` it
outputs nothing. Several ``.in`` objects may share an index.

.. patcher-object:: .out

Inside a ``~poly``, sends what arrives out of the ``~poly``'s message outlet
``index``, after the voice's number on its last outlet. Outside a ``~poly``,
or with an index the ``~poly`` has no message outlet for, it sends nothing.

.. patcher-object:: ~in

The ``~poly``'s signal inlet ``index``, the same buffer in every voice and
passed on without a copy. Outside a ``~poly``, or for an inlet the ``~poly``
does not have, it outputs silence.

.. patcher-object:: ~out

What arrives is added, over every voice that is not muted, to the
``~poly``'s signal outlet ``index``. Several ``~out`` objects with one index
add up too.

.. patcher-object:: ~thispoly

Says whether its voice is busy, which decides where ``note`` and
``midinote`` go:

- A number: non-zero busy, 0 free. ``busy <0|1>`` does the same.
- A signal: busy while any sample of the block is non-zero, set each block.
  An envelope makes a voice busy until it has faded out.
- ``mute <0|1>``: mutes or unmutes this voice, as the ``~poly``'s ``mute``
  does, and reports it.
- A bang: the voice count out of the right outlet, then the voice number out
  of the left.

The middle outlet reports the voice's mute state whenever it changes through
this object or through the ``~poly``. Outside a ``~poly`` it reports voice 0
of 1 and changes nothing.
