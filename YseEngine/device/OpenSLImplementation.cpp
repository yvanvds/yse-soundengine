#include "OpenSLImplementation.h"

#ifdef YSE_ANDROID 

#include "../implementations\logImplementation.h"
#include "../internalHeaders.h"
//#include <string.h>

#define CONV16BIT 32768

OpenSLImplementation::OpenSLImplementation()
  : buffer1(nullptr)
  , buffer2(nullptr)
  , bufferPos(YSE::STANDARD_BUFFERSIZE) 
  , sourceChannels(nullptr)
{
  mEngineObject = NULL;
  mEngine = NULL;

  mOutputMixObj = NULL;

  mSoundPlayerObj = NULL;
  mSoundPlayer = NULL;
  mSoundVolume = NULL;
  mSoundQueue = NULL;
}

OpenSLImplementation::~OpenSLImplementation() {
  Stop();
  if (buffer1 != nullptr) delete[] buffer1;
  if (buffer2 != nullptr) delete[] buffer2;
  if (sourceChannels != nullptr) delete[] sourceChannels;
}

bool OpenSLImplementation::Setup() {
  YSE::INTERNAL::LogImpl().emit(YSE::E_DEBUG, "OpenSL: Setting up Audio Interface");

  SLresult result;

  // engine
  const SLuint32 engineMixIIDCount = 1;
  const SLInterfaceID engineMixIDDs[] = { SL_IID_ENGINE };
  const SLboolean engineMixReqs[] = { SL_BOOLEAN_TRUE };

  // output mixer
  const SLuint32 outputMixIIDCount = 0;
  const SLInterfaceID outputMixIIDs[] = {};
  const SLboolean outputMixReqs[] = {};

  // create engine
  result = slCreateEngine(&mEngineObject, 0, NULL, engineMixIIDCount, engineMixIDDs, engineMixReqs);
  if (result != SL_RESULT_SUCCESS)  goto ERROR;

  // realize
  result = (*mEngineObject)->Realize(mEngineObject, SL_BOOLEAN_FALSE);
  if (result != SL_RESULT_SUCCESS)  goto ERROR;

  // get interfaces
  result = (*mEngineObject)->GetInterface(mEngineObject, SL_IID_ENGINE, &mEngine);
  if (result != SL_RESULT_SUCCESS)  goto ERROR;

  // create output
  result = (*mEngine)->CreateOutputMix(mEngine, &mOutputMixObj, outputMixIIDCount, outputMixIIDs, outputMixReqs);
  if (result != SL_RESULT_SUCCESS)  goto ERROR;
  result = (*mOutputMixObj)->Realize(mOutputMixObj, SL_BOOLEAN_FALSE);
  if (result != SL_RESULT_SUCCESS)  goto ERROR;

  return true;

ERROR:
  YSE::INTERNAL::LogImpl().emit(YSE::E_ERROR, "OpenSL: Setting up Audio Interface failed");
  Stop();
  return false;
}

bool OpenSLImplementation::Start(int channels) {
  YSE::INTERNAL::LogImpl().emit(YSE::E_DEBUG, "OpenSL: Starting Audio Interface");

  numChannels = channels;
  SLresult result;

  // input
  SLDataLocator_AndroidSimpleBufferQueue dataLocatorInput;
  dataLocatorInput.locatorType = SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE;
  dataLocatorInput.numBuffers = 2;

  // format of data
  SLDataFormat_PCM dataFormat;
  dataFormat.formatType = SL_DATAFORMAT_PCM;
  dataFormat.numChannels = numChannels;
  dataFormat.samplesPerSec = SL_SAMPLINGRATE_44_1;
  dataFormat.bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_16;
  dataFormat.containerSize = SL_PCMSAMPLEFORMAT_FIXED_16;

  if (channels > 1) {
    dataFormat.channelMask = SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT;
  }
  else {
    dataFormat.channelMask = SL_SPEAKER_FRONT_CENTER;
  }
  
  dataFormat.endianness = SL_BYTEORDER_LITTLEENDIAN;

  // combine location and format into source
  SLDataSource dataSource;
  dataSource.pLocator = &dataLocatorInput;
  dataSource.pFormat = &dataFormat;

  // output
  SLDataLocator_OutputMix dataLocatorOut;
  dataLocatorOut.locatorType = SL_DATALOCATOR_OUTPUTMIX;
  dataLocatorOut.outputMix = mOutputMixObj;

  SLDataSink dataSink;
  dataSink.pLocator = &dataLocatorOut;
  dataSink.pFormat = NULL;

  // create sound player
  const SLuint32 soundPlayerIIDCount = 3;
  const SLInterfaceID soundPlayerIIDs[] = { SL_IID_PLAY, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, SL_IID_VOLUME };
  const SLboolean soundPlayerReqs[] = { SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE };

  result = (*mEngine)->CreateAudioPlayer(mEngine, &mSoundPlayerObj, &dataSource, &dataSink, soundPlayerIIDCount, soundPlayerIIDs, soundPlayerReqs);
  if (result != SL_RESULT_SUCCESS)  goto ERROR;

  result = (*mSoundPlayerObj)->Realize(mSoundPlayerObj, SL_BOOLEAN_FALSE);
  if (result != SL_RESULT_SUCCESS)  goto ERROR;

  // get interfaces
  result = (*mSoundPlayerObj)->GetInterface(mSoundPlayerObj, SL_IID_PLAY, &mSoundPlayer);
  if (result != SL_RESULT_SUCCESS)  goto ERROR;

  result = (*mSoundPlayerObj)->GetInterface(mSoundPlayerObj, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &mSoundQueue);
  if (result != SL_RESULT_SUCCESS)  goto ERROR;

  result = (*mSoundPlayerObj)->GetInterface(mSoundPlayerObj, SL_IID_VOLUME, &mSoundVolume);
  if (result != SL_RESULT_SUCCESS)  goto ERROR;

  // register callback
  result = (*mSoundQueue)->RegisterCallback(mSoundQueue, SoundPlayerCallback, this);
  if (result != SL_RESULT_SUCCESS)  goto ERROR;

  // prepare mixer and enqueue 2 buffers
  buffer1 = new sl_int16_t[ANDROID_BUFFER_SIZE * channels];
  buffer2 = new sl_int16_t[ANDROID_BUFFER_SIZE * channels];
  memset(buffer1, 0, sizeof(sl_int16_t) * ANDROID_BUFFER_SIZE * channels);
  memset(buffer2, 0, sizeof(sl_int16_t) * ANDROID_BUFFER_SIZE * channels);
  currentBuffer = buffer1;
  sourceChannels = new float*[numChannels];

  // send two buffers
  SendSoundBuffer();
  SendSoundBuffer();

  // start playing
  result = (*mSoundPlayer)->SetPlayState(mSoundPlayer, SL_PLAYSTATE_PLAYING);
  if (result != SL_RESULT_SUCCESS)  goto ERROR;

  return true;

ERROR:
  YSE::INTERNAL::LogImpl().emit(YSE::E_ERROR, "OpenSL: Starting Audio Interface failed");
  Stop();
  return false;
}

void OpenSLImplementation::SoundPlayerCallback(SLAndroidSimpleBufferQueueItf aSoundQueue, void * aContext) {
  YSE::INTERNAL::LogImpl().emit(YSE::E_DEBUG, "OpenSL: Audio Callback Called");
  ((OpenSLImplementation*)aContext)->SendSoundBuffer();
}

void OpenSLImplementation::SendSoundBuffer() {
  SLuint32 result;

  PrepareSoundBuffer();
  result = (*mSoundQueue)->Enqueue(mSoundQueue, currentBuffer, sizeof(sl_int16_t) * ANDROID_BUFFER_SIZE * numChannels);
  if (result != SL_RESULT_SUCCESS) {
    YSE::INTERNAL::LogImpl().emit(YSE::E_ERROR, "OpenSL: enqueue method of sound buffer failed");
  }
  SwapSoundBuffers();
}

void OpenSLImplementation::StopPlayer() {
  
  if (mSoundPlayerObj != NULL) {
    SLuint32 soundPlayerState;
    (*mSoundPlayerObj)->GetState(mSoundPlayerObj, &soundPlayerState);

    if (soundPlayerState == SL_OBJECT_STATE_REALIZED) {
      (*mSoundQueue)->Clear(mSoundQueue);
      (*mSoundPlayerObj)->AbortAsyncOperation(mSoundPlayerObj);
      (*mSoundPlayerObj)->Destroy(mSoundPlayerObj);

      mSoundPlayerObj = NULL;
      mSoundPlayer = NULL;
      mSoundQueue = NULL;
      mSoundVolume = NULL;
    }
  }
}

void OpenSLImplementation::Stop() {
  // destroy sound player
  YSE::INTERNAL::LogImpl().emit(YSE::E_DEBUG, "OpenSL: Stopping Audio Player");
  StopPlayer();

  YSE::INTERNAL::LogImpl().emit(YSE::E_DEBUG, "OpenSL: Destroying sound output");
  if (mOutputMixObj != NULL) {
    (*mOutputMixObj)->Destroy(mOutputMixObj);
    mOutputMixObj = NULL;
  }

  YSE::INTERNAL::LogImpl().emit(YSE::E_DEBUG, "OpenSL: Destroying sound engine");
  if (mEngineObject != NULL) {
    (*mEngineObject)->Destroy(mEngineObject);
    mEngineObject = NULL;
    mEngine = NULL;
  }
}

void OpenSLImplementation::SwapSoundBuffers() {
  if (currentBuffer == buffer1) {
    currentBuffer = buffer2;
  }
  else {
    currentBuffer = buffer1;
  }
}

void OpenSLImplementation::PrepareSoundBuffer() {
  if (!YSE::DEVICE::Manager().doOnCallback(ANDROID_BUFFER_SIZE)) return;

  int pos = 0;
  int numSamples = ANDROID_BUFFER_SIZE;
  auto & master = YSE::DEVICE::Manager().getMaster();
  auto & out = master.GetBuffers();

  while (pos < numSamples) {
    if (bufferPos == YSE::STANDARD_BUFFERSIZE) {
      master.dsp();
      master.buffersToParent();
      bufferPos = 0;
    }

    UInt size = (numSamples - pos) > (YSE::STANDARD_BUFFERSIZE - bufferPos) ? (YSE::STANDARD_BUFFERSIZE - bufferPos) : ((UInt)numSamples - pos);

    UInt l = size;
    sl_int16_t * dest = (currentBuffer) + (pos * numChannels);

    for (UInt i = 0; i < numChannels; i++) {
      sourceChannels[i] = out[i].getPtr() + bufferPos;
    }

    while (l--) {
      for (UInt i = 0; i < numChannels; i++) {
        *dest++ = (short)((*sourceChannels[i]++) * CONV16BIT);
      }
    }

    bufferPos += size;
    pos += size;
  }
}

#endif
