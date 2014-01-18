#include "stdafx.h"
#include "sample.hpp"
#include "implementations.h"
#include "utils/misc.hpp"

namespace YSE {
  namespace DSP {

  sample::sample(UInt size) : impl(new sampleImpl(size)) {}

  sample::sample(SAMPLE cp) : impl(new sampleImpl(cp.getLength())) {
	  operator=(cp);
  }

  sample::~sample() {
    delete impl;
  }

  UInt  sample::getLength() const { 
    return impl->length; 
  } 

  UInt	sample::getLengthMS() const { 
    return (UInt)(impl->length / (Flt)(sampleRate * 0.001)); 
  }

  Flt		sample::getLengthSec() const { 
    return (impl->length / (Flt)(sampleRate)); 
  }

  // getting rid of this function would be safer, but can we?
  Flt * sample::getBuffer() const { 
    return impl->buffer; 
  }

  SAMPLE sample::operator+=(Flt f) {
	  UInt l = impl->length;
	  Flt * ptr = impl->buffer;
	  for (; l > 7; l -= 8, ptr += 8) {
		  ptr[0] += f; ptr[1] += f; ptr[2] += f; ptr[3] += f;
		  ptr[4] += f; ptr[5] += f; ptr[6] += f; ptr[7] += f;
	  }
	  while(l--) *ptr++ += f;
	  return (*this);
  }

  SAMPLE sample::operator+=(SAMPLE s) {
    // use length of shortest buffer to prevent memory errors
    UInt l = impl->length < s.getLength() ? impl->length.load() : s.getLength();
	  Flt * ptr1 = impl->buffer;
	  Flt * ptr2 = s.impl->buffer;
	
    for (; l > 7; l -= 8, ptr1 += 8, ptr2 += 8) {
		  ptr1[0] += ptr2[0]; ptr1[1] += ptr2[1]; ptr1[2] += ptr2[2]; ptr1[3] += ptr2[3];
		  ptr1[4] += ptr2[4]; ptr1[5] += ptr2[5]; ptr1[6] += ptr2[6]; ptr1[7] += ptr2[7];
	  }
	  while(l--) *ptr1++ += *ptr2++;
	  return (*this);
  }

  SAMPLE sample::operator-=(Flt f) {
	  UInt l = impl->length;
	  Flt * ptr = impl->buffer;
	  for (; l > 7; l -= 8, ptr += 8) {
		  ptr[0] -= f; ptr[1] -= f; ptr[2] -= f; ptr[3] -= f;
		  ptr[4] -= f; ptr[5] -= f; ptr[6] -= f; ptr[7] -= f;
	  }
	  while(l--) *ptr++ -= f;
	  return (*this);
  }

  SAMPLE sample::operator-=(SAMPLE s) {
	  // use length of shortest buffer to prevent memory errors
    UInt l = impl->length < s.getLength() ? impl->length.load() : s.getLength();
	  Flt * ptr1 = impl->buffer;
	  Flt * ptr2 = s.impl->buffer;
	  for (; l > 7; l -= 8, ptr1 += 8, ptr2 += 8) {
		  ptr1[0] -= ptr2[0]; ptr1[1] -= ptr2[1]; ptr1[2] -= ptr2[2]; ptr1[3] -= ptr2[3];
		  ptr1[4] -= ptr2[4]; ptr1[5] -= ptr2[5]; ptr1[6] -= ptr2[6]; ptr1[7] -= ptr2[7];
	  }
	  while(l--) *ptr1++ -= *ptr2++;
	  return (*this);
  }

  SAMPLE sample::operator*=(Flt f) {
	  UInt l = impl->length;
	  Flt * ptr = impl->buffer;
	  for (; l > 7; l -= 8, ptr += 8) {
		  ptr[0] *= f; ptr[1] *= f; ptr[2] *= f; ptr[3] *= f;
		  ptr[4] *= f; ptr[5] *= f; ptr[6] *= f; ptr[7] *= f;
	  }
	  while(l--) *ptr++ *= f;
	  return (*this);
  }

  SAMPLE sample::operator*=(SAMPLE s) {
	  // use length of shortest buffer to prevent memory errors
    UInt l = impl->length < s.getLength() ? impl->length.load() : s.getLength();
	  Flt * ptr1 = impl->buffer;
	  Flt * ptr2 = s.impl->buffer;
	  for (; l > 7; l -= 8, ptr1 += 8, ptr2 += 8) {
		  ptr1[0] *= ptr2[0]; ptr1[1] *= ptr2[1]; ptr1[2] *= ptr2[2]; ptr1[3] *= ptr2[3];
		  ptr1[4] *= ptr2[4]; ptr1[5] *= ptr2[5]; ptr1[6] *= ptr2[6]; ptr1[7] *= ptr2[7];
	  }
	  while(l--) *ptr1++ *= *ptr2++;
	  return (*this);
  }

  SAMPLE sample::operator/=(Flt f) {
	  UInt l = impl->length;
	  Flt * ptr = impl->buffer;
	  for (; l > 7; l -= 8, ptr += 8) {
		  ptr[0] = (f? ptr[0] /= f : 0); 
		  ptr[1] = (f? ptr[1] /= f : 0); 
		  ptr[2] = (f? ptr[2] /= f : 0); 
		  ptr[3] = (f? ptr[3] /= f : 0);
		  ptr[4] = (f? ptr[4] /= f : 0); 
		  ptr[5] = (f? ptr[5] /= f : 0); 
		  ptr[6] = (f? ptr[6] /= f : 0); 
		  ptr[7] = (f? ptr[7] /= f : 0);
	  }
	  while(l--) *ptr++ = (f? *ptr / f : 0);
	  return (*this);
  }

  SAMPLE sample::operator/=(SAMPLE s) {
	  // use length of shortest buffer to prevent memory errors
    UInt l = impl->length < s.getLength() ? impl->length.load() : s.getLength();
	  Flt * ptr1 = impl->buffer;
	  Flt * ptr2 = s.impl->buffer;
	  for (; l > 7; l -= 8, ptr1 += 8, ptr2 += 8) {
		  ptr1[0] = (ptr2[0]? ptr1[0] /= ptr2[0] : 0); 
		  ptr1[1] = (ptr2[1]? ptr1[1] /= ptr2[1] : 0); 
		  ptr1[2] = (ptr2[2]? ptr1[2] /= ptr2[2] : 0); 
		  ptr1[3] = (ptr2[3]? ptr1[3] /= ptr2[3] : 0);
		  ptr1[4] = (ptr2[4]? ptr1[4] /= ptr2[4] : 0); 
		  ptr1[5] = (ptr2[5]? ptr1[5] /= ptr2[5] : 0); 
		  ptr1[6] = (ptr2[6]? ptr1[6] /= ptr2[6] : 0); 
		  ptr1[7] = (ptr2[7]? ptr1[7] /= ptr2[7] : 0);
	
	  }
	  while(l--) {
		  *ptr1 = (*ptr2? *ptr1 / *ptr2 : 0);
		  ptr1++, ptr2++;
	  }
	  return (*this);
  }

  SAMPLE sample::operator=(SAMPLE s) {
    if (impl->length != s.getLength()) {
      resize(s.getLength());
    }

    UInt l = impl->length;
	  Flt * ptr1 = impl->buffer;
    Flt * ptr2 = s.impl->buffer;

	  for (; l > 7; l -= 8, ptr1 += 8, ptr2 += 8) {
		  ptr1[0] = ptr2[0]; 
		  ptr1[1] = ptr2[1]; 
		  ptr1[2] = ptr2[2]; 
		  ptr1[3] = ptr2[3];
		  ptr1[4] = ptr2[4]; 
		  ptr1[5] = ptr2[5]; 
		  ptr1[6] = ptr2[6]; 
		  ptr1[7] = ptr2[7];
	
	  }
	  while(l--) *ptr1++ = *ptr2++;
	  return (*this);
  }

  SAMPLE sample::operator=(Flt f) { 
	  UInt l = impl->length;
	  Flt * ptr1 = impl->buffer;
	  for (; l > 7; l -= 8, ptr1 += 8) {
		  ptr1[0] = f; 
		  ptr1[1] = f; 
		  ptr1[2] = f; 
		  ptr1[3] = f;
		  ptr1[4] = f; 
		  ptr1[5] = f; 
		  ptr1[6] = f; 
		  ptr1[7] = f;
	
	  }
	  while(l--) *ptr1++ = f;
	  return (*this);
  }

  SAMPLE sample::copyFrom(SAMPLE s, Int sourcePos, Int destPos, Int length) {
	  // TODO: don't just return if buffers are not long enough!
    if ((UInt)(sourcePos + length) > impl->length) return (*this);
	  if ((UInt)(destPos + length) > s.impl->length) return (*this);

	  UInt l = length;
	  Flt * ptr1 = impl->buffer + sourcePos;
	  Flt * ptr2 = s.impl->buffer + destPos;
	  for (; l > 7; l -= 8, ptr1 += 8, ptr2 += 8) {
		  ptr1[0] = ptr2[0]; 
		  ptr1[1] = ptr2[1]; 
		  ptr1[2] = ptr2[2]; 
		  ptr1[3] = ptr2[3];
		  ptr1[4] = ptr2[4]; 
		  ptr1[5] = ptr2[5]; 
		  ptr1[6] = ptr2[6]; 
		  ptr1[7] = ptr2[7];
	
	  }
	  while(l--) *ptr1++ = *ptr2++;

	  return (*this);
  }

  // what was i thinking?? Let's see if anyone needs this 
  /* const YSE::DSP::sample& YSE::DSP::sample::getPart(YSE::DSP::samplePtr& s, UInt start, UInt size) {
	  if (s.base->length < start + size) return (*this);
	  if (base->length < size) Realloc(base->buffer, size);

	  UInt l = start + size;
	  Flt * ptr1 = base->buffer;
	  Flt * ptr2 = s.base->buffer;
	  for (; l > start + 7; l -= 8, ptr1 += 8, ptr2 += 8) {
		  ptr1[0] = ptr2[0]; 
		  ptr1[1] = ptr2[1]; 
		  ptr1[2] = ptr2[2]; 
		  ptr1[3] = ptr2[3];
		  ptr1[4] = ptr2[4]; 
		  ptr1[5] = ptr2[5]; 
		  ptr1[6] = ptr2[6]; 
		  ptr1[7] = ptr2[7];
	
	  }
	  while(l-- > start) *ptr1++ = *ptr2++;

	  return (*this);
  }*/



  SAMPLE sample::resize(UInt length, Bool copy) {
	  Flt * temp = new Flt[length];
	  if (copy) {
		  UInt l = length;
		  Flt * ptr1 = temp;
		  Flt * ptr2 = impl->buffer;

		  for (; l > 7; l -= 8, ptr1 += 8, ptr2 += 8) {
			  ptr1[0] = ptr2[0]; 
			  ptr1[1] = ptr2[1]; 
			  ptr1[2] = ptr2[2]; 
			  ptr1[3] = ptr2[3];
			  ptr1[4] = ptr2[4]; 
			  ptr1[5] = ptr2[5]; 
			  ptr1[6] = ptr2[6]; 
			  ptr1[7] = ptr2[7];
	
		  }
		  while(l--) *ptr1++ = *ptr2++;
		
		  l = length - impl->length;
		  for (; l > 7; l -= 8, ptr1 += 8, ptr2 += 8) {
			  ptr1[0] = 0; 
			  ptr1[1] = 0; 
			  ptr1[2] = 0; 
			  ptr1[3] = 0;
			  ptr1[4] = 0; 
			  ptr1[5] = 0; 
			  ptr1[6] = 0; 
			  ptr1[7] = 0;
	
		  }
		  while(l--) *ptr1++ = 0;
	  }
    if (impl->buffer != NULL) delete[] impl->buffer;
	  impl->buffer = temp;
	  impl->length = length;

    return (*this);
  }

  sample & YSE::DSP::sample::drawLine(UInt start, UInt stop, Flt startValue, Flt stopValue) {
    //Clamp(start, 0, impl->length.load());
    //Clamp(stop, 0, impl->length.load());
    //if (stop < start) return *this; // don't do this

    Flt frac = (stopValue - startValue) / (Flt)(stop - start > 1 ? stop - start : 1); // don't divide by zero
    Flt * ptr = impl->buffer;
    Flt value = startValue;
    for (UInt i = start; i < stop; i++) {
      ptr[i] = value;
      value += frac;
    }
    return *this;
  }


  sample & YSE::DSP::sample::drawLine(UInt start, UInt stop, Flt value) {
    //Clamp(start, 0, impl->length.load());
    //Clamp(stop, 1, impl->length.load());
    //if (stop < start) return *this; // don't do this
  
    Flt * ptr = impl->buffer;
    for (UInt i = start; i < stop; i++) {
      ptr[i] = value;
    }
    return *this;
  }

  Flt YSE::DSP::sample::getBack() {
    return impl->buffer[impl->length - 1];
  }

/************************************************************************/


  } // namespace DSP
} // namespace YSE