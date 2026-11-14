.. Notes for the Delay category. Format: _data/patcher_notes/README.md.

.. patcher-object:: ~tapin

Records its input into a delay line. The outlet is not an audio output: it
connects only to the left inlet of ``~tapout``, which reads the line. One
``~tapin`` can feed any number of ``~tapout`` objects.

- The argument is the longest delay, in milliseconds (default 100). The
  memory for it is allocated when the object is created.
- A number at the inlet sets a new longest delay, up to the argument, and
  clears the line. ``clear`` clears it without changing the length.
- ``freeze 1`` stops recording: the line keeps replaying its last longest
  delay of audio. ``freeze 0`` records again.

A cord from a ``~tapout`` back into its ``~tapin`` is feedback, and is
allowed. A loop of ordinary signal cords is refused, because no object in it
could ever run first; going through a delay line is how a patch builds an
echo, a comb or a reverb.

.. code-block:: text

   [~tapin 2000]   a line up to two seconds long

.. patcher-object:: ~tapout

Reads the delay line of the ``~tapin`` connected to its left inlet. Each
argument is one tap with its own delay inlet and output, so
``[~tapout 100 250 400]`` has three.

- A number at a tap's inlet sets that tap's delay in milliseconds. The delay
  is rounded to whole samples, and a change jumps.
- A signal at a tap's inlet sets the delay sample by sample, read with
  interpolation, so a moving delay bends the pitch smoothly (chorus,
  flanger). A connected signal wins over the number.
- A list at the left inlet sets the delays of several taps, from the first
  one on.
- The shortest delay is one block of samples, the longest is the
  ``~tapin``'s. A delay outside that range is held at the nearest end.
- With no ``~tapin`` connected, the outputs are silent.

.. code-block:: text

   [~tapin 1000]
    |
   [~tapout 300]   an echo 300 ms later; send it through [~* 0.5] back
                   into ~tapin for repeats that fade by half each time

.. patcher-object:: ~comb

A comb filter: the input, the input some time ago and the output the same
time ago, each with its own gain.

.. code-block:: text

   y(n) = a x(n) + b x(n-D) + c y(n-D)

With only ``b`` the filter is a plain delay; ``a`` and ``b`` together cut
notches into the spectrum, evenly spaced, the classic flanger sound when the
delay moves. ``c`` feeds the output back, so the filter rings at the
frequency ``1000 / D`` (D in ms) and its multiples, each echo ``c`` times the
one before: a resonator, or a plucked string when a short burst of noise goes
in.

The creation arguments are the maximum delay in ms, the delay in ms and the
gains ``a``, ``b`` and ``c`` (``~comb 50 5 1 0 0.9``); they default to 10 and
zeros, which is silent. The maximum sizes the delay lines when the object is
made, for rates up to 192 kHz, so nothing is allocated while it runs; changing
it rebuilds the object. The delay and the gains are numbers or signals in the
four right inlets, read per sample, or a list of up to four numbers in any
inlet. A connected signal wins over the number, which is used again once the
cord is gone.

The delay follows the sample rate and is interpolated between samples, so it
can be swept. It is held between one sample and the maximum. ``c`` is held
between -1 and 1; at exactly 1 or -1 the filter rings forever. ``clear`` in
any inlet silences the delay lines. An input sample that is not a finite
number reads as 0. Unconnected, the left inlet reads 0.

.. code-block:: text

   [~noise] -> [~comb 50 2.27 0.2 0 0.97] -> [~dac]
   a buzzing resonance near 440 Hz; sweep the delay with a slow ~cycle

.. patcher-object:: ~teeth

The comb filter of ``~comb`` with a separate delay for each path.

.. code-block:: text

   y(n) = a x(n) + b x(n-F) + c y(n-B)

The second inlet is the feedforward delay ``F``, the third the feedback delay
``B``, both in ms; then the gains ``a``, ``b`` and ``c``. With ``F`` and
``B`` equal it is ``~comb``. Apart from the second delay it works the same:
the creation arguments are the maximum delay, the two delays and the three
gains (``~teeth 50 3 7 1 0.5 0.8``); a list in any inlet sets up to five
numbers in that order; ``clear`` silences the lines.

.. code-block:: text

   [~click] -> [~teeth 50 1.3 4.1 1 0.5 0.8] -> [~dac]
   notches from the short path, ringing from the long one

.. patcher-object:: ~delay

Delays a signal by a number of samples. The delay can be anything from 0,
where the signal passes unchanged, up to the longest delay, even less than
one block, which ``~tapout`` cannot do. Not to be confused with ``.delay``,
which delays a bang.

- The creation arguments are the memory in samples, the initial delay in
  samples and the ramp time in ms (``~delay 44100 200 10``); they default to
  512, 0 and 0. The memory is allocated when the object is made, so nothing
  is allocated while it runs; changing it rebuilds the object.
- A number at the right inlet sets the delay in samples; a fraction is
  dropped. ``<n> samples`` means the same, and ``<n> ms`` sets it in
  milliseconds, rounded to whole samples at the current sample rate. A delay
  in samples lasts longer at a lower sample rate; one in milliseconds does
  not. ``delay <time>`` takes the same forms.
- With a ramp time (``ramp <ms>``), a new delay crossfades from the old one
  instead of jumping, so the change does not click. A change during a
  crossfade starts a new crossfade from the delay that was fading in.
- A signal at the right inlet sets the delay sample by sample, in samples,
  read with interpolation, so it can be swept smoothly. It skips the ramp,
  and the number is used again once the cord is gone.
- ``maxsize <n>`` sets the longest delay in samples, up to the memory, and
  clears the line. ``clear`` silences the line. A delay outside 0 and the
  longest delay is held at the nearest end.
- All of these words work in either inlet. An input sample that is not a
  finite number is written as 0. Unconnected, the left inlet reads 0.

The output cannot be fed back into the input: a loop of signal cords is
refused. For echoes that repeat, use ``~tapin`` and ``~tapout`` or
``~comb``.

.. code-block:: text

   [~adc] -> [~delay 4800 96] -> [~dac]
   the input, two milliseconds late at 48 kHz

.. patcher-object:: ~pitchshift

Shifts the pitch of a signal up or down without changing its speed. The
shift is a ratio (``2`` an octave up, ``0.5`` an octave down, ``1``
unchanged) or, with ``usecents 1``, cents (``100`` a semitone up,
``-1200`` an octave down), from two octaves down to two octaves up.

- The first argument is the number of channels: ``[~pitchshift 2]`` has two
  signal inlets and outlets, shifted alike, and the shift inlet after them.
  Any of the words below may follow with a value, and are saved with the
  patch: ``[~pitchshift 1 pitchshiftcent 700 quality good]``.
- The last inlet takes the shift as a number or a signal. A number is read
  as a ratio or as cents by what ``usecents`` was when it arrived. A signal
  is read per sample, as ``usecents`` says now, and wins over the number.
- ``pitchshift <ratio>`` and ``pitchshiftcent <cents>`` set the shift in
  either unit. ``enabled 0`` passes the input straight through, with no
  delay; ``enabled 1`` starts shifting again. All words work in any inlet.
- A pitch shifter needs some of the past to work with, so its output comes
  late. The last outlet reports that latency in samples: ``reportlatency 1``
  sends it now and every time it changes, and ``getlatency`` sends it once.
  ``getlatency 1`` and ``getlatency 0`` are the same as ``reportlatency``.

``quality`` picks how the shifting is done, and with it the latency:

.. list-table::
   :header-rows: 1
   :widths: 14 46 40

   * - quality
     - how
     - latency at 48 kHz
   * - ``basic`` (default)
     - Two overlapping grains of 1024 samples read a delay line, each
       aligned with the other so a tone stays in phase.
     - 2 samples unshifted, about 160 a semitone away, 642 an octave up
       (13 ms)
   * - ``good``
     - The same with grains of 2048 samples: aligns lower notes (down to
       about 94 Hz instead of 187), and its level wavers more slowly.
     - about twice ``basic``'s
   * - ``better``
     - A phase vocoder: the spectrum, 2048 samples at a time, with its
       peaks moved to their new pitch.
     - 2048 samples (43 ms)
   * - ``best``
     - The phase vocoder with 4096 samples and more overlap.
     - 4096 samples (85 ms)

The grain modes are quick and suit live playing and small corrections, but
where the grains cross a chord or noise partly cancels, so its level wavers
about 94 times a second (``basic``), and an attack can be heard twice. The
phase vocoder keeps a steady tone at its level, at the price of more delay
and smeared attacks. In the grain modes the latency depends on the shift;
``constantlatency 1`` holds it at its largest (1666 samples for ``basic``,
3330 for ``good``) whatever the shift, which keeps a shifted copy in time
with itself while the shift moves. Changing ``quality`` starts the shifter
again: the output is silent for the new latency. In every mode the formants
move with the pitch, so a voice shifted up sounds smaller.

.. code-block:: text

   [~adc] -> [~pitchshift 1 pitchshiftcent -1200] -> [~dac]
   an octave down

.. patcher-object:: ~retune

Listens to the pitch of a single voice or instrument and pulls it onto the
nearest note of a scale: the familiar autotune effect, or, with a little
correction, a gentle intonation fix. It also works as a pitch detector.

- The first argument is the standard pitch, the A in Hz (``440``). Any of
  the words below may follow with their values, and are saved with the
  patch: ``[~retune 440 notelist 200 400 500 700 900 1100 notebase 9]`` is
  A major. Every word also works as a message in any inlet.
- **The scale.** ``notelist`` gives the notes of one octave above the base
  note, in cents (``100`` a semitone) or as ratios (``9/8``, ``3/2``), as a
  Scala file lists them; the base note itself is always the first note, and
  a ``1200`` or ``2/1`` at the end (the octave) is left out. ``notebase``
  picks the base note, ``0`` for C up to ``11`` for B. The scale repeats at
  every octave. ``defaulttuning`` goes back to the twelve semitones.
- **Which notes.** ``enablednotes`` lists the notes of the scale to correct
  toward, counted from ``0`` (the base note): ``enablednotes 0 2 4`` in a
  major scale is its first, third and fifth. ``enablednotes all`` and
  ``flush`` turn every note back on. A list of MIDI notes and velocities in
  the left inlet turns notes on (a velocity above 0) or off (0): the note
  number counts from the base note, wrapping round the scale, so with the
  twelve semitones on C, ``69 0`` turns A off.
- **How much.** ``correction_amount`` is how far a note is pulled, in
  percent: ``100`` (the default) right onto the note, ``0`` not at all.
  ``correction_threshold`` leaves a note alone while it is within that many
  cents of its target, so a singer's small drifts stay.
- **When not to.** Sound that has no clear pitch passes through
  uncorrected. ``correction_ambience_threshold`` (``0.3``) is how clear it
  has to be: higher also corrects noisier or more reverberant sound, lower
  leaves more of it alone. ``correction_bypass`` leaves hissing sounds such
  as an "s" alone: it is the share of samples, in percent, on which the
  signal changes sign (about 2 for a sung A, 20 to 40 for an "s") above
  which nothing is corrected. The default, ``100``, never bypasses.
- The middle inlet takes a pitch in Hz to pull every note toward instead
  of the scale, as a signal or a number; ``0`` goes back to the scale. The
  right inlet adds a detune in cents, as a signal or a number.
- ``retune 0`` passes the input straight through, without delay; ``retune
  1`` corrects again. With ``pitchdetection 1`` the middle outlet carries
  the detected pitch in Hz as a signal, ``0`` where there is none, also
  while ``retune`` is off. ``windowsize`` is how often, in samples, the
  pitch is measured (256 by default).
- ``quality`` (``basic``, ``good``, ``better`` or ``best``) picks the pitch
  shifter, exactly as for ``~pitchshift``, and with it the latency. In the
  grain modes the latency follows the correction: about 130 to 145 samples
  for a correction within 40 cents at ``basic``, and 2 when nothing is
  corrected. ``better`` and ``best`` are 2048 and 4096 samples whatever
  the correction. The right outlet reports the latency, with
  ``reportlatency`` and ``getlatency`` as for ``~pitchshift``.
- ``use_16bit`` is accepted and changes nothing.

A note is measured over about 2048 samples, so the start of a new note is
still pulled toward the previous one's target for a moment, about 20 ms at
48 kHz. The lowest pitch it hears is about 47 Hz at 48 kHz. It follows a
single voice: in a chord it finds one pitch at best, and corrects the whole
chord by it.

.. code-block:: text

   [~adc] -> [~retune 440 notelist 200 400 500 700 900 1100 notebase 9] -> [~dac]
   a voice pulled onto A major
