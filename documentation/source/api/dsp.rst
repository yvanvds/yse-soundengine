DSP
===

Buffers and samples
-------------------

.. doxygenfile:: dsp/buffer.hpp
   :project: libYSE

.. doxygenfile:: dsp/drawableBuffer.hpp
   :project: libYSE

.. doxygenfile:: dsp/fileBuffer.hpp
   :project: libYSE

.. doxygenfile:: dsp/wavetable.hpp
   :project: libYSE

.. doxygenfile:: dsp/sample_functions.hpp
   :project: libYSE

Math
----

.. doxygenfile:: dsp/math.hpp
   :project: libYSE

.. doxygenfile:: dsp/operators/delta.hpp
   :project: libYSE

.. doxygenfile:: dsp/operators/degrade.hpp
   :project: libYSE

.. doxygenfile:: dsp/operators/downsample.hpp
   :project: libYSE

.. doxygenfile:: dsp/operators/overdrive.hpp
   :project: libYSE

.. doxygenfile:: dsp/operators/fold.hpp
   :project: libYSE

.. doxygenfile:: dsp/operators/scale.hpp
   :project: libYSE

Generators
----------

.. doxygenfile:: dsp/oscillators.hpp
   :project: libYSE

.. doxygenfile:: dsp/generators/phasor.hpp
   :project: libYSE

.. doxygenfile:: dsp/generators/cycle.hpp
   :project: libYSE

.. doxygenfile:: dsp/generators/pinkNoise.hpp
   :project: libYSE

.. doxygenfile:: dsp/generators/bandLimitedRandom.hpp
   :project: libYSE

.. doxygenfile:: dsp/generators/bandLimitedOscillators.hpp
   :project: libYSE

.. doxygenfile:: dsp/generators/click.hpp
   :project: libYSE

.. doxygenfile:: dsp/generators/kink.hpp
   :project: libYSE

.. doxygenfile:: dsp/generators/oscillatorBank.hpp
   :project: libYSE

.. doxygenfile:: dsp/generators/phaseShapes.hpp
   :project: libYSE

.. doxygenfile:: dsp/generators/sampleCounter.hpp
   :project: libYSE

.. doxygenfile:: dsp/generators/techno.hpp
   :project: libYSE

.. doxygenfile:: dsp/generators/updown.hpp
   :project: libYSE

.. doxygenfile:: dsp/generators/shapeEnvelope.hpp
   :project: libYSE

.. doxygenfile:: dsp/generators/triggeredRamp.hpp
   :project: libYSE

.. doxygenfile:: dsp/generators/phasorRate.hpp
   :project: libYSE

.. doxygenfile:: dsp/generators/phasorSubdiv.hpp
   :project: libYSE

.. doxygenfile:: dsp/generators/phasorStepDiv.hpp
   :project: libYSE

.. doxygenfile:: dsp/generators/phasorSwing.hpp
   :project: libYSE

.. doxygenfile:: dsp/generators/phasorTwist.hpp
   :project: libYSE

.. doxygenfile:: dsp/generators/phasorWhat.hpp
   :project: libYSE

.. doxygenfile:: dsp/generators/phasorWhere.hpp
   :project: libYSE

.. doxygenfile:: dsp/generators/stepCounter.hpp
   :project: libYSE

.. doxygenfile:: dsp/generators/stepFunction.hpp
   :project: libYSE

.. doxygenfile:: dsp/generators/sampleHoldMemory.hpp
   :project: libYSE

.. doxygenfile:: dsp/generators/signalStash.hpp
   :project: libYSE

.. doxygenfile:: dsp/generators/snowfall.hpp
   :project: libYSE

.. doxygenfile:: dsp/generators/pulseTrain.hpp
   :project: libYSE

.. doxygenfile:: dsp/generators/adsr.hpp
   :project: libYSE

.. doxygenfile:: dsp/generators/curveSegments.hpp
   :project: libYSE

.. doxygenfile:: dsp/generators/lineSegments.hpp
   :project: libYSE

.. doxygenfile:: dsp/generators/zigzag.hpp
   :project: libYSE

.. doxygenfile:: dsp/generators/eventSequencer.hpp
   :project: libYSE

.. doxygenfile:: dsp/generators/clockFollower.hpp
   :project: libYSE

.. doxygenfile:: dsp/generators/beatSpan.hpp
   :project: libYSE

.. doxygenfile:: dsp/lfo.hpp
   :project: libYSE

Envelopes and ramps
-------------------

.. doxygenfile:: dsp/envelope.hpp
   :project: libYSE

.. doxygenfile:: dsp/ADSRenvelope.hpp
   :project: libYSE

.. doxygenfile:: dsp/ramp.hpp
   :project: libYSE

Analysis
--------

.. doxygenfile:: dsp/analysis/direction.hpp
   :project: libYSE

.. doxygenfile:: dsp/analysis/edgeDetector.hpp
   :project: libYSE

.. doxygenfile:: dsp/analysis/schmittTrigger.hpp
   :project: libYSE

.. doxygenfile:: dsp/analysis/spikeDetector.hpp
   :project: libYSE

.. doxygenfile:: dsp/analysis/zeroCrossing.hpp
   :project: libYSE

.. doxygenfile:: dsp/analysis/peakTracker.hpp
   :project: libYSE

.. doxygenfile:: dsp/analysis/movingAverage.hpp
   :project: libYSE

.. doxygenfile:: dsp/analysis/minMax.hpp
   :project: libYSE

.. doxygenfile:: dsp/analysis/sampleCapture.hpp
   :project: libYSE

.. doxygenfile:: dsp/analysis/loudnessMeter.hpp
   :project: libYSE

The peak or RMS level of a signal for a meter, with ballistics, a peak hold
and a clip indicator: what ``~meter``, ``~levelmeter`` and ``~gridmeter``
measure.

.. doxygenfile:: dsp/analysis/levelMeter.hpp
   :project: libYSE

An oscilloscope's traces of a signal or an X/Y pair, with samples-per-point
decimation, a delay and a trigger, handed to a reader on another thread
through a triple buffer: what ``~scope`` captures.

.. doxygenfile:: dsp/analysis/scopeCapture.hpp
   :project: libYSE

The amplitude or phase spectrum of a signal, or of a ``~pfft``'s spectral
frames, every update interval, handed to a reader on another thread through
a triple buffer: what ``~spectroscope`` measures.

.. doxygenfile:: dsp/analysis/spectrumAnalyzer.hpp
   :project: libYSE

The values and axes of a 2-D plot of one or more subplots, set by lists or
taken from frames of a signal or a region of a sample buffer, handed to a
reader on another thread through a triple buffer: what ``~plot`` holds.

.. doxygenfile:: dsp/analysis/plotData.hpp
   :project: libYSE

The fundamental frequency of a monophonic signal after the YIN method, with
its peak amplitude and the onsets of new notes: what ``~fzero`` does.

.. doxygenfile:: dsp/analysis/pitchTracker.hpp
   :project: libYSE

Filters
-------

.. doxygenfile:: dsp/filters.hpp
   :project: libYSE

.. doxygenfile:: dsp/filters/onePole.hpp
   :project: libYSE

.. doxygenfile:: dsp/filters/lores.hpp
   :project: libYSE

.. doxygenfile:: dsp/filters/reson.hpp
   :project: libYSE

.. doxygenfile:: dsp/filters/stateVariableFilter.hpp
   :project: libYSE

.. doxygenfile:: dsp/filters/allpass.hpp
   :project: libYSE

.. doxygenfile:: dsp/filters/biquadCascade.hpp
   :project: libYSE

.. doxygenfile:: dsp/filters/filterCoefficients.hpp
   :project: libYSE

.. doxygenfile:: dsp/filters/crossover.hpp
   :project: libYSE

.. doxygenfile:: dsp/filters/filterBank.hpp
   :project: libYSE

.. doxygenfile:: dsp/filters/phaseShift.hpp
   :project: libYSE

.. doxygenfile:: dsp/filters/slide.hpp
   :project: libYSE

.. doxygenfile:: dsp/filters/rampSmooth.hpp
   :project: libYSE

.. doxygenfile:: dsp/filters/slewLimiter.hpp
   :project: libYSE

.. doxygenfile:: dsp/filters/filterDesign.hpp
   :project: libYSE

.. doxygenfile:: dsp/filters/filterResponse.hpp
   :project: libYSE

.. doxygenfile:: dsp/filters/filterBands.hpp
   :project: libYSE

.. doxygenfile:: dsp/filters/zPlane.hpp
   :project: libYSE

The resonant ladder low-pass that ``SYNTH::vaVoice`` uses also works on its
own in any DSP path:

.. doxygenfile:: dsp/ladderFilter.hpp
   :project: libYSE

Delay
-----

.. doxygenfile:: dsp/delay.hpp
   :project: libYSE

.. doxygenfile:: dsp/delay/delayLine.hpp
   :project: libYSE

.. doxygenfile:: dsp/delay/delayTap.hpp
   :project: libYSE

.. doxygenfile:: dsp/delay/comb.hpp
   :project: libYSE

.. doxygenfile:: dsp/delay/sampleDelay.hpp
   :project: libYSE

A real-time pitch shifter: a granular dual delay line whose grains are
aligned in phase, for low latency, or a phase vocoder built from
``DSP::stft`` and ``DSP::spectralPitchShift``, for a steadier tone at more
latency. What ``~pitchshift`` wraps; the class notes give the latency of
every mode and what each one costs in quality.

.. doxygenfile:: dsp/delay/pitchShifter.hpp
   :project: libYSE

Pitch correction toward a scale or a set of notes, built from
``DSP::pitchTracker`` and ``DSP::pitchShifter``: what ``~retune`` wraps.
``DSP::tuning`` holds the scale (degrees in cents on a base note, any
subset enabled), and can be filled from a ``YSE::scale``.

.. doxygenfile:: dsp/delay/retune.hpp
   :project: libYSE

Dynamics
--------

.. doxygenfile:: dsp/dynamics/lookaheadLimiter.hpp
   :project: libYSE

.. doxygenfile:: dsp/dynamics/multibandCompressor.hpp
   :project: libYSE

The mixer fader that ``~gain`` wraps: a slider position on a log taper, ramped.

.. doxygenfile:: dsp/dynamics/gainFader.hpp
   :project: libYSE

The compressor that ``~comp`` wraps, :cpp:class:`YSE::DSP::MODULES::compressor`,
is documented with the effects on :doc:`effects`.

Sample buffers
--------------

Shared, named, multichannel sample buffers: what ``~buffer`` holds and the
sampling objects play. Change one from a control thread, read it from the
audio thread through a ``sampleBufferReader``, and write its samples in place
at audio rate through a ``sampleBufferWriter``.

.. doxygenfile:: dsp/sampling/sampleBuffer.hpp
   :project: libYSE

Reading and writing single samples by index, on a pinned
``sampleWriteView``: what ``~peek`` and ``~poke`` do. Reading a pinned
``sampleView`` at signal-rate indexes without interpolation: what ``~index``
does.

.. doxygenfile:: dsp/sampling/sampleAccess.hpp
   :project: libYSE

A sample buffer read as a table of values at signal rate, with
interpolation, out-of-range rules, input and output scaling, stepping and
weighted random draws: what ``~table`` does.

.. doxygenfile:: dsp/sampling/tableLookup.hpp
   :project: libYSE

Part of a sample buffer used as a transfer function: a signal from -1 to 1
looks up a value in it, for waveshaping with a drawn curve: what
``~lookup`` does.

.. doxygenfile:: dsp/sampling/transferLookup.hpp
   :project: libYSE

A region of a sample buffer read as one cycle of a waveform at a phase from 0
to 1, with seven interpolation modes, and optionally cut into rows that a
second phase crossfades between: what ``~wave`` and ``~2d.wave`` do.

.. doxygenfile:: dsp/sampling/waveReader.hpp
   :project: libYSE

A FIR filter whose taps are part of a sample buffer: the input convolved with
up to 4096 of its samples, for custom filters and short impulse responses:
what ``~buffir`` does.

.. doxygenfile:: dsp/sampling/firFilter.hpp
   :project: libYSE

A capture buffer that records its input all the time and, on a trigger,
copies the most recent samples into a fragment one or more phases replay,
with chances to repeat, drop out and vary the gain: what ``~stutter`` does.

.. doxygenfile:: dsp/sampling/stutter.hpp
   :project: libYSE

Signals recorded into a sample buffer between loop points, once or in a loop,
with append and a sync output: what ``~record`` does. The loop points are a
``sampleRegion``, a span in frames made from milliseconds for each block,
which players that loop a region use as well.

.. doxygenfile:: dsp/sampling/bufferRecorder.hpp
   :project: libYSE

.. doxygenfile:: dsp/sampling/sampleRegion.hpp
   :project: libYSE

A sample buffer played from a start to an end point at a speed, backward,
in a loop with a crossfade across the jump, or at the positions of a
signal: what ``~play`` does. It reads with ``sampleView::cubic`` and loops
through ``loopCrossfade``, which other players that crossfade a loop use as
well.

.. doxygenfile:: dsp/sampling/bufferPlayer.hpp
   :project: libYSE

.. doxygenfile:: dsp/sampling/loopCrossfade.hpp
   :project: libYSE

A sample buffer played at the speed of a signal, forward or backward,
between loop points it jumps across (with a crossfade) or swings over, with
a sync output: what ``~groove`` does. It can also follow a phase instead of a
speed, reading the loop where a 0 to 1 ramp says: what ``~phasegroove`` does.
Its outputs, and the read that mixes the buffer's channels into them, are a
``playerOutputs``, which every buffer player shares.

.. doxygenfile:: dsp/sampling/groovePlayer.hpp
   :project: libYSE

.. doxygenfile:: dsp/sampling/playerOutputs.hpp
   :project: libYSE

A whole sample buffer made longer or shorter and higher or lower, each on its
own, offline on a worker thread: what ``~stretch`` does. A phase vocoder with
identity phase locking on ``stftAnalysis`` / ``stftSynthesis``, onsets copied
rather than stretched in the rhythmic modes, the pitch shifted by resampling,
and an optional pitch correction through ``retune``. The result is written
back with ``sampleBuffer::setContents``, one change that readers never see
half made.

.. doxygenfile:: dsp/sampling/timeStretch.hpp
   :project: libYSE

The view of a waveform display of a sample buffer: the part shown, the
selection, and the mouse gestures that select, scroll, zoom or draw into the
samples. What ``~waveform`` holds; the host draws the samples with
``sampleBuffer::peaks``, the smallest and largest sample per pixel column.

.. doxygenfile:: dsp/sampling/waveformView.hpp
   :project: libYSE

A numbered collection of sample buffers sharing a name, ``kit.1``,
``kit.2`` and so on, filled from files or a whole folder: what
``~polybuffer`` holds. Each buffer is an ordinary named ``sampleBuffer``.

.. doxygenfile:: dsp/sampling/sampleBufferSet.hpp
   :project: libYSE

Sound files played from disk without loading them, through the engine's own
streaming, with numbered cues shared by name: what ``~sfplay`` and
``~sflist`` do.

.. doxygenfile:: dsp/sampling/fileStreamPlayer.hpp
   :project: libYSE

Routing
-------

Signal routing over numbered channels, switched hard or with a crossfade:
what ``~gate`` and ``~selector`` wrap.

.. doxygenfile:: dsp/routing/signalSelector.hpp
   :project: libYSE

A mixing matrix with a ramped gain at every crosspoint: what ``~matrix``
wraps.

.. doxygenfile:: dsp/routing/mixMatrix.hpp
   :project: libYSE

Named signal buses: audio sent and received by name, without a cord, what
``~send`` and ``~receive`` wrap.

.. doxygenfile:: dsp/routing/signalBus.hpp
   :project: libYSE

Spectral
--------

Fourier transforms of a stream, frame by frame: a frame of input samples
transformed at the exact sample it completes, whatever the block, and put
out over the next frame one bin per sample, with a sync signal giving the
bin. What ``~fft`` and ``~ifft`` wrap. The frame schedule, the size, interval
and offset of the frames along the stream, is a ``frameClock`` of its own,
for other frame-based processes.

.. doxygenfile:: dsp/spectral/streamingFft.hpp
   :project: libYSE

.. doxygenfile:: dsp/spectral/frameClock.hpp
   :project: libYSE

The short-time Fourier transform: a stream cut into overlapping windowed
frames, each handed to a callback that may change its bins, transformed back
and overlap-added, ``size`` samples later. The analysis and synthesis halves
can also be driven on their own. What ``~pfft`` runs its subpatch on.

.. doxygenfile:: dsp/spectral/stft.hpp
   :project: libYSE

Successive frames, such as spectra, smoothed index by index with a linear
ramp, a logarithmic slide or a limited change per frame: what ``~vectral``
wraps.

.. doxygenfile:: dsp/spectral/vectral.hpp
   :project: libYSE

The change and the running sum of every bin across successive frames, the
phase deviation and running phase of a phase vocoder: what ``~framedelta``
and ``~frameaccum`` wrap. They work on a ``stftFrame`` run of bins, so the
same object serves a ``DSP::stft`` callback and a ``~pfft`` pass.

.. doxygenfile:: dsp/spectral/frameDelta.hpp
   :project: libYSE

The running average of every bin over the last frames, and an average
along each frame that never reaches into the next, centred on each value for
a whole frame or trailing it for a frame that streams in: what
``~frameaverage`` and ``~framesmooth`` wrap. They work on a ``stftFrame``
run of bins as well, or cut a stream into frames of a given size.

.. doxygenfile:: dsp/spectral/frameAverage.hpp
   :project: libYSE

A stored frame put out bin by bin, and the last whole frame of a stream kept
to be read as values: what ``~frame`` and ``~framesnap`` wrap to turn lists
into frames and frames into lists. Both work on a ``stftFrame`` run of bins,
or take a block as a frame.

.. doxygenfile:: dsp/spectral/frameList.hpp
   :project: libYSE

A pitch shifter for whole spectral frames: every peak moved, with the bins
around it, to its transposed frequency, and its phase turned so the partial
stays continuous from frame to frame. What ``~gizmo`` wraps; it takes the
whole frame a ``DSP::stft`` callback gets, so it shifts the pitch of a
stream without a patcher.

.. doxygenfile:: dsp/spectral/spectralPitchShift.hpp
   :project: libYSE

A frequency shifter for whole spectral frames: every bin moved by the same
number of Hz, and the frame's phase turned so the moved partials stay
continuous from frame to frame. What ``~fbinshift`` wraps; like
``spectralPitchShift`` it takes the whole frame a ``DSP::stft`` callback
gets, so it shifts a stream without a patcher.

.. doxygenfile:: dsp/spectral/spectralFreqShift.hpp
   :project: libYSE

Filter primitives
-----------------

.. doxygenfile:: dsp/rawFilters.hpp
   :project: libYSE

Interpolation
-------------

.. doxygenfile:: dsp/interpolate4.hpp
   :project: libYSE

DSP base
--------

.. doxygenfile:: dsp/dspObject.hpp
   :project: libYSE

Per-channel state helper
------------------------

A fan-out helper for module authors: hold one ``State`` struct per channel so
a multichannel ``dspObject`` keeps each channel's history independent.

.. doxygenfile:: dsp/perChannel.hpp
   :project: libYSE
