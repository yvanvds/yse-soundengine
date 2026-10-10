.. Notes for the Random category. Format: _data/patcher_notes/README.md.

.. patcher-object:: .decide

An int or float at the left inlet flips the coin just like a bang; its value
is ignored.

**Seeds**

``.decide``, ``.drunk``, ``.prob`` and ``.urn`` each keep their own random
sequence, set by the ``seed`` argument:

- Any seed other than 0 gives the same sequence on every run, so a generative
  patch can replay exactly.
- Seed 0, the default, takes a sequence from the engine's generator. That too
  repeats from run to run, until the host calls ``YSE::Randomize()``
  (``yse_randomize()`` in C).
- ``seed <n>`` (or an int at the seed inlet, where there is one) restarts
  the sequence while the patch runs. A saved patch keeps the seed it was
  created with, not the last one sent.

.. patcher-object:: .drunk

Each bang adds a random step to the current position and sends the result,
clipped to 0 to range-1. The walk stays at an edge once it reaches it, until a
step leads back; for a walk that bounces or wraps, pass the output through
``.pong``.

The step size is a limit that steps never reach: with the default 2, each step
is -1, 0 or +1. A negative step size gives the same steps without 0, so the
walk moves on every bang (if it is not held at an edge).

**Messages**

- bang: take a step and send the new position.
- an int or float: move to that position (clipped) and send it.
- ``set <n>``: move to ``n`` without sending.
- ``seed <n>``: restart the random sequence; see ``.decide``.

Changing the range clips the current position into it straight away. Floats
are truncated.

.. patcher-object:: .prob

A list ``<from> <to> <weight>`` stores a transition. Storing the same pair
again replaces its weight. For each state, the weights of its transitions are
added up, and each weight's share is the chance of that jump:

.. code-block:: text

   1 1 3,  1 2 4,  1 3 1   =>   from state 1: to 1 37.5%, to 2 50%, to 3 12.5%

A weight of 0 keeps the transition but switches it off; negative weights count
as 0. A state may jump to itself.

Each bang makes one jump from the current state and sends the new state,
which becomes the current state. When the current state has no transition with
a weight above 0, nothing comes out of the left outlet: the middle outlet
bangs and the current state becomes the ``reset`` state, so the next bang can
start again.

**Messages**

- bang: make one jump.
- an int or float: set the current state without sending anything.
- ``<from> <to> <weight>``: store a transition. A list of any other length is
  ignored.
- ``clear``: forget every transition.
- ``dump``: send every transition out of the right outlet, in the order they
  were first stored.
- ``reset <n>``: set the state to return to after a dead end.
- ``seed <n>``: restart the random sequence; see ``.decide``.

**Limits**

The table holds 1024 transitions. Once it is full, a new pair is ignored;
changing the weight of a stored pair still works.

``.anal`` builds the same kind of table from a stream of numbers.

.. patcher-object:: .random

``.random`` draws from the engine's generator, so its numbers repeat from run
to run until the host calls ``YSE::Randomize()``, and ``YSE::RandomSeed()``
(``yse_random_seed()`` in C) makes them replay. It has no seed of its own. A
range of 0 or less always gives 0.

.. patcher-object:: .urn

Each bang deals one number from 0 to limit-1 that has not come out since the
last refill, so every number appears once before any repeats. When all are
dealt, a bang sends nothing from the left outlet and bangs the right one
instead.

**Messages**

- bang: deal the next number.
- ``clear``: put every number back, in a new order.
- ``seed <n>``: restart the random sequence and reshuffle the numbers not yet
  dealt; see ``.decide``.
- an int or float at the right inlet: set the limit, clamped to 1-4096, and
  refill.

The urn is refilled only by ``clear`` or a new limit. To deal round after
round, send ``clear`` when the right outlet bangs.
