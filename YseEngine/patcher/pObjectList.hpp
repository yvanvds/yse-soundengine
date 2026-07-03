#pragma once

#define DEFOBJ(name, tag) static constexpr char const* name = tag

namespace YSE {
  /**
   *  @brief Compile-time string identifiers for patcher object types.
   *
   *  Pass any of these constants to ``patcher::CreateObject`` instead of the
   *  raw string literal. The ``~`` prefix marks DSP / audio-rate objects,
   *  ``.`` marks control-rate objects.
   *
   *  - DSP generators: ``D_SINE``, ``D_SAW``, ``D_NOISE``.
   *  - DSP math: ``D_ADD``, ``D_MULTIPLY``, ``D_CLIP``.
   *  - DSP filters: ``D_LOWPASS``, ``D_HIGHPASS``, ``D_BANDPASS``, ``D_VCF``.
   *  - I/O: ``D_DAC``, ``D_ADC``, ``D_LINE``, ``D_OUT``.
   *  - Control: ``G_INT``, ``G_FLOAT``, ``G_SLIDER``, ``G_METRO``, ``G_RANDOM``.
   *  - Messaging: ``G_SEND``, ``G_RECEIVE``, ``G_ROUTE``, ``G_GATE``, ``G_SWITCH``.
   *  - MIDI: ``M_OUT``, ``M_NOTEON``, ``M_NOTEOFF``, ``M_CONTROL``.
   *  - Conversion: ``MIDITOFREQUENCY``, ``FREQUENCYTOMIDI``.
   */
  struct API OBJ {
    DEFOBJ(PATCHER, "patcher");

    DEFOBJ(D_DAC, "~dac");
    DEFOBJ(D_ADC, "~adc");
    DEFOBJ(G_RECEIVE, ".r");
    DEFOBJ(G_SEND, ".s");

    DEFOBJ(D_OUT, "~out");
    DEFOBJ(D_LINE, "~line");

    DEFOBJ(D_SINE, "~sine");
    DEFOBJ(D_SAW, "~saw");
    DEFOBJ(D_NOISE, "~noise");

    DEFOBJ(G_INT, ".i");
    DEFOBJ(G_FLOAT, ".f");
    DEFOBJ(G_SLIDER, ".slider");
    DEFOBJ(G_BUTTON, ".b");
    DEFOBJ(G_TOGGLE, ".t");
    DEFOBJ(G_MESSAGE, ".m");
    DEFOBJ(G_LIST, ".l");
    DEFOBJ(G_TEXT, ".text");
    DEFOBJ(G_COUNTER, ".counter");
    DEFOBJ(G_SWITCH, ".switch");
    DEFOBJ(G_GATE, ".gate");
    DEFOBJ(G_ROUTE, ".route");

    DEFOBJ(G_ADD, ".+");
    DEFOBJ(G_SUBSTRACT, ".-");
    DEFOBJ(G_MULTIPLY, ".*");
    DEFOBJ(G_DIVIDE, "./");

    DEFOBJ(G_RANDOM, ".random");
    DEFOBJ(G_METRO, ".metro");

    DEFOBJ(D_ADD, "~+");
    DEFOBJ(D_SUBSTRACT, "~-");
    DEFOBJ(D_MULTIPLY, "~*");
    DEFOBJ(D_DIVIDE, "~/");

    DEFOBJ(D_CLIP, "~clip");

    DEFOBJ(MIDITOFREQUENCY, ".mtof");
    DEFOBJ(FREQUENCYTOMIDI, ".ftom");

    DEFOBJ(D_LOWPASS, "~lp");
    DEFOBJ(D_HIGHPASS, "~hp");
    DEFOBJ(D_BANDPASS, "~bp");
    DEFOBJ(D_VCF, "~vcf");

    DEFOBJ(M_OUT, ".midiout");
    DEFOBJ(M_NOTEON, ".noteon");
    DEFOBJ(M_NOTEOFF, ".noteoff");
    DEFOBJ(M_CONTROL, ".controlchange");
    DEFOBJ(M_POLYPRESS, ".polypressure");
    DEFOBJ(M_CHANPRESS, ".channelpressure");
    DEFOBJ(M_PROGCHANGE, ".programchange");
  };
} // namespace YSE
