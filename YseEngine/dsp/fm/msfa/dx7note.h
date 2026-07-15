/*
 * Derived from music-synthesizer-for-android (MSFA):
 *   https://github.com/google/music-synthesizer-for-android
 * Ported into the YSE sound engine for issue #176. Wrapped in the
 * YSE::DSP::msfa namespace; the init() parameter is spelled patch[156] to
 * match the unpacked-voice length the implementation actually reads. Upstream
 * Apache-2.0 license header preserved below; see the repository NOTICE file
 * for attribution.
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

#ifndef YSE_DSP_FM_MSFA_DX7NOTE_H
#define YSE_DSP_FM_MSFA_DX7NOTE_H

// This is the logic to put together a note from the MIDI description
// and run the low-level modules.

#include "env.h"
#include "pitchenv.h"
#include "fm_core.h"
#include "controllers.h"

namespace YSE {
  namespace DSP {
    namespace msfa {

      class Dx7Note {
      public:
        void init(const char patch[156], int midinote, int velocity);

        // Note: this _adds_ to the buffer.
        void compute(int32_t* buf, int32_t lfo_val, int32_t lfo_delay, const Controllers* ctrls);

        void keyup();

      private:
        FmCore core_;
        Env env_[6];
        FmOpParams params_[6];
        PitchEnv pitchenv_;
        int32_t basepitch_[6];
        int32_t fb_buf_[2];
        int32_t fb_shift_;

        int algorithm_;
        int pitchmoddepth_;
        int pitchmodsens_;
      };

    } // namespace msfa
  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_FM_MSFA_DX7NOTE_H
