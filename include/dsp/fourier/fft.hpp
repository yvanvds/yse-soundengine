/*
  ==============================================================================

    fft.h
    Created: 27 Jul 2015 2:04:09pm
    Author:  yvan

  ==============================================================================
*/

#ifndef FFT_H_INCLUDED
#define FFT_H_INCLUDED

#include "../buffer.hpp"
#include "../../headers/enums.hpp"

namespace YSE {
  namespace DSP {

    class API fft {
    public:

      // updates the object and returns real part
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & real, YSE::DSP::buffer & imaginary);
      YSE::DSP::buffer & getReal();
      YSE::DSP::buffer & getImaginary();

    private:
      buffer real, imaginary;
    };

    class API inverseFft {
    public:

      // updates the object and returns real part
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & real, YSE::DSP::buffer & imaginary);
      YSE::DSP::buffer & getReal();
      YSE::DSP::buffer & getImaginary();

    private:
      buffer real, imaginary;
    };

    class API realFft {
    public:
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);
      YSE::DSP::buffer & getReal();
      YSE::DSP::buffer & getImaginary();

    private:
      buffer real, imaginary;
    };

    class API inverseRealFft {
    public:
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & real, YSE::DSP::buffer & imaginary);
      YSE::DSP::buffer & getReal();

    private:
      buffer real;
    };

    class API fftStats {
    public:
      void operator()(YSE::DSP::buffer & real, YSE::DSP::buffer & imaginary);
      YSE::DSP::buffer & getFrequencies();
      YSE::DSP::buffer & getAmplitudes();

    private:
      buffer frequencies, amplitudes;
    };

  }
}



#endif  // FFT_H_INCLUDED
