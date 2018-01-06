/*
  ==============================================================================

    oscillators.cpp
    Created: 31 Jan 2014 2:54:59pm
    Author:  yvan

  ==============================================================================
*/

#include <cmath>
#include "oscillators.hpp"
#include <vector>
#include "../utils/misc.hpp"

#define UNITBIT32 1572864.f  /* 3*2^19; bit 32 has place value 1 */
#define HIOFFSET 1
#define LOWOFFSET 0

union tablePhase {
  Dbl d;
  Int i[2];
};

namespace YSE {
  namespace DSP {

    class cosTable {
    public:
      cosTable();
      Flt * operator()();
    private:
      std::vector<Flt> table;
    };



    saw::saw() : phase(0), conv(0), frequency(440)  {}

    YSE::DSP::buffer & saw::operator()(YSE::DSP::buffer & in) {
      if (in.getLength() != buffer.getLength()) buffer.resize(in.getLength());

      inPtr = in.getPtr();
      calc(false);

      return buffer;
    }

    YSE::DSP::buffer & saw::operator()(Flt frequency, UInt length) {
      if (length != buffer.getLength()) buffer.resize(length);

      this->frequency = frequency;
      calc(true);

      return buffer;
    }


    void saw::calc(Bool useFrequency) {
      Flt * outPtr = buffer.getPtr();
      UInt length = buffer.getLength();

      conv = 1.f / SAMPLERATE;
      Dbl dphase = phase + (Dbl)UNITBIT32;
      tablePhase tf;
      Int normhipart;

      tf.d = UNITBIT32;
      normhipart = tf.i[HIOFFSET];
      tf.d = dphase;

      while (length--) {
        tf.i[HIOFFSET] = normhipart;
        if (useFrequency) dphase += frequency * conv;
        else	dphase += *inPtr++ * conv;
        *outPtr++ = static_cast<Flt>(tf.d) - UNITBIT32;
        tf.d = dphase;
      }

      tf.i[HIOFFSET] = normhipart;
      phase = tf.d - UNITBIT32;

    }

    /**********************************************************************************/

#define LOGCOSTABSIZE 9
#define COSTABSIZE (1<<LOGCOSTABSIZE)

    cosTable::cosTable() : table(COSTABSIZE + 1) {
      Flt *fp;
      Flt phase = 0;
      Flt phsinc = (2.f * YSE::Pi) / COSTABSIZE;

      fp = table.data();
      for (Int i = COSTABSIZE + 1; i--; fp++, phase += phsinc) *fp = ::cos(phase);

    }

    Flt * cosTable::operator()() {
      return table.data();
    }

    Flt * CosTable() {
      static cosTable table;
      return table();
    }

    /**********************************************************************************/

    cosine::cosine() {}

    YSE::DSP::buffer & cosine::operator()(YSE::DSP::buffer & in) {
      if (in.getLength() != buffer.getLength()) {
        buffer.resize(in.getLength());
      }

      Flt * inPtr = in.getPtr();
      Flt * outPtr = buffer.getPtr();
      UInt  length = in.getLength();

      Flt * tab = CosTable();
      Flt * addr, f1, f2, frac;
      Dbl   dphase;
      int   normhipart;
      tablePhase tf;

      tf.d = UNITBIT32;
      normhipart = tf.i[HIOFFSET];

      dphase = (Dbl)(*inPtr++ * static_cast<Flt>(COSTABSIZE)) + UNITBIT32;
      tf.d = dphase;
      addr = tab + (tf.i[HIOFFSET] & (COSTABSIZE - 1));
      tf.i[HIOFFSET] = normhipart;

      while (--length) {
        dphase = (Dbl)(*inPtr++ * static_cast<Flt>(COSTABSIZE)) + UNITBIT32;
        frac = static_cast<Flt>(tf.d) - UNITBIT32;
        tf.d = dphase;
        f1 = addr[0];
        f2 = addr[1];
        addr = tab + (tf.i[HIOFFSET] & (COSTABSIZE - 1));
        *outPtr++ = f1 + frac * (f2 - f1);
        tf.i[HIOFFSET] = normhipart;
      }

      frac = static_cast<Flt>(tf.d) - UNITBIT32;
      f1 = addr[0];
      f2 = addr[1];
      *outPtr++ = f1 + frac * (f2 - f1);

      return buffer;
    }

    /**********************************************************************************/

    sine::sine() : phase(0), conv(0) {}

    void sine::reset() {
      phase = 0;
      conv = 0;
    }

    YSE::DSP::buffer & sine::operator()(YSE::DSP::buffer & in) {
      if (in.getLength() != buffer.getLength()) buffer.resize(in.getLength());

      inPtr = in.getPtr();
      calc(false);

      return buffer;
    }

    YSE::DSP::buffer & sine::operator()(Flt frequency, UInt length) {
      if (length != buffer.getLength()) buffer.resize(length);

      this->frequency = frequency;
      calc(true);

      return buffer;
    }


    void sine::calc(Bool useFrequency) {
      Flt * outPtr = buffer.getPtr();
      UInt length = buffer.getLength();

      Flt *tab = CosTable(), *addr, f1, f2, frac;
      Dbl dphase = phase + UNITBIT32;
      Int normhipart;
      tablePhase tf;

      conv = COSTABSIZE / static_cast<Flt>(SAMPLERATE);

      tf.d = UNITBIT32;
      normhipart = tf.i[HIOFFSET];

      tf.d = dphase;
      if (useFrequency) dphase += frequency * conv;
      else	{
        dphase += *inPtr++ * conv;
      }
      addr = tab + (tf.i[HIOFFSET] & (COSTABSIZE - 1));
      tf.i[HIOFFSET] = normhipart;
      frac = static_cast<Flt>(tf.d) - UNITBIT32;

      while (--length) {
        tf.d = dphase;
        f1 = addr[0];
        f2 = addr[1];

        if (useFrequency) dphase += frequency * conv;
        else	dphase += *inPtr++ * conv;
        addr = tab + (tf.i[HIOFFSET] & (COSTABSIZE - 1));
        tf.i[HIOFFSET] = normhipart;
        *outPtr++ = f1 + frac * (f2 - f1);
        frac = static_cast<Flt>(tf.d) - UNITBIT32;
      }
      f1 = addr[0];
      f2 = addr[1];
      *outPtr++ = f1 + frac * (f2 - f1);

      tf.d = UNITBIT32 * COSTABSIZE;
      normhipart = tf.i[HIOFFSET];
      tf.d = dphase + (UNITBIT32 * COSTABSIZE - UNITBIT32);
      tf.i[HIOFFSET] = normhipart;
      phase = tf.d - UNITBIT32 * COSTABSIZE;
    }

    /**********************************************************************************
      oscillator
    **********************************************************************************/

    oscillator::oscillator() : phase(0), conv(0), table(nullptr) {}

    void oscillator::reset() {
      phase = 0;
      conv = 0;
    }

    void oscillator::initialize(wavetable & source) {
      table = &source;
    }

    YSE::DSP::buffer & oscillator::operator()(YSE::DSP::buffer & in) {
      if (in.getLength() != buffer.getLength()) buffer.resize(in.getLength());

      inPtr = in.getPtr();
      calc(false);

      return buffer;
    }

    YSE::DSP::buffer & oscillator::operator()(Flt frequency, UInt length) {
      if (length != buffer.getLength()) buffer.resize(length);

      this->frequency = frequency;
      calc(true);

      return buffer;
    }

    void oscillator::calc(Bool useFrequency) {
      Flt * outPtr = buffer.getPtr();
      UInt bufferLength = buffer.getLength();
      Flt * tablePtr = table->getPtr();
      UInt tableLength = table->getLength();
      Flt f1, f2;

      Flt increase = tableLength / static_cast<Flt>(SAMPLERATE);



      while (bufferLength--) {        
        f1 = tablePtr[static_cast<UInt>(phase)];
        f2 = tablePtr[static_cast<UInt>(phase) + 1];
        *outPtr++ = f1 + (phase - static_cast<Int>(phase)) * (f2 - f1);

        phase += (useFrequency ? frequency : *inPtr++) * increase;

        while (phase >= tableLength) phase -= tableLength;
        while (phase < 0) phase += tableLength;

      }

    }

    /**********************************************************************************/

    noise::noise() : value(307 * 1319) {}


    YSE::DSP::buffer & noise::operator()(UInt length) {
      if (length != buffer.getLength()) buffer.resize(length);
      Flt * outPtr = buffer.getPtr();

      while (length--) {
        *outPtr++ = ((float)((value & 0x7fffffff) - 0x40000000)) * (float)(1.0 / 0x40000000);
        value = value * 435898247 + 382842987;
      }

      return buffer;
    }

    /*******************************************************************************************/


    vcf::vcf() {
      q = im = re = isr = 0;
    }

    vcf& vcf::sharpness(Flt q) {
      this->q = (q > 0 ? q : 0.f);
      return (*this);
    }

    YSE::DSP::buffer & vcf::operator()(YSE::DSP::buffer & in, YSE::DSP::buffer & center, YSE::DSP::buffer & out2) {
      if (in.getLength() != buffer.getLength()) buffer.resize(in.getLength());
      if (in.getLength() != out2.getLength()) out2.resize(in.getLength());

      Flt * inPtr = in.getPtr();
      Flt * centerPtr = center.getPtr();
      Flt * out1Ptr = buffer.getPtr();
      Flt * out2Ptr = out2.getPtr();
      UInt length = in.getLength();

      isr = Pi2 / static_cast<Flt>(SAMPLERATE);

      Flt re2;
      Flt qinv = (q > 0 ? 1.0f / q : 0);
      Flt ampcorrect = 2.0f - 2.0f / (q + 2.0f);
      Flt coefr, coefi;
      Flt *tab = CosTable(), *addr, f1, f2, frac;
      Dbl dphase;
      Int normhipart, tabindex;
      tablePhase tf;

      tf.d = UNITBIT32;
      normhipart = tf.i[HIOFFSET];
#ifdef YSE_WINDOWS
#pragma warning ( disable : 4018 )
#endif
      for (Int i = 0; i < length; i++) {
        float cf, cfindx, r, oneminusr;

        cf = *centerPtr++ * isr;
        if (cf < 0) cf = 0;
        cfindx = cf * static_cast<Flt>(COSTABSIZE / Pi2);
        r = (qinv > 0 ? 1 - cf * qinv : 0);
        if (r < 0) r = 0;
        oneminusr = 1.0f - r;
        dphase = ((Dbl)(cfindx)) + UNITBIT32;
        tf.d = dphase;
        tabindex = tf.i[HIOFFSET] & (COSTABSIZE - 1);
        addr = tab + tabindex;
        tf.i[HIOFFSET] = normhipart;
        frac = static_cast<Flt>(tf.d) - UNITBIT32;
        f1 = addr[0];
        f2 = addr[1];
        coefr = r * (f1 + frac * (f2 - f1));

        addr = tab + ((tabindex - (COSTABSIZE / 4)) & (COSTABSIZE - 1));
        f1 = addr[0];
        f2 = addr[1];
        coefi = r * (f1 + frac * (f2 - f1));

        f1 = *inPtr++;
        re2 = re;
        *out1Ptr++ = re = ampcorrect * oneminusr * f1 + coefr * re2 - coefi * im;
        *out2Ptr++ = im = coefi * re2 + coefr * im;
      }

      return buffer;
    }

  } // end DSP
}   // end YSE