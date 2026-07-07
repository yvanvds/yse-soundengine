/*
  ==============================================================================

    fmPatch.cpp
    Serialisation + built-in voices for the DX7 FM patch — see fmPatch.hpp and
    issue #176.

  ==============================================================================
*/

#include "fmPatch.hpp"

#include <cstring>

namespace YSE {
  namespace SYNTH {

    void fmPatch::toUnpacked(char dest[156]) const {
      for (int i = 0; i < 6; i++) {
        const fmOperator& o = op[i];
        char* d = dest + i * 21;
        d[0] = static_cast<char>(o.egRate[0]);
        d[1] = static_cast<char>(o.egRate[1]);
        d[2] = static_cast<char>(o.egRate[2]);
        d[3] = static_cast<char>(o.egRate[3]);
        d[4] = static_cast<char>(o.egLevel[0]);
        d[5] = static_cast<char>(o.egLevel[1]);
        d[6] = static_cast<char>(o.egLevel[2]);
        d[7] = static_cast<char>(o.egLevel[3]);
        d[8] = static_cast<char>(o.levelScaleBreakPoint);
        d[9] = static_cast<char>(o.levelScaleLeftDepth);
        d[10] = static_cast<char>(o.levelScaleRightDepth);
        d[11] = static_cast<char>(o.levelScaleLeftCurve);
        d[12] = static_cast<char>(o.levelScaleRightCurve);
        d[13] = static_cast<char>(o.rateScaling);
        d[14] = static_cast<char>(o.ampModSens);
        d[15] = static_cast<char>(o.keyVelSens);
        d[16] = static_cast<char>(o.outputLevel);
        d[17] = static_cast<char>(o.oscMode);
        d[18] = static_cast<char>(o.freqCoarse);
        d[19] = static_cast<char>(o.freqFine);
        d[20] = static_cast<char>(o.detune);
      }
      dest[126] = static_cast<char>(pitchEgRate[0]);
      dest[127] = static_cast<char>(pitchEgRate[1]);
      dest[128] = static_cast<char>(pitchEgRate[2]);
      dest[129] = static_cast<char>(pitchEgRate[3]);
      dest[130] = static_cast<char>(pitchEgLevel[0]);
      dest[131] = static_cast<char>(pitchEgLevel[1]);
      dest[132] = static_cast<char>(pitchEgLevel[2]);
      dest[133] = static_cast<char>(pitchEgLevel[3]);
      dest[134] = static_cast<char>(algorithm);
      dest[135] = static_cast<char>(feedback);
      dest[136] = static_cast<char>(oscKeySync);
      dest[137] = static_cast<char>(lfoSpeed);
      dest[138] = static_cast<char>(lfoDelay);
      dest[139] = static_cast<char>(lfoPitchModDepth);
      dest[140] = static_cast<char>(lfoAmpModDepth);
      dest[141] = static_cast<char>(lfoSync);
      dest[142] = static_cast<char>(lfoWaveform);
      dest[143] = static_cast<char>(pitchModSens);
      dest[144] = static_cast<char>(transpose);
      for (int i = 0; i < 10; i++)
        dest[145 + i] = name[i];
      dest[155] = static_cast<char>(opEnabled);
    }

    // ─── built-in voices ─────────────────────────────────────────────────────

    namespace {

      // A fully-sustaining operator: envelope snaps to full and holds, no key
      // scaling, no velocity sensitivity (so tests are level-deterministic),
      // frequency ratio 1.0 (coarse 1 / fine 0 / detune centred).
      fmOperator makeOp(uint8_t outputLevel, uint8_t coarse = 1) {
        fmOperator o{};
        o.egRate[0] = o.egRate[1] = o.egRate[2] = o.egRate[3] = 99;
        o.egLevel[0] = o.egLevel[1] = o.egLevel[2] = 99;
        o.egLevel[3] = 0; // L4 is the release floor
        o.levelScaleBreakPoint = 39;
        o.levelScaleLeftDepth = 0;
        o.levelScaleRightDepth = 0;
        o.levelScaleLeftCurve = 0;
        o.levelScaleRightCurve = 0;
        o.rateScaling = 0;
        o.ampModSens = 0;
        o.keyVelSens = 0;
        o.outputLevel = outputLevel;
        o.oscMode = 0;
        o.freqCoarse = coarse;
        o.freqFine = 0;
        o.detune = 7;
        return o;
      }

      // Fill the global (non-operator) fields of a sustaining voice: flat pitch
      // envelope (level 50 = centre), LFO present but with zero modulation
      // depth, no transpose.
      void fillGlobals(fmPatch& p, uint8_t algorithm, uint8_t feedback, const char* name) {
        p.pitchEgRate[0] = p.pitchEgRate[1] = p.pitchEgRate[2] = p.pitchEgRate[3] = 99;
        p.pitchEgLevel[0] = p.pitchEgLevel[1] = p.pitchEgLevel[2] = p.pitchEgLevel[3] = 50;
        p.algorithm = algorithm;
        p.feedback = feedback;
        p.oscKeySync = 1;
        p.lfoSpeed = 35;
        p.lfoDelay = 0;
        p.lfoPitchModDepth = 0;
        p.lfoAmpModDepth = 0;
        p.lfoSync = 1;
        p.lfoWaveform = 0;
        p.pitchModSens = 0;
        p.transpose = 24;
        std::memset(p.name, ' ', sizeof(p.name));
        for (int i = 0; i < 10 && name[i]; i++)
          p.name[i] = name[i];
        p.opEnabled = 0x3f;
      }

    } // namespace

    fmPatch fmPatch::sine() {
      fmPatch p{};
      // Algorithm 32 (index 31): six parallel carriers. Only OP1 sounds.
      p.op[0] = makeOp(99);
      for (int i = 1; i < 6; i++)
        p.op[i] = makeOp(0);
      fillGlobals(p, 31, 0, "YSE Sine");
      return p;
    }

    fmPatch fmPatch::fm2op() {
      fmPatch p{};
      // Algorithm 5 (index 4): three 2-op stacks. In the first stack OP1 (op[0],
      // routed to modulation bus 1) modulates OP2 (op[1], the carrier that
      // reaches the output); both run at ratio 1.0. A moderate modulator level
      // gives a clear carrier plus a handful of FM sidebands rather than a
      // saturated, noise-like spectrum. The other stacks are silent.
      p.op[0] = makeOp(72); // modulator (modulation index)
      p.op[1] = makeOp(99); // carrier
      for (int i = 2; i < 6; i++)
        p.op[i] = makeOp(0);
      fillGlobals(p, 4, 0, "YSE 2opFM");
      return p;
    }

    fmPatch fmPatch::brass() {
      fmPatch p{};
      // Algorithm 1 (index 0) with all six operators active and operator
      // feedback engaged — a bright, harmonically rich voice that exercises the
      // full operator set.
      p.op[0] = makeOp(99, 1);
      p.op[1] = makeOp(90, 1);
      p.op[2] = makeOp(90, 1);
      p.op[3] = makeOp(85, 1);
      p.op[4] = makeOp(90, 1);
      p.op[5] = makeOp(85, 1);
      fillGlobals(p, 0, 6, "YSE Brass");
      return p;
    }

  } // namespace SYNTH
} // namespace YSE
