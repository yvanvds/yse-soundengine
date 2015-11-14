/*
  ==============================================================================

    buffer.cpp
    Created: 28 Jan 2014 2:25:46pm
    Author:  yvan

  ==============================================================================
*/

#include <cassert>
#include "buffer.hpp"


namespace YSE {
  namespace DSP {

    // don't move these constructors to the header file, because it will cause storage
    // vector to be allocated in the application memory instead of the dll memory, causing
    // a dll boundary crossing when resizing it later.
    buffer::buffer(UInt length, UInt overflow) : storage(length + overflow), overflow(overflow) {}
    
    buffer::buffer(const buffer & cp) : storage(cp.storage.size()) {
      operator=(cp);
    }

    buffer & buffer::operator+=(Flt f) {
      UInt l = storage.size();
      Flt * ptr = storage.data();
      for (; l > 7; l -= 8, ptr += 8) {
        ptr[0] += f; ptr[1] += f; ptr[2] += f; ptr[3] += f;
        ptr[4] += f; ptr[5] += f; ptr[6] += f; ptr[7] += f;
      }
      while (l--) *ptr++ += f;
      return (*this);
    }

    buffer & buffer::operator+=(const buffer & s) {
      // use length of shortest buffer to prevent memory errors
      UInt l = getLength() < s.getLength() ? getLength() : s.getLength();
      Flt * ptr1 = storage.data();
      const Flt * ptr2 = s.storage.data();

      for (; l > 7; l -= 8, ptr1 += 8, ptr2 += 8) {
        ptr1[0] += ptr2[0]; ptr1[1] += ptr2[1]; ptr1[2] += ptr2[2]; ptr1[3] += ptr2[3];
        ptr1[4] += ptr2[4]; ptr1[5] += ptr2[5]; ptr1[6] += ptr2[6]; ptr1[7] += ptr2[7];
      }
      while (l--) *ptr1++ += *ptr2++;

      copyOverflow();

      return (*this);
    }

    buffer & buffer::operator-=(Flt f) {
      UInt l = storage.size();
      Flt * ptr = storage.data();
      for (; l > 7; l -= 8, ptr += 8) {
        ptr[0] -= f; ptr[1] -= f; ptr[2] -= f; ptr[3] -= f;
        ptr[4] -= f; ptr[5] -= f; ptr[6] -= f; ptr[7] -= f;
      }
      while (l--) *ptr++ -= f;
      return (*this);
    }

    buffer & buffer::operator-=(const buffer & s) {
      // use length of shortest buffer to prevent memory errors
      UInt l = getLength() < s.getLength() ? getLength() : s.getLength();
      Flt * ptr1 = storage.data();
      const Flt * ptr2 = s.storage.data();
      for (; l > 7; l -= 8, ptr1 += 8, ptr2 += 8) {
        ptr1[0] -= ptr2[0]; ptr1[1] -= ptr2[1]; ptr1[2] -= ptr2[2]; ptr1[3] -= ptr2[3];
        ptr1[4] -= ptr2[4]; ptr1[5] -= ptr2[5]; ptr1[6] -= ptr2[6]; ptr1[7] -= ptr2[7];
      }
      while (l--) *ptr1++ -= *ptr2++;

      copyOverflow();

      return (*this);
    }

    buffer & buffer::operator*=(Flt f) {
      UInt l = storage.size();
      Flt * ptr = storage.data();
      for (; l > 7; l -= 8, ptr += 8) {
        ptr[0] *= f; ptr[1] *= f; ptr[2] *= f; ptr[3] *= f;
        ptr[4] *= f; ptr[5] *= f; ptr[6] *= f; ptr[7] *= f;
      }
      while (l--) *ptr++ *= f;
      return (*this);
    }

    buffer & buffer::operator*=(const buffer & s) {
      // use length of shortest buffer to prevent memory errors
      UInt l = getLength() < s.getLength() ? getLength() : s.getLength();
      Flt * ptr1 = storage.data();
      const Flt * ptr2 = s.storage.data();
      for (; l > 7; l -= 8, ptr1 += 8, ptr2 += 8) {
        ptr1[0] *= ptr2[0]; ptr1[1] *= ptr2[1]; ptr1[2] *= ptr2[2]; ptr1[3] *= ptr2[3];
        ptr1[4] *= ptr2[4]; ptr1[5] *= ptr2[5]; ptr1[6] *= ptr2[6]; ptr1[7] *= ptr2[7];
      }
      while (l--) *ptr1++ *= *ptr2++;

      copyOverflow();

      return (*this);
    }

    buffer & buffer::operator/=(Flt f) {
      UInt l = storage.size();
      Flt * ptr = storage.data();
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

    buffer & buffer::operator/=(const buffer & s) {
      // use length of shortest buffer to prevent memory errors
      UInt l = getLength() < s.getLength() ? getLength() : s.getLength();
      Flt * ptr1 = storage.data();
      const Flt * ptr2 = s.storage.data();
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

      copyOverflow();

      return (*this);
    }

    buffer & buffer::operator=(const buffer & s) {
      if (storage.size() != s.storage.size()) {
        resize(s.storage.size());
      }

      overflow = s.overflow;

      UInt l = storage.size();
      Flt * ptr1 = storage.data();
      const Flt * ptr2 = s.storage.data();

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

    buffer & buffer::operator=(Flt f) {
      UInt l = storage.size();
      Flt * ptr1 = storage.data();
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

    buffer & buffer::copyFrom(const buffer & s, UInt sourcePos, UInt destPos, UInt length) {
      // TODO: don't just return if buffers are not long enough!
      if ((UInt)(sourcePos + length) > s.storage.size()) return (*this);
      if ((UInt)(destPos   + length) > storage.size()) return (*this);

      UInt l = length;
      Flt * ptr1 = storage.data() + destPos;
      const Flt * ptr2 = s.storage.data() + sourcePos;
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

    buffer & buffer::swap(buffer & s) {
      if (getLength() != s.getLength()) {
        // only swap with a buffer of the same length
        assert(false);
        return (*this);
      }

      UInt l = storage.size();
      Flt * ptr1 = storage.data();
      Flt * ptr2 = s.storage.data();
      Flt extra;

      for (; l > 7; l -= 8, ptr1 += 8, ptr2 += 8) {
        extra = ptr1[0]; ptr1[0] = ptr2[0]; ptr2[0] = extra;
        extra = ptr1[1]; ptr1[1] = ptr2[1]; ptr2[1] = extra;
        extra = ptr1[2]; ptr1[2] = ptr2[2]; ptr2[2] = extra;
        extra = ptr1[3]; ptr1[3] = ptr2[3]; ptr2[3] = extra;
        extra = ptr1[4]; ptr1[4] = ptr2[4]; ptr2[4] = extra;
        extra = ptr1[5]; ptr1[5] = ptr2[5]; ptr2[5] = extra;
        extra = ptr1[6]; ptr1[6] = ptr2[6]; ptr2[6] = extra;
        extra = ptr1[7]; ptr1[7] = ptr2[7]; ptr2[7] = extra;
      }

      while (l--) {
        extra = *ptr1; *ptr1++ = *ptr2; *ptr2++ = extra;
      }
      return (*this);
    }


    buffer & buffer::resize(UInt length, Flt value) {
      storage.resize(length + overflow, value);
      return (*this);
    }

    

    Flt YSE::DSP::buffer::getBack() {
      // TODO: what to do with the overflow?
      return storage.back();
    }

    /************************************************************************/


  } // namespace DSP
} // namespace YSE