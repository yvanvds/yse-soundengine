.. Notes for the Filters category. Format: _data/patcher_notes/README.md.

.. patcher-object:: ~lp

The cutoff argument defaults to 0 Hz, where the filter lets nothing through:
give it a cutoff (``~lp 800``). The cutoff is an approximation that holds
well at low frequencies. From about a sixth of the sample rate up (about
7 kHz at 44.1 kHz), the filter is fully open and passes the input unchanged.

.. patcher-object:: ~hp

At 0 Hz, the default, the input passes unchanged. As for ``~lp``, the cutoff
is an approximation that holds well at low frequencies; from about a sixth of
the sample rate up, the output is the difference between successive samples.

.. patcher-object:: ~bp

The center frequency is in Hz; below 0.001 Hz, including the default 0, the
filter uses 10 Hz. A higher ``Q`` makes the band narrower; the default is 1.
A tone at the center frequency comes out at the input's level for every
``Q``. A ``Q`` low enough to open the band fully (below 0.001, or below about
``2 * pi * frequency / samplerate``) turns filtering off and passes the input
unchanged.

.. patcher-object:: ~vcf

Unlike ``~bp``, the center frequency comes in as a signal, in Hz, so it can
be swept smoothly or modulated at audio rate. Feed it from ``~line`` or an
oscillator scaled to a frequency range.

``sharpness`` sets how narrow the band is: higher is narrower. At 0, the
default, the left outlet passes the input unchanged and the right outlet is
silent.

Nothing comes out unless both the audio input and the center frequency are
connected.
