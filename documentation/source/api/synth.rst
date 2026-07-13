Synthesis
=========

The synth subsystem turns libYSE from a sample player into a polyphonic
instrument host. A :cpp:class:`YSE::synth` owns a pool of voices, note
allocation, voice stealing and full keyboard state (pedals, controllers,
pitch wheel, aftertouch); a voice — a :cpp:class:`YSE::SYNTH::dspVoice`
subclass — owns only what a single note sounds like. The synth is rendered
behind an ordinary positioned :cpp:class:`YSE::sound`, so everything the
spatial engine already does (3D panning, channels, reverb) applies to it.

See the tutorials for worked, compilable walk-throughs:
:doc:`/tutorials/06_first_synth`,
:doc:`/tutorials/07_custom_dspvoice`,
:doc:`/tutorials/08_instruments`, and
:doc:`/tutorials/10_per_note_3d`.

The synth
---------

.. doxygenfile:: synth/synth.hpp
   :project: libYSE

.. doxygenfile:: synth/synthInterface.hpp
   :project: libYSE

The voice model
---------------

Derive from :cpp:class:`YSE::SYNTH::dspVoice` to define a custom voice, or
use one of the built-in voices below.

.. doxygenfile:: synth/dspVoice.hpp
   :project: libYSE

.. doxygenfile:: synth/sineVoice.hpp
   :project: libYSE

Virtual-analog voice
--------------------

A multi-oscillator virtual-analog + wavetable voice with a Moog-style ladder
filter and a live-editable shared patch.

.. doxygenfile:: synth/vaVoice.hpp
   :project: libYSE

SFZ sampler voice
-----------------

Loads an SFZ instrument (region map + resident PCM) and plays it as a
multi-layer sampler voice. See :doc:`/tutorials/08_instruments`.

.. doxygenfile:: synth/samplerVoice.hpp
   :project: libYSE

FM voice and DX7 banks
----------------------

A DX7-class 6-operator FM voice driven by an :cpp:struct:`YSE::SYNTH::fmPatch`,
plus the SysEx importer that fills patches from a vintage DX7 bank dump.

.. doxygenfile:: dsp/fm/fmVoice.hpp
   :project: libYSE

.. doxygenfile:: dsp/fm/fmPatch.hpp
   :project: libYSE

.. doxygenfile:: dsp/fm/dx7Sysex.hpp
   :project: libYSE

Per-note 3D positioning
-----------------------

Attach a :cpp:class:`YSE::SYNTH::positionHandler` to give every voice its own
3D position and movement — the basis of the "swarm" effect. Steer the whole
swarm live with :cpp:func:`YSE::synth::handlerParam`, or place a single note
imperatively with :cpp:func:`YSE::synth::notePosition`. See
:doc:`/tutorials/10_per_note_3d`.

.. doxygenfile:: synth/positionHandler.hpp
   :project: libYSE

.. doxygenfile:: synth/positionHandlers.hpp
   :project: libYSE
