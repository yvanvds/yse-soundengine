#pragma once

namespace YSE {
  struct API OBJ {
    static constexpr char const * PATCHER = "patcher";

    static constexpr char const * D_DAC = "~dac";
    static constexpr char const * D_ADC = "~adc";

    static constexpr char const * D_OUT = "~out";
    static constexpr char const * D_LINE = "~line";

    static constexpr char const * D_SINE = "~sine";

    static constexpr char const * G_INT = ".i";
    static constexpr char const * G_FLOAT = ".f";
    static constexpr char const * G_SLIDER = ".slider";
    static constexpr char const * G_BUTTON = ".b";
    static constexpr char const * G_TOGGLE = ".t";

    static constexpr char const * G_ADD = ".+";
    static constexpr char const * G_SUBSTRACT = ".-";
    static constexpr char const * G_MULTIPLY = ".*";
    static constexpr char const * G_DIVIDE = "./";

    static constexpr char const * G_RANDOM = ".random";
    static constexpr char const * G_METRO = ".metro";

    static constexpr char const * D_ADD = "~+";
    static constexpr char const * D_SUBSTRACT = "~-";
    static constexpr char const * D_MULTIPLY = "~*";
    static constexpr char const * D_DIVIDE = "~/";

    static constexpr char const * D_CLIP = "~clip";

    static constexpr char const * MIDITOFREQUENCY = ".mtof";
    static constexpr char const * FREQUENCYTOMIDI = ".ftom";

    static constexpr char const * D_LOWPASS = "~lp";
    static constexpr char const * D_HIGHPASS = "~hp";
    static constexpr char const * D_BANDPASS = "~bp";
  };
}
