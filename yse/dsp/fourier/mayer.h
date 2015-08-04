/*
  ==============================================================================

    mayer.h
    Created: 27 Jul 2015 1:19:43pm
    Author:  yvan

  ==============================================================================
*/

#ifndef MAYER_H_INCLUDED
#define MAYER_H_INCLUDED

#include "../../headers/types.hpp"

void mayer_fht     (Flt *fz, int n);
void mayer_fft     (int n, Flt *real, Flt *imag);
void mayer_ifft    (int n, Flt *real, Flt *imag);
void mayer_realfft (int n, Flt *real           );
void mayer_realifft(int n, Flt *real           );


#endif  // MAYER_H_INCLUDED
