/*
 * Derived from music-synthesizer-for-android (MSFA):
 *   https://github.com/google/music-synthesizer-for-android
 * Ported into the YSE sound engine for issue #176. Wrapped in the
 * YSE::DSP::msfa namespace; the debug-only dump() method was removed.
 * Upstream Apache-2.0 license header preserved below; see the repository
 * NOTICE file for attribution.
 */

/*
 * Copyright 2012 Google Inc.
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

#ifndef YSE_DSP_FM_MSFA_FM_CORE_H
#define YSE_DSP_FM_MSFA_FM_CORE_H

#include <stdint.h>

#include "synth.h"
#include "aligned_buf.h"
#include "fm_op_kernel.h"
#include "controllers.h"

namespace YSE {
  namespace DSP {
    namespace msfa {

      struct FmOpParams {
        int32_t gain[2];
        int32_t freq;
        int32_t phase;
      };

      class FmCore {
      public:
        void compute(int32_t* output, FmOpParams* params, int algorithm, int32_t* fb_buf,
                     int32_t feedback_gain);

      private:
        AlignedBuf<int32_t, N> buf_[2];
      };

      // Number of DX7 FM algorithms.
      const int kNumAlgorithms = 32;

    } // namespace msfa
  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_FM_MSFA_FM_CORE_H
