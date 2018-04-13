#pragma once

#define DEFOBJ(name, tag) static constexpr char const * name = tag

namespace YSE {
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
  };
}
