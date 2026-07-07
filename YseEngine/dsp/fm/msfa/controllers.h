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

// State of MIDI controllers

#ifndef YSE_DSP_FM_MSFA_CONTROLLERS_H
#define YSE_DSP_FM_MSFA_CONTROLLERS_H

namespace YSE {
  namespace DSP {
    namespace msfa {

      const int kControllerPitch = 128;

      class Controllers {
      public:
        int values_[129];
      };

    } // namespace msfa
  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_FM_MSFA_CONTROLLERS_H
