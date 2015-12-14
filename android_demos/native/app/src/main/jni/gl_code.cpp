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

}

JNIEXPORT void JNICALL Java_com_android_yseDemo_mainLib_step(JNIEnv * env, jobject obj)
{
    renderFrame();
}
