.. Notes for the Spectral category. Format: _data/patcher_notes/README.md.

.. patcher-object:: ~fft

Takes a signal apart into its spectrum, one frame at a time. Every
*interval* samples, a frame of *size* samples is transformed, and its bins
come out over the next *size* samples, one per sample: bin k on sample k of
that output period, its real part on the left outlet, its imaginary part on
the middle outlet, and k itself on the right (sync) outlet. Bin k is the
frequency k * samplerate / size; the bins above size / 2 mirror those below
for a real signal. The values are not divided by the size: a sine of
amplitude 1 that fits a whole number of cycles in the frame shows up with a
magnitude of size / 2 in its two bins.

The output starts the sample after a frame's last input sample, so it lags
the input by one frame, and it does not depend on the block size. With the
interval equal to the size (the default) the frames follow each other with
no gap: each comes out while the next is taken in. A larger interval uses
the first *size* samples of each period and leaves the rest out; the
outlets are then 0 for the rest of the period, the sync outlet included.

The arguments are the size (a power of two from 16 to 65536, 512 by
default; other values are rounded up), the interval (a power of two at
least the size; 0 or none means the size) and the offset: how many samples
into the stream the first frame starts, taken modulo the interval. Two
``~fft`` objects with the same size and interval and offsets half a frame
apart give overlapping frames. The signal reads as 0 before it starts, so
with an offset the first frame is partly silence.

``phase <n>`` or ``offset <n>`` sets a new offset while it plays, from the
next block on: the frames move along the stream from there, and the frame
being taken in when it changes comes out mixed. Messages are not saved; the
arguments are. Changing an argument rebuilds the object.

An unconnected right inlet reads 0, a real signal. A number sent to an
unconnected inlet is used as a constant signal.

.. code-block:: text

   [~cycle 1378.125] -> [~fft 32]   (at 44100 Hz: 1 cycle per frame)
   bins 1 and 31 of every frame have a magnitude of 16, the others 0

.. patcher-object:: ~ifft

Turns a stream of bins back into a signal, one frame at a time: the inverse
of ``~fft``, with the same arguments, messages and outlets. Every
*interval* samples, a frame of *size* bins (bin k on sample k, the real part
on the left inlet and the imaginary part on the right one) is transformed
back, and its samples come out over the next *size* samples. The right
outlet counts the samples of each output period, 0 to size - 1.

The output is not divided by the size. Directly after a ``~fft`` with the
same size and interval, the frames line up when the interval equals the
size; with a larger interval, give the ``~ifft`` an offset *size* samples
later than the ``~fft``'s. The signal then comes back *size* times louder,
two frames (2 * size samples) later. Change the bins in between, through
``~*`` or ``~index``, to filter or resynthesise in the frequency domain.

.. code-block:: text

   [~fft 512] -> left and middle outlets -> [~ifft 512] -> [~* 0.001953125]
   the input again, 1024 samples later

.. patcher-object:: ~pfft

Runs the objects placed inside it once per spectral frame. Each signal inlet
is cut into frames of *size* samples, one every *size* / *overlap* samples,
windowed and transformed; the objects inside work on the bins, from
``~fftin`` to ``~fftout``; and the result is transformed back, windowed and
overlap-added to the outlets. Put objects inside with ``SetContainer``, as
for a ``patcher``. :doc:`/patcher/subpatchers` explains the model in full.

The arguments are a name (free text for a host to show; the engine does not
read it), the FFT size (a power of two from 16 to 65536, 512 by default;
other values are rounded up), the overlap (a power of two, 2 by default; 4
suits most processing), an offset (how many samples into the stream the
first frame starts, modulo the hop), the full-spectrum flag, and how many
signal inlets (1 to 16, 1 by default) and outlets (0 to 16, 1 by default)
it has. The pins come from these arguments, not from the objects inside.

The objects inside see a frame of *size* / 2 bins, DC up to the bin below
Nyquist, or *size* bins with the full-spectrum flag. The upper half of a
real signal's spectrum is filled in as the mirror of the lower half; the
Nyquist bin of a half spectrum is resynthesised as 0. A frame of more than
128 bins is rendered in passes of 128 bins, in bin order, at the sample the
frame completes, so every object inside works on ordinary 128-sample blocks;
``~fftin``'s bin-index outlet says which bin each sample is.

Frames are handled at the exact sample they complete, so the output is the
same whatever the block size, and the ``~pfft`` delays its input by exactly
*size* samples. With an unchanged spectrum the output is the input, *size*
samples later (without its Nyquist-frequency part in a half spectrum): the
overlap-add is divided, sample by sample, by the sum of the overlapping
analysis and synthesis windows, which holds for any window pair at an
overlap of 2 or more. With an overlap of 1 the average of that sum is used.

Inlet 0 also takes ``clear``, which empties every frame and the output, and
``mute 1`` / ``mute 0``, which stop and restart the objects inside: the
frames already being overlap-added play out, then the outlets are silent.
Neither is saved. A number sent to an unconnected inlet is used as a
constant signal. A signal cord cannot cross the ``~pfft``'s boundary; use
its inlets and outlets. Inside, ``~dac``, ``~adc`` and ``~send`` /
``~receive`` timing do not apply.

.. code-block:: text

   [~adc] -> [~pfft fx 1024 4] -> [~dac]
   inside:  [~fftin 0] real, imaginary -> [~* 0.5] -> [~fftout 0]
   the input at half its level, 1024 samples later

.. patcher-object:: ~fftin

Inside a ``~pfft``, puts out the spectrum of one of the ``~pfft``'s signal
inlets, chosen by the first argument (0 is the left inlet). The left outlet
is the real part of each bin, the middle one the imaginary part and the
right one the bin's index, from 0 to the frame size - 1: bin k is the
frequency k * samplerate / FFT size. In a frame of more than 128 bins the
outlets carry one pass of 128 bins at a time, and the index says where each
sample is. The values are not divided by the size: a sine of amplitude 1
centred on a bin has a magnitude of about size / 4 there under the Hann
window.

The second argument is the analysis window of that inlet: ``hanning`` (the
default), ``hamming``, ``blackman``, ``triangle`` or ``square`` (no window).
When two ``~fftin`` read the same inlet, the one created first sets its
window. Outside a ``~pfft`` it puts out silence.

.. patcher-object:: ~fftout

Inside a ``~pfft``, takes the spectrum one of the ``~pfft``'s signal outlets
is resynthesised from, chosen by the first argument (0 is the left outlet):
the real part of each bin on the left inlet, the imaginary part on the
right, timed as ``~fftin`` puts them out. Bins that nothing reaches are 0,
so an outlet without a ``~fftout`` is silent; several ``~fftout`` with the
same index add up. An unconnected inlet reads the last number sent to it,
else 0.

The second argument is the synthesis window of that outlet: ``hanning``
(the default), ``hamming``, ``blackman``, ``triangle`` or ``square``. The
overlap-add gain is computed for it together with the analysis window of the
inlet with the same index (inlet 0 when there is none), so an unchanged
spectrum comes back at the input's level. Outside a ``~pfft`` it does
nothing.

.. patcher-object:: ~fftinfo

Inside a ``~pfft``, reports the FFT size, the spectral frame size (the bins
the objects inside see: half the FFT size, or the FFT size with the full
spectrum), the hop (samples from one frame to the next) and the
full-spectrum flag, on its four outlets from left to right. It reports by
itself when the ``~pfft`` first runs its contents and whenever the figures
change, and again on a bang. Outside a ``~pfft`` it reports nothing.

.. patcher-object:: ~vectral

Smooths a stream of frames index by index. A frame is a run of values, one
per sample, such as the bins of a spectrum; ``~vectral`` keeps one smoothed
value per index and smooths each new value against the value at the same
index in the frames before it, never against its neighbours in the same
frame. That steadies a spectrum display, or follows a spectral envelope.

Three signals drive it. The right inlet carries the values. The middle inlet
says, sample by sample, at which index each value is smoothed and stored; the
left inlet says which stored value comes out. Usually both carry the same
index, a ramp from 0 to size - 1 that restarts with every frame, and every
value comes out smoothed in the frame it arrives in. An input index ahead of
the output index by d shifts the frame by d places. An index is used as its
whole part; one below 0 or at or past the size stores nothing, or reads 0.

The smoothing is chosen by message, and each mode keeps its own settings:

- ``rampsmooth <up> [down]``: a new value starts a straight ramp from the
  stored value, over ``up`` frames when it is higher and ``down`` frames when
  it is lower, as ``~rampsmooth`` does over samples. Whole frames; below 1
  counts as 1. This is the starting mode, at 1 and 1, which passes the
  frames through unchanged.
- ``slide <up> [down]``: each frame moves the stored value 1/``up`` of the
  way towards a higher value and 1/``down`` of the way towards a lower one,
  as ``~slide`` does over samples. Below 1 counts as 1.
- ``deltaclip <lowest> <highest>``: the change from one frame to the next is
  clipped between the two limits, as ``~deltaclip`` does over samples. The
  lowest limit is the steepest fall and normally negative.

With one number, ``rampsmooth`` and ``slide`` use it for both directions.
Switching modes keeps the stored values, so the smoothing carries on from
where it is. ``clear`` sets every stored value to 0. ``size <n>`` sets the
frame size, up to the creation argument, and clears the frame. The creation
argument is the frame size, 512 by default; it also sets the largest size
the message can choose. Messages are not saved with the patch.

.. code-block:: text

   [~count 0 512 1] -> left and middle inlets of [~vectral 512]
   [spectrum magnitudes] -> right inlet
   then "slide 1 20": peaks rise at once and fall away over about 20 frames

.. patcher-object:: ~framedelta

Puts out the change of every bin since the previous frame: each value minus
the value at the same place in the frame before. Fed the phases of a
spectrum (the right outlet of ``~cartopol``), that is the phase deviation of
each bin from one frame to the next, the starting point of a phase vocoder:
a bin's phase deviation tells how far its frequency lies from the bin's
centre.

Inside a ``~pfft`` a frame is the ``~pfft``'s spectral frame, bin by bin,
however many passes it takes. Anywhere else every block counts as a frame,
so sample k of a block is compared with sample k of the block before. The
first frame, and the first after ``clear``, comes out as it goes in. A frame
of another size than the one before (another ``~pfft`` size) starts again
from 0 as well. A value that is not a finite number counts as 0.

The argument is the largest frame it keeps, in bins: 4096 by default, which
serves a ``~pfft`` up to an FFT size of 8192 (half spectrum). Bins past it
put out 0, so give a larger ``~pfft`` a larger argument. ``clear`` sets the
stored frame to 0 from the start of the next frame; it is not saved.

.. patcher-object:: ~frameaccum

Puts out a running sum of every bin over successive frames: each value plus
the sum of the values at the same place in the frames before. After a
``~framedelta`` on the phases of a spectrum it gives the running phase of a
phase vocoder: change the phase deviations between the two (scaled, they
move every bin's frequency, a simple spectral pitch shift), and
``~frameaccum`` builds up the new phases.
``~framedelta`` followed by ``~frameaccum`` with nothing in between gives
the frames back.

Frames, the first frame, a change of frame size, non-finite values and
``clear`` work as for ``~framedelta``. The first argument switches on phase
wrapping: nonzero wraps the sums into -pi to pi and keeps them there, as a
``~phasewrap`` after the object would (pi itself becomes -pi), but without
the sums growing into the thousands and losing their precision. The second
argument is the largest frame kept, in bins, 4096 by default.

.. code-block:: text

   inside [~pfft pv 2048 4]:
   [~fftin 0] real, imaginary -> [~cartopol]
   [~cartopol] magnitude -> left inlet of [~poltocar]
   [~cartopol] phase -> [~framedelta] -> [~* 1.5] -> [~frameaccum 1]
   [~frameaccum 1] -> right inlet of [~poltocar]
   [~poltocar] real, imaginary -> [~fftout 0]
