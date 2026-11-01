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

Sample buffers
--------------

Shared, named, multichannel sample buffers: what ``~buffer`` holds and the
sampling objects play. Change one from a control thread, read it from the
audio thread through a ``sampleBufferReader``.

.. doxygenfile:: dsp/sampling/sampleBuffer.hpp
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
