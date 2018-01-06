/*
  ==============================================================================

    granulator.cpp
    Created: 6 Sep 2015 4:51:57pm
    Author:  yvan

  ==============================================================================
*/

#include "granulator.hpp"
#include "../../utils/misc.hpp"
#include "../math_functions.h"
#include <iostream>

namespace YSE {
  namespace DSP {
    namespace MODULES {
      enum GRAIN_STATE {
        GS_START,
        GS_FADE_IN,
        GS_FADE_OUT,
        GS_SUSTAIN,
        GS_STOPPED,
      };

      class grain {
      public:
        grain(bool value) : needsInfo(value), state(GS_STOPPED) {}

        void start(UInt grainLength, UInt waitTime, Flt pitch, DSP::buffer * pool) {
          state = GS_START;
          this->grainLength = grainLength;
          this->waitTime = waitTime;
          this->pool = pool;
          this->pitch = pitch;

          // determine start position
          int offset = pool->cursor - pool->getPtr();
          int min = offset + this->waitTime;
          int max = offset + this->waitTime + pool->getLength() - grainLength;
          poolPos = min + BigRandom(max - min);
          while (poolPos > pool->getLength()) poolPos -= pool->getLength();

          this->currentGain = 0;
        }

        void process(DSP::buffer * pool, DSP::buffer * output) {
          if (state == GS_STOPPED) return;
          Flt * poolPtr = pool->getPtr();

          Flt * outPtr = output->getPtr();
          UInt n = output->getLength();

          if (state == GS_START) {
            while (n-- && --waitTime) outPtr++;
            if (waitTime <= 0) {
              state = GS_FADE_IN;
              n++;
            }
            else return;
          }

          if (state == GS_FADE_IN) {
            while (n-- && currentGain < 1 && (grainLength - 100) > 0) {
              *outPtr++ += poolPtr[(UInt)poolPos] * currentGain;
              currentGain += 0.005;
              poolPos += pitch;
              if (poolPos >= pool->getLength()) poolPos -= pool->getLength();
              grainLength--;
            }
            if (currentGain >= 1) {
              if (grainLength - 100 > 0) state = GS_SUSTAIN;
              else state = GS_FADE_OUT;
              n++;
            }
          }

          if (state == GS_SUSTAIN) {
            while (n-- && (grainLength - 100) > 0) {
              *outPtr++ += poolPtr[(UInt)poolPos];
              poolPos += pitch;
              if (poolPos >= pool->getLength()) poolPos -= pool->getLength();
              grainLength--;
            }
            if (grainLength  <= 100) {
              state = GS_FADE_OUT;
              n++;
            }
          }

          if (state == GS_FADE_OUT) {
            while (n-- && currentGain > 0) {
              *outPtr++ += poolPtr[(UInt)poolPos] * currentGain;
              currentGain -= 0.01;
              poolPos += pitch;
              if (poolPos >= pool->getLength()) poolPos -= pool->getLength();
              grainLength--;
            }
            if (currentGain <= 0) {
              state = GS_STOPPED;
            }
          }
         
        }

        bool needsInfo;
        UInt grainLength;
        Int waitTime;
        Flt pitch;
        DSP::buffer * pool;
        Flt poolPos;
        Flt currentGain;
        GRAIN_STATE state;
      };
    }
  }
}

YSE::DSP::MODULES::granulator::granulator(UInt poolSize, UInt maxGrains) 
: poolSize(poolSize), parmTranspose(0), parmTransposeRandom(0), parmGain(1.0f)
, leftOverStarts(0.f)
, maxGrains(maxGrains)
{
  limiter.set(-0.97, 0.97);
}

void YSE::DSP::MODULES::granulator::create() {
  pool.reset(new DSP::buffer(poolSize));
  (*pool) = 0;
  poolPosition = 0;
  readPos = 4000;

  grainPool.reset(new std::vector<grain>);
  for (unsigned int i = 0; i < maxGrains; i++) {
    grainPool->emplace_back(true);
  }
}

void YSE::DSP::MODULES::granulator::process(MULTICHANNELBUFFER & buffer) {
  createIfNeeded();
  
  // add current buffer to pool
  if (pool->getLength() - poolPosition >= buffer[0].getLength()) {
    pool->copyFrom(buffer[0], 0, poolPosition, buffer[0].getLength());
    poolPosition += buffer[0].getLength();
    if (poolPosition == pool->getLength()) poolPosition = 0;
  }
  else {
    UInt length = pool->getLength() - poolPosition;
    pool->copyFrom(buffer[0], 0, poolPosition, length);
    poolPosition = 0;
    pool->copyFrom(buffer[0], length, poolPosition, buffer[0].getLength() - length);
    poolPosition = buffer[0].getLength() - length;
  }

  pool->cursor = pool->getPtr() + poolPosition;

  
  // see how many grains should be started during this buffer
  Flt newGrains = parmFrequency / (SAMPLERATE / static_cast<Flt>(buffer[0].getLength()));
  leftOverStarts += newGrains - static_cast<Int>(newGrains);
  if (leftOverStarts > 1) {
    newGrains++;
    leftOverStarts--;
  }

  // activate grains if needed
  UInt num = static_cast<Int>(newGrains);
  if(num) for (UInt i = 0; i < grainPool->size(); i++) {
    if (grainPool->at(i).state == GS_STOPPED) {
      UInt length = parmLength;
      if (parmLengthRandom) length += Random(-(int)parmLengthRandom, parmLengthRandom);
      Int wait = YSE::BigRandom(poolSize);
      Flt pitch = 1 + parmTranspose;
      if (parmTransposeRandom > 0) pitch += RandomF(-parmTransposeRandom, parmTransposeRandom);
      grainPool->at(i).start(length, wait, pitch, pool.get());
      num--;
      if (!num) break;
    }
  }

  // create new grains if needed
  while (num--) {
    grainPool->emplace_back(true);
    UInt length = parmLength;
    if (parmLengthRandom) length += Random(-(int)parmLengthRandom, parmLengthRandom);
    Flt pitch = 1 + parmTranspose;
    if (parmTransposeRandom > 0) pitch += RandomF(-parmTransposeRandom, parmTransposeRandom);
    grainPool->back().start(length, YSE::BigRandom(poolSize), pitch, pool.get());
  }

  // zero input buffer
  buffer[0] = 0;

  // process grains
  for (UInt i = 0; i < grainPool->size(); i++) {
    grainPool->at(i).process(pool.get(), &buffer[0]);
  }

  // adjust gain
  buffer[0] *= parmGain;  
}

YSE::DSP::MODULES::granulator & YSE::DSP::MODULES::granulator::grainLength(UInt samples, UInt random ) {
  parmLength = samples;
  parmLengthRandom = random;
  return *this;
}

YSE::DSP::MODULES::granulator & YSE::DSP::MODULES::granulator::grainFrequency(UInt value) {
  parmFrequency = value;
  return *this;
}

YSE::DSP::MODULES::granulator & YSE::DSP::MODULES::granulator::grainTranspose(Flt pitch, Flt random) {
  parmTranspose = pitch;
  parmTransposeRandom = random;
  return *this;
}

YSE::DSP::MODULES::granulator & YSE::DSP::MODULES::granulator::gain(Flt value) {
  parmGain = value;
  return *this;
}