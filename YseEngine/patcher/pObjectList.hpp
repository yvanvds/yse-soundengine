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
   *  - Control math: ``G_ADD``, ``G_DIVIDE``, ``G_REVERSESUBSTRACT``,
   *    ``G_REVERSEDIVIDE``, ``G_MODULO``, ``G_INTDIVIDE``.
   *  - Operand order: ``G_SWAP``.
   *  - Elementary math: ``G_ABS``, ``G_SQRT``, ``G_POW``, ``G_ROUND``.
   *  - Range mapping: ``G_SCALE``, ``G_ZMAP``, ``G_LINEDRIVE``.
   *  - Range limiting: ``G_CLIP``, ``G_PONG``.
   *  - Range routing: ``G_SPLIT``.
   *  - Smoothing: ``G_SLIDE``.
   *  - Expressions: ``G_EXPR``, ``G_VEXPR``.
   *  - Trigonometry / hyperbolics: ``G_SIN``, ``G_COS``, ``G_TAN``,
   *    ``G_ASIN``, ``G_ACOS``, ``G_ATAN``, ``G_ATAN2``, ``G_SINH``,
   *    ``G_COSH``, ``G_TANH``, ``G_ASINH``, ``G_ACOSH``, ``G_ATANH``.
   *  - Comparison / logic: ``G_EQUAL``, ``G_LESS``, ``G_LOGICALAND``, ...
   *  - Running comparison: ``G_MAXIMUM``, ``G_MINIMUM``.
   *  - Running extremes: ``G_PEAK``, ``G_TROUGH``.
   *  - Threshold crossing: ``G_PAST``.
   *  - Change detection: ``G_CHANGE``.
   *  - Zero-crossing edges: ``G_TOGEDGE``.
   *  - Bitwise: ``G_BITAND``, ``G_BITOR``, ``G_SHIFTLEFT``, ``G_SHIFTRIGHT``.
   *  - DSP filters: ``D_LOWPASS``, ``D_HIGHPASS``, ``D_BANDPASS``, ``D_VCF``.
   *  - I/O: ``D_DAC``, ``D_ADC``, ``D_LINE``.
   *  - Control: ``G_INT``, ``G_FLOAT``, ``G_SLIDER``, ``G_METRO``, ``G_RANDOM``.
   *  - Registers: ``G_COUNTER``, ``G_ACCUM``.
   *  - Randomness: ``G_RANDOM``, ``G_DRUNK``, ``G_URN``, ``G_DECIDE``,
   *    ``G_PROB``, ``G_ANAL``, ``G_HISTO``.
   *  - Statistics: ``G_ANAL``, ``G_HISTO``, ``G_MEAN``.
   *  - Messaging: ``G_SEND``, ``G_RECEIVE``, ``G_ROUTE``, ``G_SEL``,
   *    ``G_TRIGGER``, ``G_BANGBANG``, ``G_ONEBANG``, ``G_NEXT``, ``G_MATCH``,
   *    ``G_BONDO``, ``G_BUDDY``, ``G_CYCLE``, ``G_BUCKET``,
   *    ``G_GATE``, ``G_SWITCH``, ``G_IF``, ``G_REGEXP``.
   *  - Iteration: ``G_UZI``.
   *  - MIDI: ``M_OUT``, ``M_NOTEON``, ``M_NOTEOFF``, ``M_CONTROL``.
   *  - Conversion: ``MIDITOFREQUENCY``, ``FREQUENCYTOMIDI``, ``G_ATODB``,
   *    ``G_DBTOA``, ``G_CARTOPOL``, ``G_POLTOCAR``.
   */
  struct API OBJ {
    DEFOBJ(PATCHER, "patcher");

    DEFOBJ(D_DAC, "~dac");
    DEFOBJ(D_ADC, "~adc");
    DEFOBJ(G_RECEIVE, ".r");
    DEFOBJ(G_SEND, ".s");

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
    DEFOBJ(G_ACCUM, ".accum");
    DEFOBJ(G_SWITCH, ".switch");
    DEFOBJ(G_GATE, ".gate");
    DEFOBJ(G_ROUTE, ".route");
    DEFOBJ(G_SEL, ".sel");
    DEFOBJ(G_TRIGGER, ".trigger");
    DEFOBJ(G_BANGBANG, ".bangbang");
    DEFOBJ(G_ONEBANG, ".onebang");
    DEFOBJ(G_NEXT, ".next");
    DEFOBJ(G_MATCH, ".match");
    DEFOBJ(G_UZI, ".uzi");
    DEFOBJ(G_BONDO, ".bondo");
    DEFOBJ(G_BUDDY, ".buddy");
    DEFOBJ(G_CYCLE, ".cycle");
    DEFOBJ(G_BUCKET, ".bucket");
    DEFOBJ(G_IF, ".if");
    DEFOBJ(G_REGEXP, ".regexp");

    DEFOBJ(G_ADD, ".+");
    DEFOBJ(G_SUBSTRACT, ".-");
    DEFOBJ(G_MULTIPLY, ".*");
    DEFOBJ(G_DIVIDE, "./");

    DEFOBJ(G_REVERSESUBSTRACT, ".!-");
    DEFOBJ(G_REVERSEDIVIDE, ".!/");
    DEFOBJ(G_SWAP, ".swap");
    DEFOBJ(G_MODULO, ".%");
    DEFOBJ(G_INTDIVIDE, ".div");

    DEFOBJ(G_SCALE, ".scale");
    DEFOBJ(G_ZMAP, ".zmap");
    DEFOBJ(G_LINEDRIVE, ".linedrive");

    DEFOBJ(G_CLIP, ".clip");
    DEFOBJ(G_PONG, ".pong");

    DEFOBJ(G_SPLIT, ".split");

    DEFOBJ(G_SLIDE, ".slide");

    DEFOBJ(G_EXPR, ".expr");
    DEFOBJ(G_VEXPR, ".vexpr");

    DEFOBJ(G_ABS, ".abs");
    DEFOBJ(G_SQRT, ".sqrt");
    DEFOBJ(G_POW, ".pow");
    DEFOBJ(G_ROUND, ".round");

    DEFOBJ(G_SIN, ".sin");
    DEFOBJ(G_COS, ".cos");
    DEFOBJ(G_TAN, ".tan");
    DEFOBJ(G_ASIN, ".asin");
    DEFOBJ(G_ACOS, ".acos");
    DEFOBJ(G_ATAN, ".atan");
    DEFOBJ(G_ATAN2, ".atan2");
    DEFOBJ(G_SINH, ".sinh");
    DEFOBJ(G_COSH, ".cosh");
    DEFOBJ(G_TANH, ".tanh");
    DEFOBJ(G_ASINH, ".asinh");
    DEFOBJ(G_ACOSH, ".acosh");
    DEFOBJ(G_ATANH, ".atanh");

    DEFOBJ(G_EQUAL, ".==");
    DEFOBJ(G_NOTEQUAL, ".!=");
    DEFOBJ(G_LESS, ".<");
    DEFOBJ(G_LESSEQUAL, ".<=");
    DEFOBJ(G_GREATER, ".>");
    DEFOBJ(G_GREATEREQUAL, ".>=");
    DEFOBJ(G_LOGICALAND, ".&&");
    DEFOBJ(G_LOGICALOR, ".||");

    DEFOBJ(G_MAXIMUM, ".maximum");
    DEFOBJ(G_MINIMUM, ".minimum");

    DEFOBJ(G_PEAK, ".peak");
    DEFOBJ(G_TROUGH, ".trough");

    DEFOBJ(G_PAST, ".past");

    DEFOBJ(G_CHANGE, ".change");
    DEFOBJ(G_TOGEDGE, ".togedge");

    DEFOBJ(G_BITAND, ".&");
    DEFOBJ(G_BITOR, ".|");
    DEFOBJ(G_SHIFTLEFT, ".<<");
    DEFOBJ(G_SHIFTRIGHT, ".>>");

    DEFOBJ(G_RANDOM, ".random");
    DEFOBJ(G_DRUNK, ".drunk");
    DEFOBJ(G_URN, ".urn");
    DEFOBJ(G_DECIDE, ".decide");
    DEFOBJ(G_PROB, ".prob");
    DEFOBJ(G_ANAL, ".anal");
    DEFOBJ(G_HISTO, ".histo");
    DEFOBJ(G_MEAN, ".mean");
    DEFOBJ(G_METRO, ".metro");

    DEFOBJ(D_ADD, "~+");
    DEFOBJ(D_SUBSTRACT, "~-");
    DEFOBJ(D_MULTIPLY, "~*");
    DEFOBJ(D_DIVIDE, "~/");

    DEFOBJ(D_CLIP, "~clip");

    DEFOBJ(MIDITOFREQUENCY, ".mtof");
    DEFOBJ(FREQUENCYTOMIDI, ".ftom");

    DEFOBJ(G_ATODB, ".atodb");
    DEFOBJ(G_DBTOA, ".dbtoa");
    DEFOBJ(G_CARTOPOL, ".cartopol");
    DEFOBJ(G_POLTOCAR, ".poltocar");

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
