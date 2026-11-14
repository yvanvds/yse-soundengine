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
