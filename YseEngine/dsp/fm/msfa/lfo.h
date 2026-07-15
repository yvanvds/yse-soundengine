/*
 * Derived from music-synthesizer-for-android (MSFA):
 *   https://github.com/google/music-synthesizer-for-android
 * Ported into the YSE sound engine for issue #176. Wrapped in the
 * YSE::DSP::msfa namespace; otherwise unchanged. Upstream Apache-2.0 license
 * header preserved below; see the repository NOTICE file for attribution.
 */

/*
 * Copyright 2013 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Low frequency oscillator, compatible with DX7

#ifndef YSE_DSP_FM_MSFA_LFO_H
#define YSE_DSP_FM_MSFA_LFO_H

#include <stdint.h>

namespace YSE {
  namespace DSP {
    namespace msfa {

      class Lfo {
      public:
        static void init(double sample_rate);
        void reset(const char params[6]);

        // result is 0..1 in Q24
        int32_t getsample();

        // result is 0..1 in Q24
        int32_t getdelay();

        void keydown();

      private:
        static uint32_t unit_;

        uint32_t phase_; // Q32
        uint32_t delta_;
        uint8_t waveform_;
        uint8_t randstate_;
        bool sync_;

        uint32_t delaystate_;
        uint32_t delayinc_;
        uint32_t delayinc2_;
      };

    } // namespace msfa
  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_FM_MSFA_LFO_H
