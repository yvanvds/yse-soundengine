/*
 * Derived from music-synthesizer-for-android (MSFA):
 *   https://github.com/google/music-synthesizer-for-android
 * Ported into the YSE sound engine for issue #176. Adapted for engine
 * integration: wrapped in the YSE::DSP::msfa namespace; the Android/Apple
 * memory-barrier and ARM-NEON scaffolding of the upstream synth.h has been
 * removed (the desktop/engine build uses the scalar kernels only). The
 * upstream Apache-2.0 license header is preserved below; see the repository
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

#ifndef YSE_DSP_FM_MSFA_SYNTH_H
#define YSE_DSP_FM_MSFA_SYNTH_H

#include <stdint.h>

namespace YSE {
  namespace DSP {
    namespace msfa {

// The FM core processes in fixed blocks of N = 64 samples.
#define LG_N 6
#define N (1 << LG_N)

      template <typename T> inline static T min(const T& a, const T& b) {
        return a < b ? a : b;
      }

      template <typename T> inline static T max(const T& a, const T& b) {
        return a > b ? a : b;
      }

      // The engine build ships the portable scalar kernels; the upstream
      // ARM-NEON fast path is intentionally not compiled in.
      inline static bool hasNeon() {
        return false;
      }

    } // namespace msfa
  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_FM_MSFA_SYNTH_H
