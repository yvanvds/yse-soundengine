.. Notes for the Signal generators category. Format: _data/patcher_notes/README.md.

.. patcher-object:: ~line

A float at the left inlet starts a ramp from the current value to that
target. The ramp takes the time last set at the right inlet, so set the time
first. A new time does not change a ramp that is already running, and
sending the target the ramp already has does nothing. With a time of 0 the
output jumps to the target.

The time is counted in whole milliseconds and then rounded to whole audio
blocks; a ramp always lasts at least one block. Within a block the ramp is
smooth, sample by sample.

.. code-block:: text

   [~line 0 20]   left <- 1.   =>   rises from 0 to 1 over 20 ms

``stop`` halts the ramp and holds the output at its current value until the
next target arrives. A new time on its own does not restart it. Any target
after a ``stop``, even the one the ramp was heading for, starts a new ramp
from the held value, taking the full ramp time.
