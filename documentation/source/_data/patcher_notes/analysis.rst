.. Notes for the Analysis category. Format: _data/patcher_notes/README.md.

.. patcher-object:: ~snapshot

Turns a signal into numbers, for the parts of a patch that work with
messages.

- A bang reports the value in the last rendered block, at once.
- With an ``interval`` (in milliseconds), it also reports on its own: on the
  first block after reporting starts, then once per interval. Reports happen
  at the end of a block, after every signal object has rendered it. An
  interval shorter than a block reports once per block.
- A non-zero number at the left inlet, or ``start``, turns interval reporting
  on; ``0`` or ``stop`` turns it off. It is on when the object is created.
- A number at the right inlet, or ``interval <ms>``, sets the interval. ``0``
  stops interval reporting. ``sampleinterval <n>`` sets it in samples
  instead, until the next ``interval``.
- ``offset <n>`` picks which sample of the block is reported, from 0. A
  number past the end of the block reports the last sample.

.. code-block:: text

   [~snapshot 100]   reports its input ten times a second
