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

.. doxygenfile:: dsp/generators/zigzag.hpp
   :project: libYSE

.. doxygenfile:: dsp/generators/eventSequencer.hpp
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

Dynamics
--------

.. doxygenfile:: dsp/dynamics/lookaheadLimiter.hpp
   :project: libYSE

.. doxygenfile:: dsp/dynamics/multibandCompressor.hpp
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

Successive frames, such as spectra, smoothed index by index with a linear
ramp, a logarithmic slide or a limited change per frame: what ``~vectral``
wraps.

.. doxygenfile:: dsp/spectral/vectral.hpp
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
