/*
  ==============================================================================

    sample.cpp
    Created: 28 Jan 2014 2:25:46pm
    Author:  yvan

  ==============================================================================
*/

#include "sample.hpp"

namespace YSE {
  namespace DSP {

    sample::sample(UInt length) : buffer(length) {}

    sample::sample(AUDIOBUFFER & cp) : buffer(cp.getLength()) {
      operator=(cp);
    }

    UInt  sample::getLength() const {
      return buffer.size();
    }

    UInt	sample::getLengthMS() const {
      return (UInt)(buffer.size() / static_cast<Flt>(SAMPLERATE * 0.001));
    }

    Flt		sample::getLengthSec() const {
      return (buffer.size() / static_cast<Flt>(SAMPLERATE));
    }

    // getting rid of this function would be safer, but can we?
    Flt * sample::getBuffer() {
      return buffer.data();
    }

    AUDIOBUFFER & sample::operator+=(Flt f) {
      UInt l = buffer.size();
      Flt * ptr = buffer.data();
      for (; l > 7; l -= 8, ptr += 8) {
        ptr[0] += f; ptr[1] += f; ptr[2] += f; ptr[3] += f;
        ptr[4] += f; ptr[5] += f; ptr[6] += f; ptr[7] += f;
      }
      while (l--) *ptr++ += f;
      return (*this);
    }

    AUDIOBUFFER & sample::operator+=(AUDIOBUFFER & s) {
      // use length of shortest buffer to prevent memory errors
      UInt l = buffer.size() < s.getLength() ? buffer.size() : s.getLength();
      Flt * ptr1 = buffer.data();
      Flt * ptr2 = s.buffer.data();

      for (; l > 7; l -= 8, ptr1 += 8, ptr2 += 8) {
        ptr1[0] += ptr2[0]; ptr1[1] += ptr2[1]; ptr1[2] += ptr2[2]; ptr1[3] += ptr2[3];
        ptr1[4] += ptr2[4]; ptr1[5] += ptr2[5]; ptr1[6] += ptr2[6]; ptr1[7] += ptr2[7];
      }
      while (l--) *ptr1++ += *ptr2++;
      return (*this);
    }

    AUDIOBUFFER & sample::operator-=(Flt f) {
      UInt l = buffer.size();
      Flt * ptr = buffer.data();
      for (; l > 7; l -= 8, ptr += 8) {
        ptr[0] -= f; ptr[1] -= f; ptr[2] -= f; ptr[3] -= f;
        ptr[4] -= f; ptr[5] -= f; ptr[6] -= f; ptr[7] -= f;
      }
      while (l--) *ptr++ -= f;
      return (*this);
    }

    AUDIOBUFFER & sample::operator-=(AUDIOBUFFER & s) {
      // use length of shortest buffer to prevent memory errors
      UInt l = buffer.size() < s.getLength() ? buffer.size() : s.getLength();
      Flt * ptr1 = buffer.data();
      Flt * ptr2 = s.buffer.data();
      for (; l > 7; l -= 8, ptr1 += 8, ptr2 += 8) {
        ptr1[0] -= ptr2[0]; ptr1[1] -= ptr2[1]; ptr1[2] -= ptr2[2]; ptr1[3] -= ptr2[3];
        ptr1[4] -= ptr2[4]; ptr1[5] -= ptr2[5]; ptr1[6] -= ptr2[6]; ptr1[7] -= ptr2[7];
      }
      while (l--) *ptr1++ -= *ptr2++;
      return (*this);
    }

    AUDIOBUFFER & sample::operator*=(Flt f) {
      UInt l = buffer.size();
      Flt * ptr = buffer.data();
      for (; l > 7; l -= 8, ptr += 8) {
        ptr[0] *= f; ptr[1] *= f; ptr[2] *= f; ptr[3] *= f;
        ptr[4] *= f; ptr[5] *= f; ptr[6] *= f; ptr[7] *= f;
      }
      while (l--) *ptr++ *= f;
      return (*this);
    }

    AUDIOBUFFER & sample::operator*=(AUDIOBUFFER & s) {
      // use length of shortest buffer to prevent memory errors
      UInt l = buffer.size() < s.getLength() ? buffer.size() : s.getLength();
      Flt * ptr1 = buffer.data();
      Flt * ptr2 = s.buffer.data();
      for (; l > 7; l -= 8, ptr1 += 8, ptr2 += 8) {
        ptr1[0] *= ptr2[0]; ptr1[1] *= ptr2[1]; ptr1[2] *= ptr2[2]; ptr1[3] *= ptr2[3];
        ptr1[4] *= ptr2[4]; ptr1[5] *= ptr2[5]; ptr1[6] *= ptr2[6]; ptr1[7] *= ptr2[7];
      }
      while (l--) *ptr1++ *= *ptr2++;
      return (*this);
    }

    AUDIOBUFFER & sample::operator/=(Flt f) {
      UInt l = buffer.size();
      Flt * ptr = buffer.data();
      for (; l > 7; l -= 8, ptr += 8) {
        ptr[0] = (f ? ptr[0] /= f : 0);
        ptr[1] = (f ? ptr[1] /= f : 0);
        ptr[2] = (f ? ptr[2] /= f : 0);
        ptr[3] = (f ? ptr[3] /= f : 0);
        ptr[4] = (f ? ptr[4] /= f : 0);
        ptr[5] = (f ? ptr[5] /= f : 0);
        ptr[6] = (f ? ptr[6] /= f : 0);
        ptr[7] = (f ? ptr[7] /= f : 0);
      }
      while (l--) *ptr++ = (f ? *ptr / f : 0);
      return (*this);
    }

    AUDIOBUFFER & sample::operator/=(AUDIOBUFFER & s) {
      // use length of shortest buffer to prevent memory errors
      UInt l = buffer.size() < s.getLength() ? buffer.size() : s.getLength();
      Flt * ptr1 = buffer.data();
      Flt * ptr2 = s.buffer.data();
      for (; l > 7; l -= 8, ptr1 += 8, ptr2 += 8) {
        ptr1[0] = (ptr2[0] ? ptr1[0] /= ptr2[0] : 0);
        ptr1[1] = (ptr2[1] ? ptr1[1] /= ptr2[1] : 0);
        ptr1[2] = (ptr2[2] ? ptr1[2] /= ptr2[2] : 0);
        ptr1[3] = (ptr2[3] ? ptr1[3] /= ptr2[3] : 0);
        ptr1[4] = (ptr2[4] ? ptr1[4] /= ptr2[4] : 0);
        ptr1[5] = (ptr2[5] ? ptr1[5] /= ptr2[5] : 0);
        ptr1[6] = (ptr2[6] ? ptr1[6] /= ptr2[6] : 0);
        ptr1[7] = (ptr2[7] ? ptr1[7] /= ptr2[7] : 0);

      }
      while (l--) {
        *ptr1 = (*ptr2 ? *ptr1 / *ptr2 : 0);
        ptr1++, ptr2++;
      }
      return (*this);
    }

    AUDIOBUFFER & sample::operator=(AUDIOBUFFER & s) {
      if (buffer.size() != s.getLength()) {
        resize(s.getLength());
      }

      UInt l = buffer.size();
      Flt * ptr1 = buffer.data();
      Flt * ptr2 = s.buffer.data();

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
      while (l--) *ptr1++ = *ptr2++;
      return (*this);
    }

    AUDIOBUFFER & sample::operator=(Flt f) {
      UInt l = buffer.size();
      Flt * ptr1 = buffer.data();
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
      while (l--) *ptr1++ = f;
      return (*this);
    }

    AUDIOBUFFER & sample::copyFrom(AUDIOBUFFER & s, UInt sourcePos, UInt destPos, UInt length) {
      // TODO: don't just return if buffers are not long enough!
      if ((UInt)(sourcePos + length) > length) return (*this);
      if ((UInt)(destPos + length) > s.getLength()) return (*this);

      UInt l = length;
      Flt * ptr1 = buffer.data() + sourcePos;
      Flt * ptr2 = s.buffer.data() + destPos;
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
      while (l--) *ptr1++ = *ptr2++;

      return (*this);
    }



    AUDIOBUFFER & sample::resize(UInt length, Bool copy) {
      if (copy) {
        buffer.resize(length);
      }
      else {
        // avoid copying the contents if not needed
        if (buffer.capacity() < length) {
          buffer.clear();
        }
        buffer.resize(length);
      }
      return (*this);
    }

    sample & YSE::DSP::sample::drawLine(UInt start, UInt stop, Flt startValue, Flt stopValue) {
      //Clamp(start, 0, impl->length.load());
      //Clamp(stop, 0, impl->length.load());
      //if (stop < start) return *this; // don't do this

      Flt frac = (stopValue - startValue) / static_cast<Flt>(stop - start > 1 ? stop - start : 1); // don't divide by zero
      Flt * ptr = buffer.data();
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

      Flt * ptr = buffer.data();
      for (UInt i = start; i < stop; i++) {
        ptr[i] = value;
      }
      return *this;
    }

    Flt YSE::DSP::sample::getBack() {
      return buffer.back();
    }

    /************************************************************************/


  } // namespace DSP
} // namespace YSE