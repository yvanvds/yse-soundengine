#pragma once

namespace YSE {
  struct API OBJ {
    static constexpr char const * PATCHER = "patcher";

    static constexpr char const * D_OUT = "~out";
    static constexpr char const * D_LINE = "~line";

    static constexpr char const * D_SINE = "~sine";

    static constexpr char const * D_ADD = "~+";
    static constexpr char const * D_SUBSTRACT = "~-";
    static constexpr char const * D_MULTIPLIER = "~*";
    static constexpr char const * D_DIVIDE = "~/";

    static constexpr char const * MIDITOFREQUENCY = "mtof";
    static constexpr char const * FREQUENCYTOMIDI = "ftom";

    static constexpr char const * D_LOWPASS = "~lp";
    static constexpr char const * D_HIGHPASS = "~hp";
    static constexpr char const * D_BANDPASS = "~bp";
  };
}
