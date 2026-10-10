.. Notes for the Subpatchers category. Format: _data/patcher_notes/README.md.

.. patcher-object:: patcher

A ``patcher`` object has no inlets, outlets or arguments of its own. Create
its contents with ``CreateObject`` on the same patcher and move them inside
with ``SetContainer``. Its pins are the boundary objects among its direct
contents: inlet N is the ``.inlet`` or ``~inlet`` with index N, and outlet N
the ``.outlet`` or ``~outlet`` with index N. Connect to the subpatcher by pin
number, as to any object.

Subpatchers nest, and nesting costs the audio thread nothing. Deleting a
subpatcher deletes everything inside it. On load, the contents fire their
``.loadbang`` objects together with the rest of the patch, in no set order.

Names are not local: a ``.r freq`` inside a subpatcher hears every ``freq`` in
the whole patcher. :doc:`/patcher/subpatchers` explains how to build, connect,
save and edit subpatchers.

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
