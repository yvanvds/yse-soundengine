/*
opensl_io.c:
Android OpenSL input/output module
Copyright (c) 2012, Victor Lazzarini
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
* Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.
* Neither the name of the <organization> nor the
names of its contributors may be used to endorse or promote products
derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#if YSE_ANDROID

#include "OpenSL.h"
#include "../implementations\logImplementation.h"

#define CONV16BIT 32768
#define CONVMYFLT (1./32768.)
#define GRAIN 4

// static void bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *context);

//static SLresult bqPlayerCallback(
//  SLAndroidBufferQueueItf caller,
//  void *pCallbackContext,            /* input */
//  void *pBufferContext,              /* input */
//  void *pBufferData,                 /* input */
//  SLuint32 dataSize,                 /* input */
//  SLuint32 dataUsed,                 /* input */
// const SLAndroidBufferItem *pItems, /* input */
//  SLuint32 itemsLength               /* input */);
  

//static void bqRecorderCallback(SLAndroidSimpleBufferQueueItf bq, void *context);
//circular_buffer* create_circular_buffer(int bytes);
//int checkspace_circular_buffer(circular_buffer *p, int writeCheck);
//int read_circular_buffer_bytes(circular_buffer *p, char *out, int bytes);
//int write_circular_buffer_bytes(circular_buffer *p, const char *in, int bytes);
//void free_circular_buffer(circular_buffer *p);

// creates the OpenSL ES audio engine
static SLresult openSLCreateEngine(OPENSL_STREAM *p)
{
  SLresult result;
  // create engine
  result = slCreateEngine(&(p->engineObject), 0, NULL, 0, NULL, NULL);
  if (result != SL_RESULT_SUCCESS) goto  engine_end;

  // realize the engine 
  result = (*p->engineObject)->Realize(p->engineObject, SL_BOOLEAN_FALSE);
  if (result != SL_RESULT_SUCCESS) goto engine_end;

  // get the engine interface, which is needed in order to create other objects
  result = (*p->engineObject)->GetInterface(p->engineObject, SL_IID_ENGINE, &(p->engineEngine));
  if (result != SL_RESULT_SUCCESS) goto  engine_end;

engine_end:
  return result;
}

// opens the OpenSL ES device for output
static SLresult openSLPlayOpen(OPENSL_STREAM *p)
{
  SLresult result;
  SLuint32 sr = p->sr;
  SLuint32  channels = p->outchannels;

  if (channels) {
    // configure audio source
    

    switch (sr) {

    case 8000:
      sr = SL_SAMPLINGRATE_8;
      break;
    case 11025:
      sr = SL_SAMPLINGRATE_11_025;
      break;
    case 16000:
      sr = SL_SAMPLINGRATE_16;
      break;
    case 22050:
      sr = SL_SAMPLINGRATE_22_05;
      break;
    case 24000:
      sr = SL_SAMPLINGRATE_24;
      break;
    case 32000:
      sr = SL_SAMPLINGRATE_32;
      break;
    case 44100:
      sr = SL_SAMPLINGRATE_44_1;
      break;
    case 48000:
      sr = SL_SAMPLINGRATE_48;
      break;
    case 64000:
      sr = SL_SAMPLINGRATE_64;
      break;
    case 88200:
      sr = SL_SAMPLINGRATE_88_2;
      break;
    case 96000:
      sr = SL_SAMPLINGRATE_96;
      break;
    case 192000:
      sr = SL_SAMPLINGRATE_192;
      break;
    default:
      return -1;
    }

    result = (*p->engineEngine)->CreateOutputMix(p->engineEngine, &(p->outputMixObject), 0, nullptr, nullptr);
    if (result != SL_RESULT_SUCCESS) {
      return result;
    }

    // realize the output mix
    result = (*p->outputMixObject)->Realize(p->outputMixObject, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
      return result;
    }

    SLDataLocator_AndroidSimpleBufferQueue queueLocator = { SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2 };
    
    SLuint32 speakers;
    //if (channels > 1)
    //  speakers = SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT;
    //else 
      speakers = SL_SPEAKER_FRONT_CENTER;
    
    SLDataFormat_PCM format_pcm = { SL_DATAFORMAT_PCM, channels, sr,
     SL_PCMSAMPLEFORMAT_FIXED_16, SL_PCMSAMPLEFORMAT_FIXED_16,
     speakers, SL_BYTEORDER_LITTLEENDIAN };

    SLDataSource audioSrc = { &queueLocator, &format_pcm };

    // configure audio sink
    SLDataLocator_OutputMix loc_outmix = { SL_DATALOCATOR_OUTPUTMIX, p->outputMixObject };
    SLDataSink audioSnk = { &loc_outmix, NULL };

    // create audio player
    const SLInterfaceID ids1[] = { SL_IID_ANDROIDBUFFERQUEUESOURCE };
    const SLboolean req1[] = { SL_BOOLEAN_TRUE };
    result = (*p->engineEngine)->CreateAudioPlayer(p->engineEngine, &(p->bqPlayerObject), &audioSrc, &audioSnk, 1, ids1, req1);
    if (result != SL_RESULT_SUCCESS) {
      return result;
    }

    // realize the player
    result = (*p->bqPlayerObject)->Realize(p->bqPlayerObject, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
      return result;
    }
    // get the play interface
    result = (*p->bqPlayerObject)->GetInterface(p->bqPlayerObject, SL_IID_PLAY, &(p->bqPlayerPlay));
    if (result != SL_RESULT_SUCCESS) {
      return result;
    }

    

    return result;
  }
  return SL_RESULT_SUCCESS;
}

void android_RegisterOutputCallback(OPENSL_STREAM * p, SLresult (*callback)(SLAndroidBufferQueueItf caller, void *context, void *pBufferContext, void *pBufferData, SLuint32 dataSize, SLuint32 dataUsed, const SLAndroidBufferItem *pItems, SLuint32 itemsLength)) {
  SLresult result;

  // get the buffer queue interface
  result = (*p->bqPlayerObject)->GetInterface(p->bqPlayerObject, SL_IID_ANDROIDBUFFERQUEUESOURCE,
    &(p->bqPlayerBufferQueue));
  if (result != SL_RESULT_SUCCESS) {
    YSE::INTERNAL::LogImpl().emit(YSE::E_ERROR, "OpenSL: failed to get queue interface");
    return;
  }

  // register callback on the buffer queue
  result = (*p->bqPlayerBufferQueue)->RegisterCallback(p->bqPlayerBufferQueue, callback, p);
  if (result != SL_RESULT_SUCCESS) {
    YSE::INTERNAL::LogImpl().emit(YSE::E_ERROR, "OpenSL: failed to register callback");
    return;
  }

  

  if ((p->playBuffer = (short *)calloc(p->outBufSamples, sizeof(short))) == NULL) {
    YSE::INTERNAL::LogImpl().emit(YSE::E_ERROR, "OpenSL: failed to open buffer for playing");
    return;
  }

  for (int i = 0; i < 3; i++) {
    (*p->bqPlayerBufferQueue)->Enqueue(p->bqPlayerBufferQueue, NULL,
      p->playBuffer, p->outBufSamples * sizeof(short), NULL, 0);
  }
  // set the player's state to playing
  result = (*p->bqPlayerPlay)->SetPlayState(p->bqPlayerPlay, SL_PLAYSTATE_PLAYING);
  if (result != SL_RESULT_SUCCESS) {
    YSE::INTERNAL::LogImpl().emit(YSE::E_ERROR, "OpenSL: failed to start playing");

    return;
  }
  YSE::INTERNAL::LogImpl().emit(YSE::E_DEBUG, "OpenSL: registered callback");

}
//
//// Open the OpenSL ES device for input
//static SLresult openSLRecOpen(OPENSL_STREAM *p) {
//
//  SLresult result;
//  SLuint32 sr = p->sr;
//  SLuint32 channels = p->inchannels;
//
//  if (channels) {
//
//    switch (sr) {
//
//    case 8000:
//      sr = SL_SAMPLINGRATE_8;
//      break;
//    case 11025:
//      sr = SL_SAMPLINGRATE_11_025;
//      break;
//    case 16000:
//      sr = SL_SAMPLINGRATE_16;
//      break;
//    case 22050:
//      sr = SL_SAMPLINGRATE_22_05;
//      break;
//    case 24000:
//      sr = SL_SAMPLINGRATE_24;
//      break;
//    case 32000:
//      sr = SL_SAMPLINGRATE_32;
//      break;
//    case 44100:
//      sr = SL_SAMPLINGRATE_44_1;
//      break;
//    case 48000:
//      sr = SL_SAMPLINGRATE_48;
//      break;
//    case 64000:
//      sr = SL_SAMPLINGRATE_64;
//      break;
//    case 88200:
//      sr = SL_SAMPLINGRATE_88_2;
//      break;
//    case 96000:
//      sr = SL_SAMPLINGRATE_96;
//      break;
//    case 192000:
//      sr = SL_SAMPLINGRATE_192;
//      break;
//    default:
//      return -1;
//    }
//
//    // configure audio source
//    SLDataLocator_IODevice loc_dev = { SL_DATALOCATOR_IODEVICE, SL_IODEVICE_AUDIOINPUT,
//      SL_DEFAULTDEVICEID_AUDIOINPUT, NULL };
//    SLDataSource audioSrc = { &loc_dev, NULL };
//
//    // configure audio sink
//    int speakers;
//    if (channels > 1)
//      speakers = SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT;
//    else speakers = SL_SPEAKER_FRONT_CENTER;
//    SLDataLocator_AndroidSimpleBufferQueue loc_bq = { SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2 };
//    SLDataFormat_PCM format_pcm = { SL_DATAFORMAT_PCM, channels, sr,
//      SL_PCMSAMPLEFORMAT_FIXED_16, SL_PCMSAMPLEFORMAT_FIXED_16,
//      speakers, SL_BYTEORDER_LITTLEENDIAN };
//    SLDataSink audioSnk = { &loc_bq, &format_pcm };
//
//    // create audio recorder
//    // (requires the RECORD_AUDIO permission)
//    const SLInterfaceID id[1] = { SL_IID_ANDROIDSIMPLEBUFFERQUEUE };
//    const SLboolean req[1] = { SL_BOOLEAN_TRUE };
//    result = (*p->engineEngine)->CreateAudioRecorder(p->engineEngine, &(p->recorderObject), &audioSrc,
//      &audioSnk, 1, id, req);
//    if (SL_RESULT_SUCCESS != result) goto end_recopen;
//
//    // realize the audio recorder
//    result = (*p->recorderObject)->Realize(p->recorderObject, SL_BOOLEAN_FALSE);
//    if (SL_RESULT_SUCCESS != result) goto end_recopen;
//
//    // get the record interface
//    result = (*p->recorderObject)->GetInterface(p->recorderObject, SL_IID_RECORD, &(p->recorderRecord));
//    if (SL_RESULT_SUCCESS != result) goto end_recopen;
//
//    // get the buffer queue interface
//    result = (*p->recorderObject)->GetInterface(p->recorderObject, SL_IID_ANDROIDSIMPLEBUFFERQUEUE,
//      &(p->recorderBufferQueue));
//    if (SL_RESULT_SUCCESS != result) goto end_recopen;
//
//    // register callback on the buffer queue
//    result = (*p->recorderBufferQueue)->RegisterCallback(p->recorderBufferQueue, bqRecorderCallback,
//      p);
//    if (SL_RESULT_SUCCESS != result) goto end_recopen;
//    result = (*p->recorderRecord)->SetRecordState(p->recorderRecord, SL_RECORDSTATE_RECORDING);
//
//    if ((p->recBuffer = (short *)calloc(p->inBufSamples, sizeof(short))) == NULL) {
//      return -1;
//    }
//
//    (*p->recorderBufferQueue)->Enqueue(p->recorderBufferQueue,
//      p->recBuffer, p->inBufSamples * sizeof(short));
//
//  end_recopen:
//    return result;
//  }
//  else return SL_RESULT_SUCCESS;
//
//
//}

// close the OpenSL IO and destroy the audio engine
static void openSLDestroyEngine(OPENSL_STREAM *p) {

  // destroy buffer queue audio player object, and invalidate all associated interfaces
  if (p->bqPlayerObject != NULL) {
    SLuint32 state = SL_PLAYSTATE_PLAYING;
    (*p->bqPlayerPlay)->SetPlayState(p->bqPlayerPlay, SL_PLAYSTATE_STOPPED);
    while (state != SL_PLAYSTATE_STOPPED)
      (*p->bqPlayerPlay)->GetPlayState(p->bqPlayerPlay, &state);
    (*p->bqPlayerObject)->Destroy(p->bqPlayerObject);
    p->bqPlayerObject = NULL;
    p->bqPlayerPlay = NULL;
    p->bqPlayerBufferQueue = NULL;
    p->bqPlayerEffectSend = NULL;
  }

  // destroy audio recorder object, and invalidate all associated interfaces
  if (p->recorderObject != NULL) {
    SLuint32 state = SL_PLAYSTATE_PLAYING;
    (*p->recorderRecord)->SetRecordState(p->recorderRecord, SL_RECORDSTATE_STOPPED);
    while (state != SL_RECORDSTATE_STOPPED)
      (*p->recorderRecord)->GetRecordState(p->recorderRecord, &state);
    (*p->recorderObject)->Destroy(p->recorderObject);
    p->recorderObject = NULL;
    p->recorderRecord = NULL;
    p->recorderBufferQueue = NULL;
  }

  // destroy output mix object, and invalidate all associated interfaces
  if (p->outputMixObject != NULL) {
    (*p->outputMixObject)->Destroy(p->outputMixObject);
    p->outputMixObject = NULL;
  }

  // destroy engine object, and invalidate all associated interfaces
  if (p->engineObject != NULL) {
    (*p->engineObject)->Destroy(p->engineObject);
    p->engineObject = NULL;
    p->engineEngine = NULL;
  }

}


// open the android audio device for input and/or output
OPENSL_STREAM *android_OpenAudioDevice(int sr, int inchannels, int outchannels, int bufferframes) {

  OPENSL_STREAM *p;
  p = (OPENSL_STREAM *)malloc(sizeof(OPENSL_STREAM));
  memset(p, 0, sizeof(OPENSL_STREAM));
  p->inchannels = inchannels;
  p->outchannels = outchannels;
  p->sr = sr;

  if ((p->outBufSamples = bufferframes*outchannels) != 0) {
    if ((p->outputBuffer = (short *)calloc(p->outBufSamples, sizeof(short))) == NULL) {
      android_CloseAudioDevice(p);
      return NULL;
    }
  }

  if ((p->inBufSamples = bufferframes*inchannels) != 0) {
    if ((p->inputBuffer = (short *)calloc(p->inBufSamples, sizeof(short))) == NULL) {
      android_CloseAudioDevice(p);
      return NULL;
    }
  }

  //if ((p->outrb = create_circular_buffer(p->outBufSamples * sizeof(short) * 4)) == NULL) {
  //  android_CloseAudioDevice(p);
  //  return NULL;
  //}
  //if ((p->inrb = create_circular_buffer(p->outBufSamples * sizeof(short) * 4)) == NULL) {
  //  android_CloseAudioDevice(p);
  //  return NULL;
  //}

  if (openSLCreateEngine(p) != SL_RESULT_SUCCESS) {
    android_CloseAudioDevice(p);
    return NULL;
  }

 /* if (openSLRecOpen(p) != SL_RESULT_SUCCESS) {
    android_CloseAudioDevice(p);
    return NULL;
  }*/

  if (openSLPlayOpen(p) != SL_RESULT_SUCCESS) {
    android_CloseAudioDevice(p);
    return NULL;
  }

  p->time = 0.;
  return p;
}

// close the android audio device
void android_CloseAudioDevice(OPENSL_STREAM *p) {

  if (p == NULL)
    return;

  openSLDestroyEngine(p);

  if (p->outputBuffer != NULL) {
    free(p->outputBuffer);
    p->outputBuffer = NULL;
  }

  if (p->inputBuffer != NULL) {
    free(p->inputBuffer);
    p->inputBuffer = NULL;
  }

  if (p->playBuffer != NULL) {
    free(p->playBuffer);
    p->playBuffer = NULL;
  }

  if (p->recBuffer != NULL) {
    free(p->recBuffer);
    p->recBuffer = NULL;
  }

  //free_circular_buffer(p->inrb);
  //free_circular_buffer(p->outrb);

  free(p);
}

// returns timestamp of the processed stream
double android_GetTimestamp(OPENSL_STREAM *p) {
  return p->time;
}


//// this callback handler is called every time a buffer finishes recording
//void bqRecorderCallback(SLAndroidSimpleBufferQueueItf bq, void *context)
//{
//  OPENSL_STREAM *p = (OPENSL_STREAM *)context;
//  int bytes = p->inBufSamples * sizeof(short);
//  write_circular_buffer_bytes(p->inrb, (char *)p->recBuffer, bytes);
//  (*p->recorderBufferQueue)->Enqueue(p->recorderBufferQueue, p->recBuffer, bytes);
//
//}
//
//// gets a buffer of size samples from the device
//int android_AudioIn(OPENSL_STREAM *p, float *buffer, int size) {
//  short *inBuffer;
//  int i, bytes = size * sizeof(short);
//  if (p == NULL || p->inBufSamples == 0) return 0;
//  bytes = read_circular_buffer_bytes(p->inrb, (char *)p->inputBuffer, bytes);
//  size = bytes / sizeof(short);
//  for (i = 0; i < size; i++) {
//    buffer[i] = (float)p->inputBuffer[i] * CONVMYFLT;
//  }
//  if (p->outchannels == 0) p->time += (double)size / (p->sr*p->inchannels);
//  return size;
//}





// this callback handler is called every time a buffer finishes playing

//SLresult bqPlayerCallback(
//  SLAndroidBufferQueueItf caller,
//  void *context,            /* input */
//  void *pBufferContext,              /* input */
//  void *pBufferData,                 /* input */
//  SLuint32 dataSize,                 /* input */
//  SLuint32 dataUsed,                 /* input */
//  const SLAndroidBufferItem *pItems, /* input */
//  SLuint32 itemsLength               /* input */)
//{
//  OPENSL_STREAM *p = (OPENSL_STREAM *)context;
//  int bytes = p->outBufSamples * sizeof(short);
//  read_circular_buffer_bytes(p->outrb, (char *)p->playBuffer, bytes);
//  (*p->bqPlayerBufferQueue)->Enqueue(p->bqPlayerBufferQueue, NULL,
//    p->playBuffer, bytes, NULL, 0);
//  return 0;
//}

// puts a buffer of size samples to the device
//int android_AudioOut(OPENSL_STREAM *p, float *buffer, int size) {
//
//  short *outBuffer, *inBuffer;
//  int i, bytes = size * sizeof(short);
//  if (p == NULL || p->outBufSamples == 0)  return 0;
//  for (i = 0; i < size; i++) {
//    p->outputBuffer[i] = (short)(buffer[i] * CONV16BIT);
//  }
//  bytes = write_circular_buffer_bytes(p->outrb, (char *)p->outputBuffer, bytes);
//  p->time += (double)size / (p->sr*p->outchannels);
//  return bytes / sizeof(short);
//}
//
//circular_buffer* create_circular_buffer(int bytes) {
//  circular_buffer *p;
//  if ((p = calloc(1, sizeof(circular_buffer))) == NULL) {
//    return NULL;
//  }
//  p->size = bytes;
//  p->wp = p->rp = 0;
//
//  if ((p->buffer = calloc(bytes, sizeof(char))) == NULL) {
//    free(p);
//    return NULL;
//  }
//  return p;
//}
//
//int checkspace_circular_buffer(circular_buffer *p, int writeCheck) {
//  int wp = p->wp, rp = p->rp, size = p->size;
//  if (writeCheck) {
//    if (wp > rp) return rp - wp + size - 1;
//    else if (wp < rp) return rp - wp - 1;
//    else return size - 1;
//  }
//  else {
//    if (wp > rp) return wp - rp;
//    else if (wp < rp) return wp - rp + size;
//    else return 0;
//  }
//}
//
//int read_circular_buffer_bytes(circular_buffer *p, char *out, int bytes) {
//  int remaining;
//  int bytesread, size = p->size;
//  int i = 0, rp = p->rp;
//  char *buffer = p->buffer;
//  if ((remaining = checkspace_circular_buffer(p, 0)) == 0) {
//    return 0;
//  }
//  bytesread = bytes > remaining ? remaining : bytes;
//  for (i = 0; i < bytesread; i++) {
//    out[i] = buffer[rp++];
//    if (rp == size) rp = 0;
//  }
//  p->rp = rp;
//  return bytesread;
//}
//
//int write_circular_buffer_bytes(circular_buffer *p, const char *in, int bytes) {
//  int remaining;
//  int byteswrite, size = p->size;
//  int i = 0, wp = p->wp;
//  char *buffer = p->buffer;
//  if ((remaining = checkspace_circular_buffer(p, 1)) == 0) {
//    return 0;
//  }
//  byteswrite = bytes > remaining ? remaining : bytes;
//  for (i = 0; i < byteswrite; i++) {
//    buffer[wp++] = in[i];
//    if (wp == size) wp = 0;
//  }
//  p->wp = wp;
//  return byteswrite;
//}
//
//void
//free_circular_buffer(circular_buffer *p) {
//  if (p == NULL) return;
//  free(p->buffer);
//  free(p);
//}

#endif