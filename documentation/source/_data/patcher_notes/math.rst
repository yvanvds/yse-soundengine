.. Notes for the Math category. Format: _data/patcher_notes/README.md.

.. patcher-object:: .!-

Use it when the value that should trigger the calculation is the one being
subtracted:

.. code-block:: text

   [.!- 100]   30 -> left inlet  =>  70

The inlets work as described under ``.+``.

.. patcher-object:: .!/

Use it when the value that should trigger the calculation is the divisor. A
left operand of 0 sends 0.

.. code-block:: text

   [.!/ 1]   4 -> left inlet  =>  0.25

The inlets work as described under ``.+``.

.. patcher-object:: .%

Works on whole numbers: a float on either inlet is truncated toward zero
first, so 7.9 counts as 7. The remainder has the sign of the left operand, so
-1 stays -1 rather than wrapping round:

.. code-block:: text

   [.% 12]   14  =>  2
   [.% 12]   -1  =>  -1

To wrap a number that can go negative into a range, use ``.pong`` with mode 2
(``.pong 0 12 2``), which sends 11 for -1. A divisor of 0 sends 0.
``.div`` sends the quotient that goes with the remainder. The inlets work as
described under ``.+``.

.. patcher-object:: .&

**The bitwise objects**

``.&``, ``.|``, ``.<<`` and ``.>>`` work on 32-bit whole numbers and send an
int. A float on either inlet is truncated toward zero first.

- ``.&`` keeps the bits set in both operands, so ``.& 15`` keeps the lowest
  four bits: 300 gives 12.
- ``.|`` keeps the bits set in either operand.
- ``.<<`` shifts bits up: ``.<< 4`` turns 3 into 48. Bits shifted past the
  top are lost, and a count of 32 or more gives 0.
- ``.>>`` shifts bits down and keeps the sign, so -8 shifted by 1 is -4. A
  count of 32 or more gives -1 for a negative number and 0 for any other.

A shift count below 0 shifts nothing. The inlets work as described under
``.+``.

.. patcher-object:: .+

**Hot and cold inlets**

The left inlet is hot: a number there is stored and the result is sent at
once. Every other inlet is cold: it stores its number for the next result and
sends nothing. The creation argument sets the right operand, which is 0 until
it is given.

.. code-block:: text

   [.+ 10]   5 -> left inlet                    =>  15
   3 -> right inlet, then 5 -> left inlet       =>  8

Every math object with more than one inlet works this way unless its notes
say otherwise.

To set both operands from one message, update the cold inlet first:
``.trigger`` sends from its rightmost outlet to its leftmost, so wire its
right outlet to the cold inlet and its left outlet to the hot one. ``.swap``
sends a pair in that order, and ``.!-`` and ``.!/`` are ``.-`` and ``./``
with the operands the other way round.

**Numbers**

Ints and floats are both accepted. A bang, a list or a word is ignored, so to
calculate again, send the left value again.

``.+``, ``.-``, ``.*``, ``./``, ``.!-`` and ``.!/`` calculate in floats and
send a float, even for two ints. A float holds whole numbers exactly only up
to 16,777,216; above that, ints lose precision. Dividing by 0 sends 0. Use
``.%`` and ``.div`` for whole-number division.

.. patcher-object:: .==

**The comparison and logic objects**

``.==``, ``.!=``, ``.<``, ``.<=``, ``.>``, ``.>=``, ``.&&`` and ``.||`` send 1
when their test holds and 0 when it does not, as an int. Ints and floats are
compared by value, so 3 equals 3.0.

The test is exact. A value that ought to be 1 but arrives as 0.9999999 after
some arithmetic is not equal to 1; send it through ``.round`` first.
``.&&`` and ``.||`` treat any number other than 0 as true, including
negative numbers and fractions such as 0.5.

The inlets work as described under ``.+``.

.. patcher-object:: .accum

The middle and right inlets change the stored value without sending it,
unlike the cold inlets of the other math objects, which only store an
operand.

**Messages**

- a number on the left inlet: replace the stored value and send it.
- bang: send the stored value.
- ``set <n>``: replace the stored value without sending it.
- ``add <n>``: add without sending, as the middle inlet does. ``ft1 <n>`` is
  another spelling of the same message.
- ``mult <n>``: multiply without sending, as the right inlet does.
- ``reset``: go back to the creation argument without sending. On an
  ``.accum 1`` that is 1, not 0.

A word without a number after it is ignored.

.. code-block:: text

   [.accum 1]   mult 2, mult 2, bang  =>  4

The stored value is a float; there is no whole-number mode, so put ``.round``
after the outlet to get whole numbers. The value never becomes infinite: it
stops at about plus or minus 3.4e38, and a later multiply brings it back. An
infinite or undefined number (NaN) changes nothing when it is added or
multiplied, and counts as 0 when it replaces the stored value.

.. patcher-object:: .anal

Every number after the first is counted as a pair with the number before it,
and the pair leaves with its new count. The first number only becomes the
one the next number is paired with.

.. code-block:: text

   60, 62, 60, 62  =>  60 62 1,  62 60 1,  60 62 2

Numbers are limited to 0 to ``limit``, and floats are truncated, so with the
default of 128 MIDI note numbers arrive unchanged.

Wire the outlet into ``.prob``: each list sets the weight of that transition
in ``.prob`` to the count, so ``.prob`` learns which numbers follow which and
can play in the same style.

**Messages**

- ``clear``: forget the counts, but keep the last number, so the next number
  still forms a pair with it.
- ``reset``: forget the last number but keep the counts, so the next number
  starts a new phrase.
- ``dump``: send every stored pair with its count, in the order the pairs
  were first seen. Use it to teach a ``.prob`` created later.

**Limits**

At most 1024 different pairs are stored. Once the table is full, pairs
already in it keep counting, and a new pair is not stored: outlet 1 bangs
instead. A count stops growing at 1,048,576.

.. patcher-object:: .atodb

**The engine's decibel scale**

``.atodb`` and ``.dbtoa`` use the scale the engine uses for its own volume
levels: an amplitude of 1 is 100 dB, and every halving of the amplitude is
about 6 dB less. An amplitude of 0.00001 and anything quieter gives 0, so the
result never goes negative.

.. code-block:: text

   1    =>  100
   0.5  =>  93.98
   0.1  =>  80

Subtract 100 to read the result as decibels relative to full scale, and add
100 to such a value before sending it to ``.dbtoa``.

.. patcher-object:: .cartopol

Both outlets send on every calculation, the angle first, so an object that
takes both values already has the angle when the distance arrives. The angle
is measured counterclockwise from the positive x axis, from -pi to pi.
``.poltocar`` converts back. The inlets work as described under ``.+``.

.. patcher-object:: .change

The first number is compared with the creation argument, 0 by default. A bare
``.change`` therefore ignores a first 0, and ``.change 5`` stays silent until
the value moves away from 5, so loading a patch does not report a parameter
that has not moved.

The comparison is exact: 0.5 and 0.5000001 are different numbers. To ignore
small changes, put ``.round`` in front.

**Outlets**

- left: the number, when it differs from the previous one.
- middle: 1 when the value changes from 0 to anything else.
- right: 1 when the value changes from anything else to 0.

The outlets send from right to left, so whatever the number triggers already
has the 0 reports. ``.togedge`` reports the same two changes as bangs.

**Modes**

- ``mode +``: send 1 when the number is greater than the previous one.
- ``mode -``: send -1 when the number is less than the previous one.
- ``mode``: back to sending numbers that differ.

A ``+`` or ``-`` after the creation argument starts the object in that mode.
In every mode the previous number is the last one received, whether it was
sent or not: a ``.change +`` fed 5, 3, 4 sends 1 (5 is above the starting
0), nothing, then 1.

**Messages**

- ``set <n>``: store n as the previous number without sending anything, for
  when the value was changed by another route.
- ``reset``: go back to the creation value and mode without sending.

A list counts as its first number. A bang, a word and an infinite or
undefined number (NaN) are ignored.

.. patcher-object:: .clip

The limits may be given in either order: ``.clip 1 -1`` clips like
``.clip -1 1``. An undefined number (NaN) becomes 0 before it is clipped, so
the result is always inside the range. ``~clip`` does the same to an audio
signal. The inlets work as described under ``.+``.

.. patcher-object:: .counter

The first bang sends the start value plus the step, so ``.counter 0 1`` sends
1, 2, 3 and so on. Create it as ``.counter -1`` to send 0 first.

An int on the left inlet sets the count and sends it; the next bang adds the
step to that. ``reset`` goes back to the start value without sending. Floats
are ignored on both inlets.

The count has no limit and does not wrap. To make it loop, send it through
``.%`` or ``.pong``, or reset it.

.. patcher-object:: .div

The quotient is truncated toward zero, so -7 divided by 2 is -3, and floats
are truncated before the division. ``.%`` sends the matching remainder.
The inlets work as described under ``.+``.

.. patcher-object:: .expr

**Placeholders**

``$i1`` to ``$i9`` read an inlet as an int, truncated toward zero; ``$f1`` to
``$f9`` read it as a float. The number is the inlet, counted from 1 at the
left. The object has as many inlets as the highest placeholder, and an inlet
that is skipped, such as inlet 2 in ``$f1 + $f3``, is there but unused. An
inlet that has not received a number reads 0.

**Types**

Types follow C:

- ``+``, ``-``, ``*``, ``/`` and ``%`` on two ints give an int, so
  ``$i1 / $i2`` is whole-number division. With a float on either side, the
  result is a float.
- Comparisons (``< <= > >= == !=``), logic (``&& || !``) and bitwise
  operators (``& | ^ << >> ~``) give an int. ``^`` is exclusive or, not a
  power: use ``pow()``.
- The result leaves as an int or a float to match.

.. code-block:: text

   [.expr ($f1 - 32) * 5 / 9]   212  =>  100, a float
   [.expr $i1 / 2]              7    =>  3, an int

**Functions**

``min``, ``max``, ``abs``, ``int``, ``float``, ``pow``, ``sqrt``, ``exp``,
``ln`` and ``log`` (both the natural logarithm), ``log10``, ``sin``, ``cos``,
``tan``, ``asin``, ``acos``, ``atan``, ``atan2``, ``sinh``, ``cosh``,
``tanh``, ``fact`` (factorial), ``round``, ``floor`` and ``ceil``. ``int``,
``float``, ``abs``, ``min`` and ``max`` keep ints as ints; the others return
a float.

Division by 0, and any result that is not a finite number, give 0.

**Input**

A list on the left inlet fills the inlets from the left and calculates.
Items past the ninth are ignored, and inlets the list does not reach keep
their numbers. A bang calculates again with the stored numbers. The other
inlets take single numbers.

**Errors**

An expression that cannot be read is reported in the log when the object is
created, and the object then sends 0. Random numbers (``random``, ``noise``)
and table functions (``size``, ``sum``, ``avg``, ``store`` and ``$s``
placeholders) are not supported and are reported the same way; use
``.random`` for random numbers.

The expression is read once, when the object is created, so evaluating it is
cheap and safe on the audio thread.

.. patcher-object:: .ftom

The scale is equal temperament with 12 notes per octave: 440 Hz is 69, 880 Hz
is 81, and 450 Hz is about 69.39. A frequency of 0 or below has no note, so it
gives -1500, which ``.mtof`` turns back into a frequency of almost 0.

.. patcher-object:: .histo

Numbers from 0 to ``size`` - 1 are counted; floats are truncated. A number
outside that range is not counted: outlet 1 bangs instead. With the default
size of 128, MIDI note numbers fit.

**Inlets and messages**

- a number on the left inlet: count it and send it with its new count.
- a number on the right inlet: send it with its count, without counting it.
- bang: send the last counted number and its count again; ``0 0`` before
  anything was counted.
- ``clear``: set every count to 0. The last number is kept, so a bang then
  reports it with a count of 0.
- ``dump``: send every number with a count above 0, lowest first.

Changing the ``size`` argument clears the counts. A count stops growing at
1,073,741,824.

.. patcher-object:: .linedrive

The output is ``outputMax * curve ^ (value - inputMax)``. With the defaults,
a 0-127 controller covers a wide range evenly:

.. code-block:: text

   [.linedrive 127 1 1.06]   127  =>  1
                             64   =>  about 0.025
                             0    =>  about 0.0006

Values above ``inputMax`` give more than ``outputMax``; nothing is limited,
so add ``.clip`` when that matters. A curve below 1 mirrors the response, so
the output grows as the value falls, and a curve of 1 always sends
``outputMax``. A curve of 0 or below, or a result too large for a float,
sends 0. The inlets work as described under ``.+``.

.. patcher-object:: .maximum

Numbers on the left inlet are compared with the stored value but never
change it. For the largest number received so far, use ``.peak``.

**Messages**

- a number: send the larger of it and the stored value.
- bang: send the last result again. Before the first result, that is the
  creation argument.
- a list of two or more numbers: send the largest item. The stored value
  takes no part, and afterwards the second largest item becomes the stored
  value. Up to 256 items are read; a one-item list counts as a number.

.. code-block:: text

   [.maximum 0.1]   0.05  =>  0.1
                    0.3   =>  0.3
                    3 9 4 =>  9, and 4 is now the stored value

With a control on the right inlet, ``.maximum`` is a lower limit that can
move. The result is always a float. An infinite or undefined number (NaN)
counts as 0.

.. patcher-object:: .mean

The count leaves before the average, so whatever the average triggers
already has the matching count.

**Messages**

- a number: add it and send the new average.
- bang: send the average and count again; 0 and 0 before any number.
- a list: send the average of its items (up to 256) and start over from
  them. Numbers that arrive later are averaged with the list's items.
- ``clear`` or ``reset``: start over without sending.

Every number counts the same however long the average runs; the sum is kept
in double precision, so it stays accurate over billions of numbers. An
infinite or undefined number (NaN) counts as 0 and is still counted. The
count outlet stops at 2,147,483,647, but the average stays correct. For an
average that forgets old numbers, use ``.slide``, or ``.array.stream`` with
``.array.mean``.

.. patcher-object:: .minimum

Works like ``.maximum`` with the comparison reversed: a list sends its
smallest item, and the second smallest becomes the stored value.

The stored value starts at 0, so a bare ``.minimum`` sends 0 for every
positive number. Give it an argument, or a number on the right inlet.

.. patcher-object:: .mtof

The scale is equal temperament with 12 notes per octave: 69 is 440 Hz, 60
(middle C) is about 261.63 Hz, and 69.5 is a quarter tone above A4. Note
numbers outside 0-127 work too, from -1500 to 1499: a higher note is treated
as 1499, and a lower one gives 0 Hz.

.. patcher-object:: .past

A number has reached its threshold when it is equal to it or above. The
object bangs when the number reaches the threshold after being below it, and
is ready again only once the number drops strictly below:

.. code-block:: text

   [.past 100]   80, 110, 120, 90, 105  =>  bang at 110, bang at 105
   [.past 5]     10, 5, 10              =>  one bang, at the first 10

A new object bangs on the first number that reaches the threshold.

**Several thresholds**

``.past 60 100`` compares a list item by item: the first item with 60, the
second with 100. It bangs when every item has reached its threshold. A list
with fewer items updates only those positions, a number is the first item,
and items past the last threshold are ignored. At most 256 thresholds are
held.

**Messages**

- ``clear``: forget the past, so the next number that reaches the thresholds
  bangs again. The thresholds are kept.
- ``set <numbers>``: change the thresholds. This does not clear: a threshold
  that is changed continually does not cause a bang for every message above
  it. Positions the new list adds start below their threshold.
- ``reset``: go back to the creation thresholds and clear.

None of them sends anything. A bang is ignored, and an infinite or undefined
number (NaN) counts as 0, in the input and in the thresholds.

.. patcher-object:: .peak

**Inlets and messages**

- a number on the left inlet: when it is greater than the peak, it becomes
  the peak and is sent. A number equal to the peak is not a new peak.
- a number on the right inlet: it becomes the peak and is sent, and counts as
  new even when it is lower.
- bang: send the peak from the left outlet only.
- a list ``<number> <peak>``: set the peak as the right inlet does, then offer
  the number. Items past the second are ignored.
- ``set <n>``: set the peak without sending.
- ``reset``: go back to the creation argument without sending.

Every number sends the two flag outlets, right before middle, and then the
new peak if there is one.

.. code-block:: text

   [.peak]   3, 5, 4, 9  =>  3, 5, (nothing), 9

The peak starts at 0, so negative numbers never beat it. For data below 0,
give a lower start, such as ``.peak -1000``. The result is always a float, and
an infinite or undefined number (NaN) counts as 0.

.. patcher-object:: .pong

**Modes**

The third argument sets the mode:

- 3, fold (the default): a number past a limit is reflected back, as many
  times as needed. With the range 0 to 10, 12 gives 8 and -3 gives 3.
- 2, wrap: a number past a limit comes back in from the other side. The
  range includes ``low`` but not ``high``, so 10 gives 0, 12 gives 2 and -3
  gives 7. Fed from a counter, this cycles through the range, negative
  counts included.
- 1, clip: as ``.clip``.
- 0: the number passes unchanged.

Any other mode folds. The limits may be given in either order, and when they
are equal the result is always that value. An infinity goes to the nearest
limit, and an undefined number (NaN) counts as 0. The inlets work as
described under ``.+``.

.. patcher-object:: .poltocar

Both outlets send on every calculation, y first, so an object that takes both
values already has y when x arrives. The inlets work as described under
``.+``.

.. patcher-object:: .scale

The mapping is

.. code-block:: text

   n   = (value - inLow) / (inHigh - inLow)
   out = outLow + (outHigh - outLow) * n ^ exponent

so an exponent above 1 starts slowly and ends fast, and one below 1 does the
opposite. Below ``inLow``, where n is negative, the curve is mirrored.

.. code-block:: text

   [.scale 0 127 0 1]     64  =>  0.504
   [.scale 0 127 0 1 2]   64  =>  0.254

A number outside the input range maps past the output range. Set the sixth
argument, ``clip``, to 1 to limit the result to the output range instead, or
use ``.zmap``, which always limits and is always a straight line.

Either range may run from high to low. When ``inLow`` equals ``inHigh``, the
result is ``outLow``. A result that is not a finite number sends 0. The
inlets work as described under ``.+``.

.. patcher-object:: .sin

**The single-input functions**

``.sin``, ``.cos``, ``.tan``, ``.asin``, ``.acos``, ``.atan``, ``.sinh``,
``.cosh``, ``.tanh``, ``.asinh``, ``.acosh``, ``.atanh``, ``.abs`` and
``.sqrt`` have one inlet. A number there, int or float, sends the result at
once as a float.

Angles are in radians: a full circle is 2 pi, about 6.2832. To use degrees,
multiply by 0.017453 (pi / 180) first:

.. code-block:: text

   [.* 0.017453] -> [.sin]   90  =>  1

Where a function has no finite result, for a number outside its range or a
result too large for a float, it sends 0. When 0 is also a meaningful
result, check the range of the input first.

.. patcher-object:: .slide

Each number moves the output ``(number - output) / slide`` closer to it. A
slide of 1 jumps straight to the number; a slide of 10 covers a tenth of the
remaining distance per number. The output starts at 0.

.. code-block:: text

   [.slide 4 4]   100, 100, 100  =>  25, 43.75, 57.81

The smoothing counts numbers, not time, so it depends on how often numbers
arrive. For a ramp over a set time, use ``.line`` or ``~line``.

A slide below 1 counts as 1. Once the output is within a millionth of the
number, it lands exactly on it. An infinite or undefined number (NaN) counts
as 0.

**Messages**

- bang: take another step towards the last number. Repeated bangs glide the
  output to it.
- ``set <n>``: make n the number to move towards, without sending. Follow it
  with bangs.
- ``reset``: set the output to 0 without sending. The number to move towards
  is kept.

.. patcher-object:: .swap

Use it when two values arrive in the wrong order for the hot and cold rule
(see ``.+``). The right outlet always sends first, and everything connected
to it finishes before the left outlet sends, so wire the right outlet to a
cold inlet and the left outlet to the hot one:

.. code-block:: text

   [.swap 100]  right outlet -> right inlet of [.-]
                left outlet  -> left inlet of [.-]
   x -> left inlet of [.swap]  =>  [.-] sends 100 - x

Each number keeps its type: an int leaves as an int and a float as a float.
The argument's spelling sets its type: ``.swap 5`` holds the int 5 and
``.swap 5.`` the float 5.

**Messages**

- a number on the left inlet: store it and send the pair.
- bang: send the pair again unchanged. Before anything has arrived, that is
  0 and the argument.
- a list: the first number goes to the left inlet's slot and the second to
  the right's, then the pair is sent. A single number leaves the right slot
  alone. Reading stops at the first word, and a list that starts with a word
  is ignored.

.. patcher-object:: .togedge

Only transitions are reported: 5 followed by 9 sends nothing, and neither
does 0 followed by 0. The two outlets therefore take turns, never banging
twice in a row from the same side, which makes them a reliable pair of
note-on and note-off triggers.

.. code-block:: text

   0, 0.2, 0.8, 0.3, 0  =>  left bang at 0.2, right bang at the last 0

The value starts at 0, so the first non-zero number bangs the left outlet and
a first 0 sends nothing. Any number other than 0 counts, fractions such as
0.5 included.

**Messages**

- bang: flip the value between zero and non-zero, and bang the matching
  outlet. A stream of bangs alternates left, right, left. After a number such
  as 5, a bang flips to 0 and bangs the right outlet.
- ``reset``: set the value to 0 without sending, so the gate is closed again
  without a right-outlet bang.

A list counts as its first number. Words and infinite or undefined numbers
(NaN) are ignored. ``.change`` reports the same transitions as 1s, next to
the value.

.. patcher-object:: .trough

Works like ``.peak`` with the comparison reversed: a number below the minimum
becomes the new minimum and is sent.

The minimum starts at 128, so MIDI values can go below it. For larger data,
give a higher start, such as ``.trough 100000``.

.. patcher-object:: .vexpr

The language is that of ``.expr``: placeholders, operators, types, functions
and errors work as described there.

Every inlet holds a list, and a single number is a one-item list. The
expression runs once per position, up to the length of the shortest list
among the inlets it uses. An inlet that holds one item supplies it to every
position.

.. code-block:: text

   [.vexpr $f1 * $f2]   0.5 -> right inlet, then 60 64 67 -> left  =>  30 32 33.5
   [.vexpr $i1 + $i2]   0 4 7 -> right inlet, then 60 -> left      =>  60 64 67

A single result leaves as an int or a float rather than a list. Every inlet
takes lists, of up to 256 items; items past that are dropped. A list without
numbers is ignored, a bang calculates again, and an inlet that has received
nothing reads as a single 0.

.. patcher-object:: .zmap

The mapping is the straight-line ``.scale``, always limited to the output
range. A number outside the input range, an infinity included, gives the
nearest output limit:

.. code-block:: text

   [.zmap 0 127 0 1]   200  =>  1
                       -5   =>  0

Either range may run from high to low. When ``inLow`` equals ``inHigh``, the
result is ``outLow``. The inlets work as described under ``.+``.

.. patcher-object:: ~*

**The audio-rate operators**

``~+``, ``~-``, ``~*`` and ``~/`` share these rules:

- Both inlets take an audio signal or a number, int or float. While audio is
  connected to an inlet, it is used sample by sample and a number sent there
  is kept for later; otherwise the last number is used. The left number
  starts at 0. The right one starts at the argument, which is saved with the
  patch; a number sent to the inlet later is not.
- A new number takes effect at the next audio block and is not smoothed, so a
  large jump can click. Ramp it with ``~line`` instead.
- When no argument is given, ``~+`` and ``~-`` use 0 and ``~*`` and ``~/``
  use 1, so a bare object passes the signal through unchanged.

.. code-block:: text

   [~* 0.5]   halves the signal's level
   [~/]       1 on the left inlet, audio on the right: the reciprocal of the audio

.. patcher-object:: ~/

A divisor of 0 gives 0, whether it is a number on the right inlet or a sample
of the audio connected there, so a silent divisor cannot put an infinity into
the signal chain. A divisor merely close to 0 still gives a large value. The
other rules are described under ``~*``.

.. patcher-object:: ~clip

The low and high inlets take ints and floats. When low
is above high, every sample becomes high. New limits take effect at the next
audio block.
