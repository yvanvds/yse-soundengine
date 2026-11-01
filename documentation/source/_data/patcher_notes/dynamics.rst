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

.. patcher-object:: ~limi

A brickwall peak limiter that looks ahead. The signal is delayed by the
lookahead, and the gain is worked out from the signal before the delay, so it
can come down gradually and reach what a peak needs by the time that peak
comes out. No output sample is louder than the threshold (then the postamp),
and there is no overshoot on a sudden peak. The cost is latency: the output is
``lookahead`` samples late.

The first two arguments are the number of channels (1-64, default 1), one
inlet and one outlet each, and the size of the delay line in samples (default
512), which is the longest lookahead the object can use. All channels share
one gain, taken from the loudest of them, so a peak on one channel turns the
others down by the same amount and the balance between them stays put.

After those, the settings as words and values, which are saved with the patch;
the same words sent as messages to the left inlet change them while it plays,
but are not saved:

- ``threshold <dB>``: the highest output level, default 0 (full scale).
- ``lookahead <samples>``: how far ahead the gain looks, default 100, at most
  the delay-line size. Changing it empties the delay line, so expect a short
  gap.
- ``release <ms>``: how long the gain takes to come back up after a peak,
  default 100.
- ``mode <0|1>``: the shape of the release: 0 a straight line, 1 (the
  default) a curve that slows as it gets close.
- ``preamp <dB>`` and ``postamp <dB>``: gain before and after the limiter,
  default 0. Raising the preamp drives the signal harder into the limiter.
- ``dcblock <0|1>``: remove DC offset before limiting, default 0.
- ``bypass <0|1>``: send the signal through unchanged, still delayed by the
  lookahead so switching does not jump in time.

``clear`` empties the delay line and lets the gain return to 1 at once. An
unconnected inlet reads the last number sent to it, else 0.

.. code-block:: text

   [~limi 2 512 threshold -1 lookahead 64 release 50]
   a stereo master limiter holding both channels under -1 dB, 64 samples late

.. patcher-object:: ~peaklim

A stereo peak limiter for the end of a master bus, with a gain-reduction
meter. It uses the same limiter as ``~limi``, with two linked channels and a
fixed lookahead of 64 samples, so the output is 64 samples late.

The settings are words and values, as arguments (saved with the patch) or as
messages to the left inlet (not saved):

- ``threshold <dB>``: the highest output level before the output gain, default
  0.
- ``ingain <dB>`` and ``outgain <dB>``: gain before and after the limiter,
  default 0.
- ``mode <0|1>``: the shape of the release (100 ms): 0 a straight line, 1 (the
  default) a curve.
- ``bypass <0|1>``: send the signal through unchanged, still delayed.
- ``meters <0|1>``: whether the right outlet reports, default 1.
- ``meterrate <ms>``: how often it reports, default 100; 0 reports every audio
  block.

The right outlet sends the deepest gain reduction since the last report, in
dB: 0 when nothing was limited, -6 when the signal was halved. ``meterdata``
asks for one report after the next block, even with the meters off. An
unconnected inlet reads the last number sent to it, else 0.

.. code-block:: text

   [~peaklim threshold -0.3 ingain 3] -> right outlet -> [.f]
   a louder master held under -0.3 dB, with the gain reduction shown in a number box
