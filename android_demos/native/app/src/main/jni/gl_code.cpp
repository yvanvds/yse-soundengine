/*
 * Copyright (C) 2009 The Android Open Source Project
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

// OpenGL ES 2.0 code

#include <jni.h>

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <stdio.h>
#include <stdlib.h>

#include "yse.hpp"


// inherit your dsp class from dspSource
class shepard : public YSE::DSP::dspSourceObject {
public:
    // process function is pure virtual in dspSource
    // you HAVE to implement it
    virtual void process(YSE::SOUND_STATUS & intent);
    // constructor can be implement if you need it
    // (you probably will)
    shepard();
    void frequency(Flt value);
    Flt frequency();

private:
    // in this case we add:
    // a sample buffer to hold the sum of all generators
    YSE::DSP::buffer out;
    // sinewave generators
    YSE::DSP::sine generators[11];
    // frequencies for all generators
    Flt freq[11];
    // the maximum frequency
    Flt top;
    // the current volume for output (this is adjusted according to SOUND_STATUS
    Flt volume;

    YSE::DSP::lowPass lp;

    Flt lpFreq;
    YSE::DSP::buffer s1, s2;
};

shepard::shepard() {
    // shepard tones are created with parallel octaves, so we double
    // the frequency for every generator
    freq[0] = 10;
    for (UInt i = 1; i < 11; i++) {
        freq[i] = freq[i - 1] * 2;
    }
    // the maximum frequency that can be reached
    top = freq[10] * 2;

    lp.setFrequency(1000);
    lpFreq = 500;

}

void shepard::process(YSE::SOUND_STATUS & intent) {
    // first clear the output buffer
    out = 0.0f;
    //out += generators[5](freq[5]);
    // add all sine generators to the output
    for (UInt i = 0; i < 11; i++) {
        out += generators[i](freq[i]);

        // adjust frequency for next run
        freq[i] = YSE::DSP::MidiToFreq(YSE::DSP::FreqToMidi(freq[i]) + 0.02f);
        // back down at maximum frequency
        if (freq[i] > top) freq[i] = 10;
    }
    // scale output volume
    out *= 0.1f;

    // most DSP object will return a reference to an AUDIOBUFFER.
    YSE::DSP::buffer & result = lp(out);

    // if you need to alter the result afterwards, you should not use a reference but
    // AUDIOBUFFER result = lp(out);
    // Note that this makes a deep copy of the object output, so use only when really needed
    // and preferably create the sample object when setting up your dsp object. Creating a
    // new sample in the process function will require memory allocation every time it runs.

    // copy buffer to all channels (YSE creates the buffer vector for your dsp, according to
    // the channels chosen for the current output device
    for (UInt i = 0; i < samples.size(); i++) {
        samples[i] = result;
    }

}

void shepard::frequency(Flt value) {
    lp.setFrequency(value);
    lpFreq = value;
}

Flt shepard::frequency() {
    return lpFreq;
}

shepard s;
YSE::sound sound;

void renderFrame() {
    static float grey;
    grey += 0.01f;
    if (grey > 1.0f) {
        grey = 0.0f;
    }
    glClearColor(grey, grey, grey, 1.0f);
    glClear( GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
}

extern "C" {
    JNIEXPORT void JNICALL Java_com_android_yseDemo_mainLib_init(JNIEnv * env, jobject obj);
    JNIEXPORT void JNICALL Java_com_android_yseDemo_mainLib_step(JNIEnv * env, jobject obj);
};

JNIEXPORT void JNICALL Java_com_android_yseDemo_mainLib_init(JNIEnv * env, jobject obj)
{
    YSE::System().init();
    sound.create(s).play();
}

JNIEXPORT void JNICALL Java_com_android_yseDemo_mainLib_step(JNIEnv * env, jobject obj)
{
    YSE::System().update();
    renderFrame();

}
