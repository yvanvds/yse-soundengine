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
