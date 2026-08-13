/*
  ==============================================================================

    buffer.cpp
    Created: 28 Jan 2014 2:25:46pm
    Author:  yvan

  ==============================================================================
*/

#include <cassert>
#include <cstddef>
#include "buffer.hpp"

namespace YSE {
  namespace DSP {

    // don't move these constructors to the header file, because it will cause storage
    // vector to be allocated in the application memory instead of the dll memory, causing
    // a dll boundary crossing when resizing it later.
    buffer::buffer(UInt length, UInt overflow)
      : storage(length + overflow), sampleRateAdjustment(1.0f), overflow(overflow) {
      cursor = storage.data();
    }

    // Sizing storage here means operator= below finds the lengths already
    // matching and skips its resize(); overflow is adopted in the init list
    // because resize() reads it. Everything else a copy needs --
    // sampleRateAdjustment and cursor -- is settled by operator=, which is
    // what makes the copy fully defined instead of leaving those two members
    // holding whatever bytes the object's memory happened to contain
    // (issue #816).
    // cursor is settled by operator= below; it is initialised here as well
    // because resize() now reads it to rebase it (issue #818), and operator=
    // only skips that resize as long as this constructor keeps pre-sizing
    // storage to match the source.
    buffer::buffer(const buffer& cp)
      : cursor(nullptr), storage(cp.storage.size()), overflow(cp.overflow) {
      operator=(cp);
    }

    buffer& buffer::operator+=(Flt f) {
      UInt l = (UInt)storage.size();
      Flt* ptr = storage.data();
      for (; l > 7; l -= 8, ptr += 8) {
        ptr[0] += f;
        ptr[1] += f;
        ptr[2] += f;
        ptr[3] += f;
        ptr[4] += f;
        ptr[5] += f;
        ptr[6] += f;
        ptr[7] += f;
      }
      while (l--)
        *ptr++ += f;
      return (*this);
    }

    buffer& buffer::operator+=(const buffer& s) {
      // use length of shortest buffer to prevent memory errors
      UInt l = getLength() < s.getLength() ? getLength() : s.getLength();
      Flt* ptr1 = storage.data();
      const Flt* ptr2 = s.storage.data();

      for (; l > 7; l -= 8, ptr1 += 8, ptr2 += 8) {
        ptr1[0] += ptr2[0];
        ptr1[1] += ptr2[1];
        ptr1[2] += ptr2[2];
        ptr1[3] += ptr2[3];
        ptr1[4] += ptr2[4];
        ptr1[5] += ptr2[5];
        ptr1[6] += ptr2[6];
        ptr1[7] += ptr2[7];
      }
      while (l--)
        *ptr1++ += *ptr2++;

      copyOverflow();

      return (*this);
    }

    buffer& buffer::operator-=(Flt f) {
      UInt l = (UInt)storage.size();
      Flt* ptr = storage.data();
      for (; l > 7; l -= 8, ptr += 8) {
        ptr[0] -= f;
        ptr[1] -= f;
        ptr[2] -= f;
        ptr[3] -= f;
        ptr[4] -= f;
        ptr[5] -= f;
        ptr[6] -= f;
        ptr[7] -= f;
      }
      while (l--)
        *ptr++ -= f;
      return (*this);
    }

    buffer& buffer::operator-=(const buffer& s) {
      // use length of shortest buffer to prevent memory errors
      UInt l = getLength() < s.getLength() ? getLength() : s.getLength();
      Flt* ptr1 = storage.data();
      const Flt* ptr2 = s.storage.data();
      for (; l > 7; l -= 8, ptr1 += 8, ptr2 += 8) {
        ptr1[0] -= ptr2[0];
        ptr1[1] -= ptr2[1];
        ptr1[2] -= ptr2[2];
        ptr1[3] -= ptr2[3];
        ptr1[4] -= ptr2[4];
        ptr1[5] -= ptr2[5];
        ptr1[6] -= ptr2[6];
        ptr1[7] -= ptr2[7];
      }
      while (l--)
        *ptr1++ -= *ptr2++;

      copyOverflow();

      return (*this);
    }

    buffer& buffer::operator*=(Flt f) {
      UInt l = (UInt)storage.size();
      Flt* ptr = storage.data();
      for (; l > 7; l -= 8, ptr += 8) {
        ptr[0] *= f;
        ptr[1] *= f;
        ptr[2] *= f;
        ptr[3] *= f;
        ptr[4] *= f;
        ptr[5] *= f;
        ptr[6] *= f;
        ptr[7] *= f;
      }
      while (l--)
        *ptr++ *= f;
      return (*this);
    }

    buffer& buffer::operator*=(const buffer& s) {
      // use length of shortest buffer to prevent memory errors
      UInt l = getLength() < s.getLength() ? getLength() : s.getLength();
      Flt* ptr1 = storage.data();
      const Flt* ptr2 = s.storage.data();
      for (; l > 7; l -= 8, ptr1 += 8, ptr2 += 8) {
        ptr1[0] *= ptr2[0];
        ptr1[1] *= ptr2[1];
        ptr1[2] *= ptr2[2];
        ptr1[3] *= ptr2[3];
        ptr1[4] *= ptr2[4];
        ptr1[5] *= ptr2[5];
        ptr1[6] *= ptr2[6];
        ptr1[7] *= ptr2[7];
      }
      while (l--)
        *ptr1++ *= *ptr2++;

      copyOverflow();

      return (*this);
    }

    buffer& buffer::operator/=(Flt f) {
      UInt l = (UInt)storage.size();
      Flt* ptr = storage.data();
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
      while (l--)
        *ptr++ = (f ? *ptr / f : 0);
      return (*this);
    }

    buffer& buffer::operator/=(const buffer& s) {
      // use length of shortest buffer to prevent memory errors
      UInt l = getLength() < s.getLength() ? getLength() : s.getLength();
      Flt* ptr1 = storage.data();
      const Flt* ptr2 = s.storage.data();
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

    buffer& buffer::operator=(const buffer& s) {
      // Adopt the source's tail length before resizing: resize() sizes storage
      // as length + overflow, so handing it the source's *storage* size (which
      // already includes the source's tail) made the destination one tail
      // longer than the source, and the copy below then ran off the end of the
      // source allocation (issue #814).
      overflow = s.overflow;

      if (storage.size() != s.storage.size()) {
        resize(s.getLength());
      }

      // Sizes match after the resize above, but bound the copy by the shorter
      // of the two anyway -- same defensive idiom as the operators above.
      UInt l = storage.size() < s.storage.size() ? (UInt)storage.size() : (UInt)s.storage.size();
      Flt* ptr1 = storage.data();
      const Flt* ptr2 = s.storage.data();

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
      while (l--)
        *ptr1++ = *ptr2++;

      // The rate ratio describes the samples that were just copied, so it has
      // to travel with them. Without this the copy constructor left it
      // indeterminate and copy-assignment silently kept the destination's own
      // stale ratio, so a copied sample played back at the wrong speed
      // (issue #816).
      sampleRateAdjustment = s.sampleRateAdjustment;

      // cursor is a raw pointer into *this* buffer's storage. The source's
      // value addresses the source's allocation, and the resize() above may
      // have moved ours, so neither is usable here: park it at the start of
      // our own storage, exactly like the length constructor does.
      cursor = storage.data();

      return (*this);
    }

    buffer& buffer::operator=(Flt f) {
      UInt l = (UInt)storage.size();
      Flt* ptr1 = storage.data();
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
      while (l--)
        *ptr1++ = f;
      return (*this);
    }

    bool buffer::isSilent() const {
      UInt l = (UInt)storage.size();
      const Flt* ptr1 = storage.data();
      for (; l > 7; l -= 8, ptr1 += 8) {
        if (ptr1[0] != 0) return false;
        if (ptr1[1] != 0) return false;
        if (ptr1[2] != 0) return false;
        if (ptr1[3] != 0) return false;
        if (ptr1[4] != 0) return false;
        if (ptr1[5] != 0) return false;
        if (ptr1[6] != 0) return false;
        if (ptr1[7] != 0) return false;
      }
      while (l--)
        if (*ptr1++ != 0) return false;
      return true;
    }

    float buffer::maxValue() const {
      float max = -100.f;
      UInt l = (UInt)storage.size();
      const Flt* ptr1 = storage.data();
      for (; l > 7; l -= 8, ptr1 += 8) {
        if (ptr1[0] > max) max = ptr1[0];
        if (ptr1[1] > max) max = ptr1[1];
        if (ptr1[2] > max) max = ptr1[2];
        if (ptr1[3] > max) max = ptr1[3];
        if (ptr1[4] > max) max = ptr1[4];
        if (ptr1[5] > max) max = ptr1[5];
        if (ptr1[6] > max) max = ptr1[6];
        if (ptr1[7] > max) max = ptr1[7];
      }
      while (l--) {
        if (*ptr1 > max) max = *ptr1;
        ptr1++;
      }
      return max;
    }

    buffer& buffer::copyFrom(const buffer& s, UInt sourcePos, UInt destPos, UInt length) {
      // TODO: don't just return if buffers are not long enough!
      if ((UInt)(sourcePos + length) > s.storage.size()) return (*this);
      if ((UInt)(destPos + length) > storage.size()) return (*this);

      UInt l = length;
      Flt* ptr1 = storage.data() + destPos;
      const Flt* ptr2 = s.storage.data() + sourcePos;
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
      while (l--)
        *ptr1++ = *ptr2++;

      return (*this);
    }

    buffer& buffer::swap(buffer& s) {
      if (getLength() != s.getLength()) {
        // only swap with a buffer of the same length
        assert(false);
        return (*this);
      }

      UInt l = (UInt)storage.size();
      Flt* ptr1 = storage.data();
      Flt* ptr2 = s.storage.data();
      Flt extra;

      for (; l > 7; l -= 8, ptr1 += 8, ptr2 += 8) {
        extra = ptr1[0];
        ptr1[0] = ptr2[0];
        ptr2[0] = extra;
        extra = ptr1[1];
        ptr1[1] = ptr2[1];
        ptr2[1] = extra;
        extra = ptr1[2];
        ptr1[2] = ptr2[2];
        ptr2[2] = extra;
        extra = ptr1[3];
        ptr1[3] = ptr2[3];
        ptr2[3] = extra;
        extra = ptr1[4];
        ptr1[4] = ptr2[4];
        ptr2[4] = extra;
        extra = ptr1[5];
        ptr1[5] = ptr2[5];
        ptr2[5] = extra;
        extra = ptr1[6];
        ptr1[6] = ptr2[6];
        ptr2[6] = extra;
        extra = ptr1[7];
        ptr1[7] = ptr2[7];
        ptr2[7] = extra;
      }

      while (l--) {
        extra = *ptr1;
        *ptr1++ = *ptr2;
        *ptr2++ = extra;
      }
      return (*this);
    }

    buffer& buffer::resize(UInt length, Flt value) {
      // Growing past the current capacity makes the vector reallocate and free
      // the old block, and cursor addresses that block -- a caller that parked
      // a position before the resize was reading through a dangling pointer
      // afterwards (issue #818). resize() keeps the samples it does not drop,
      // so the position stays meaningful: remember the offset here and re-park
      // the cursor at the same sample on the new storage below. (Copying is the
      // other case, and it answers the same question the other way for the same
      // reason: operator= replaces every sample, so no old position survives
      // and the cursor goes back to the start -- issue #816.)
      //
      // This costs two compares and a subtraction, no allocation and no extra
      // branch on the render path; the resize itself stays the capacity-
      // retaining vector::resize the DSP nodes call every block.
      const Flt* oldData = storage.data();
      std::size_t offset = 0;
      // A cursor parked outside our own storage is not ours to rebase (and
      // differencing unrelated pointers is undefined): fall back to the start.
      if (cursor >= oldData && cursor <= oldData + storage.size()) {
        offset = static_cast<std::size_t>(cursor - oldData);
      }

      storage.resize(length + overflow, value);

      // Shrinking can drop the sample the cursor sat on; clamp to the new end.
      if (offset > storage.size()) offset = storage.size();
      cursor = storage.data() + offset;

      return (*this);
    }

    Flt YSE::DSP::buffer::getBack() {
      // TODO: what to do with the overflow?
      return storage.back();
    }

    /************************************************************************/

  } // namespace DSP
} // namespace YSE