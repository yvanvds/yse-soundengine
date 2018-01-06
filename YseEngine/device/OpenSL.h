#pragma once

#if YSE_ANDROID

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <stdlib.h>
#include <unistd.h>
#ifdef __cplusplus
extern "C" {
#endif


  //typedef struct _circular_buffer {
  //  char *buffer;
  //  int  wp;
  //  int rp;
  //  int size;
  //} circular_buffer;

  typedef struct opensl_stream {

    // engine interfaces
    SLObjectItf engineObject;
    SLEngineItf engineEngine;

    // output mix interfaces
    SLObjectItf outputMixObject;

    // buffer queue player interfaces
    SLObjectItf bqPlayerObject;
    SLPlayItf bqPlayerPlay;
    SLAndroidBufferQueueItf bqPlayerBufferQueue;
    SLEffectSendItf bqPlayerEffectSend;

    // recorder interfaces
    SLObjectItf recorderObject;
    SLRecordItf recorderRecord;
    SLAndroidSimpleBufferQueueItf recorderBufferQueue;

    // buffers
    short *outputBuffer;
    short *inputBuffer;
    short *recBuffer;
    short *playBuffer;

    //circular_buffer *outrb;
    //circular_buffer *inrb;

    // size of buffers
    int outBufSamples;
    int inBufSamples;

    double time;
    int inchannels;
    int outchannels;
    int   sr;

  } OPENSL_STREAM;

  /*
  Open the audio device with a given sampling rate (sr), input and output channels and IO buffer size
  in frames. Returns a handle to the OpenSL stream
  */
  OPENSL_STREAM* android_OpenAudioDevice(int sr, int inchannels, int outchannels, int bufferframes);
  /*
  Close the audio device
  */
  void android_CloseAudioDevice(OPENSL_STREAM *p);
  /*
  Read a buffer from the OpenSL stream *p, of size samples. Returns the number of samples read.
  */
  int android_AudioIn(OPENSL_STREAM *p, float *buffer, int size);
  /*
  Write a buffer to the OpenSL stream *p, of size samples. Returns the number of samples written.
  */
  int android_AudioOut(OPENSL_STREAM *p, float *buffer, int size);

  void android_RegisterOutputCallback(OPENSL_STREAM *p, SLresult(*callback)(SLAndroidBufferQueueItf caller, void *context, void *pBufferContext, void *pBufferData, SLuint32 dataSize, SLuint32 dataUsed, const SLAndroidBufferItem *pItems, SLuint32 itemsLength));
  /*
  Get the current IO block time in seconds
  */
  double android_GetTimestamp(OPENSL_STREAM *p);

#ifdef __cplusplus
};
#endif

#endif
