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

// A convenient wrapper for buffers with alignment constraints

#ifndef YSE_DSP_FM_MSFA_ALIGNED_BUF_H
#define YSE_DSP_FM_MSFA_ALIGNED_BUF_H

#include <stddef.h>
#include <stdint.h>

namespace YSE {
  namespace DSP {
    namespace msfa {

      template <typename T, size_t size, size_t alignment = 16> class AlignedBuf {
      public:
        T* get() {
          return (T*)((((intptr_t)storage_) + alignment - 1) & -alignment);
        }

      private:
        unsigned char storage_[size * sizeof(T) + alignment];
      };

    } // namespace msfa
  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_FM_MSFA_ALIGNED_BUF_H
