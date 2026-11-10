.. Notes for the Spectral category. Format: _data/patcher_notes/README.md.

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
