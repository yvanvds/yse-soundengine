#pragma once

#ifdef YSE_ANDROID

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include "../headers\types.hpp"

const int ANDROID_BUFFER_SIZE = 256;

class OpenSLImplementation {
public:
  OpenSLImplementation();
 ~OpenSLImplementation();
  
  bool Setup();
  bool Start(int channels);
  void Stop();
  
  static void SoundPlayerCallback(SLAndroidSimpleBufferQueueItf aSoundQueue, void * aContext);
  void SendSoundBuffer();

private:
  void StopPlayer();
  void PrepareSoundBuffer();
  void SwapSoundBuffers();

  SLObjectItf mEngineObject;
  SLEngineItf mEngine;

  SLObjectItf mOutputMixObj;

  SLObjectItf mSoundPlayerObj;
  SLPlayItf mSoundPlayer;
  SLVolumeItf mSoundVolume;
  SLAndroidSimpleBufferQueueItf mSoundQueue;

  sl_int16_t * buffer1;
  sl_int16_t * buffer2;
  sl_int16_t * currentBuffer;
  
  UInt bufferPos;
  int numChannels;
  float ** sourceChannels;
};

#endif