Patcher: a step sequencer
=========================

Goal: build an eight-step sequencer in a patch. A ``.metro`` sets the pace,
a ``.counter`` counts the steps, and a ``.coll`` holds the note for each step.
The host starts and stops it, changes its speed and rewrites steps while it
plays.

This tutorial assumes you have worked through :doc:`05_patcher`. The code on
this page is compiled and run by the test suite
(`Tests/patcher/test_patcher_tutorials.cpp
<https://github.com/yvanvds/yse-soundengine/blob/dev/Tests/patcher/test_patcher_tutorials.cpp>`_),
which checks that the pattern plays in order and that the patch makes sound.

The patch
---------

.. code-block:: text

   .r run   .r rate
      |        |
    .metro 125           a bang every 125 ms
      |
    .counter -1          0, 1, 2 ...
      |
    .% 8                0 ... 7, 0 ... 7
      |
    .coll pattern  <---  .r edit
      |
    .mtof -> ~saw -> ~lp 1200 -> ~* 0.2 -> ~dac (both channels)

In code:

.. literalinclude:: ../../../Tests/patcher/test_patcher_tutorials.cpp
   :language: cpp
   :start-after: tutorial:step-sequencer:begin
   :end-before: tutorial:step-sequencer:end
   :dedent: 2

The clock
~~~~~~~~~

``.metro 125`` sends a bang every 125 ms once it is started: a sixteenth note
at 120 BPM. Its left inlet starts it with any non-zero number or a bang, and
stops it with ``0`` or ``stop``. Every start sends a bang straight away. A
number in the right inlet changes the interval, also while it runs. Both
inlets are wired to receivers, so the host controls it by name.

A millisecond ``.metro`` runs on the engine's timer thread, not on the audio
thread, so each step is heard at the start of the next audio block. That is
fine for a sequencer that plays on its own. When the steps must stay in time
with other things (clips, a second sequencer, a tempo that changes), give the
metro a beat interval on a domain clock instead: send ``clock main`` to its
left inlet (``main`` being a clock the host created) and ``16n`` to its right
inlet. :doc:`/patcher/time` explains the clocks and what each one is
accurate to.

The step number
~~~~~~~~~~~~~~~

``.counter`` adds its step (1) to its value and sends the result. Its
argument is the value it starts at, so ``.counter -1`` sends 0 on its first
bang, then 1, 2 …. ``.% 8`` folds that into the range 0-7, so the pattern
repeats every eight steps. To change the pattern length, change the ``8``.
A number in the counter's right inlet changes the step: send it ``2`` and
the sequencer plays every other step.

A ``reset`` message puts the counter back at its start value (-1), so the
next bang plays step 0 again. The test on this page uses it to replay the
first step.

The pattern
~~~~~~~~~~~

``.coll`` stores messages at addresses. A list whose first item is a number
stores the rest of the list at that address, and a number on its own recalls
what is stored there. So ``"3 63"`` stores note 63 at step 3, and the step
number ``3`` coming from ``.% 8`` sends 63 on to ``.mtof``.

Fill the pattern by sending one list per step to the ``.coll``:

.. literalinclude:: ../../../Tests/patcher/test_patcher_tutorials.cpp
   :language: cpp
   :start-after: tutorial:step-sequencer-fill:begin
   :end-before: tutorial:step-sequencer-fill:end
   :dedent: 4

``SetListData`` delivers the list straight to the object's inlet, on the
calling thread, before it returns. A step that holds nothing sends nothing,
so the previous note keeps sounding.

The collection is named ``pattern``. Every ``.coll pattern`` in the same
patcher shares one collection, so a second ``.coll pattern`` elsewhere in the
patch can read or edit the same steps. The contents are saved with
``DumpJSON`` and come back with ``ParseJSON``. :doc:`/patcher/data` covers
``.coll`` and the other stores.

The sound
~~~~~~~~~

``.mtof`` turns the note into a frequency for the ``~saw``. The ``~lp``
softens it and the ``~*`` sets the level. One outlet can feed several inlets,
so the ``~*`` feeds both channels of the ``~dac``.

Playing it
----------

Attach the patcher to a sound and start the metro:

.. code-block:: cpp

   YSE::System().init();

   YSE::patcher patch;
   patch.create(2);
   StepSequencer seq = BuildStepSequencer(patch);
   // ... fill seq.pattern as above ...

   YSE::sound sound;
   sound.create(patch);
   sound.play();

   patch.PassData(1, "run");      // start
   patch.PassData(100, "rate");   // faster: 100 ms per step
   patch.PassData(0, "run");      // stop

While it plays, rewrite a step from the host through the ``edit``
receiver:

.. code-block:: cpp

   patch.PassData(std::string("0 36"), "edit");   // step 0 now plays note 36

``PassData`` queues the value, and it reaches the ``.coll`` at the start of
the next audio block. ``.coll`` is safe to write on the audio thread: it
never allocates or waits there.

Things to try
-------------

- Replace everything after the ``.coll`` with the voice from
  :doc:`12_patcher_subpatched_voice`, and store a velocity with each note
  (``"3 63 100"``). The ``.coll`` then sends the list ``63 100``, which is
  exactly what the voice takes, and a step stored with velocity 0
  (``"5 63 0"``) is a rest.
- Build a second ``.metro`` → ``.counter`` → ``.coll pattern`` chain at a
  different rate. Both ``.coll`` objects share one pattern, so a second line
  reads the same notes at its own pace.

Next
----

- :doc:`12_patcher_subpatched_voice` — package a voice as a subpatcher.
- :doc:`/patcher/time` — metronomes, delays, tempo and clocks.
- :doc:`/patcher/data` — ``.coll``, dictionaries and arrays.
- :doc:`/patcher/objects/index` — every object on this page, in detail.
