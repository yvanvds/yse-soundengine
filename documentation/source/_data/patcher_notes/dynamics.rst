.. Notes for the Dynamics category. Format: _data/patcher_notes/README.md.

.. patcher-object:: ~normalize

Scales a signal so that its loudest sample so far comes out at a target
amplitude: every sample is multiplied by the amplitude divided by the peak,
the largest absolute input value received so far. The peak only rises, so the
output never exceeds the amplitude, and a quiet passage after a loud one stays
quiet. Use it to match the level of analysis or control signals whose range is
not known in advance.

The amplitude goes into the right inlet, as a number or a signal read per
sample; the creation argument sets it (default 1) and is what is saved with
the patch. A negative amplitude inverts the signal.

The peak starts at 0.000001. A number in the left inlet sets it, and a louder
input sample raises it again. ``reset`` sets it back to 0.000001 and
``reset <peak>`` sets it to that value; both work in either inlet, also as
text through a receive. An input sample that is not a finite number gives 0
and leaves the peak alone. Unconnected, the left inlet reads 0.

.. code-block:: text

   [~adc] -> [~* 0.01] -> [~normalize 0.8]
   a quiet input brought up so its loudest moment so far is at 0.8
