/*
  ==============================================================================

    portaudioDeviceManager.cpp
    Created: 27 Jul 2016 5:20:20pm
    Author:  yvan

  ==============================================================================
*/

#if PORTAUDIO_BACKEND

#include "portaudioDeviceManager.h"
#include "internalHeaders.h"
#include "internal/denormalGuard.h"
#ifdef __WINDOWS__
#include "pa_asio.h"
#endif

UInt YSE::SAMPLERATE = 44100;

YSE::DEVICE::managerObject & YSE::DEVICE::Manager() {
  static managerObject d;
  return d;
}

YSE::DEVICE::managerObject::managerObject()
  : stream(nullptr)
  , bufferPos(STANDARD_BUFFERSIZE)
  , initDone(false)
  , open(false)
  , started(false)
	, callbacksSinceLastUpdate(0)
{}

YSE::DEVICE::managerObject::~managerObject() {
  close();
  terminate();
}

int YSE::DEVICE::managerObject::paCallback(
    const void * /*input*/
  , void *output
  , unsigned long numSamples
  , const PaStreamCallbackTimeInfo* /*timeInfo*/
  , PaStreamCallbackFlags /*statusFlags*/
  , void * userData) {
  YSE::INTERNAL::enableFlushToZero();
  YSE::DEVICE::managerObject * manager = (YSE::DEVICE::managerObject *)userData;
	manager->callbacksSinceLastUpdate++;

  // PortAudio is opened with paFramesPerBufferUnspecified — capture whatever
  // framesPerBuffer the backend negotiated on the first callback so the live
  // getter can report it. Relaxed: any callback after the first will overwrite
  // with the same value when the device is stable.
  if (manager->activeBufferSize.load(std::memory_order_relaxed) == 0) {
    manager->activeBufferSize.store((int)numSamples, std::memory_order_release);
  }

  if (!manager->doOnCallback(numSamples)) return 0;

  UInt pos = 0;
  while (pos < static_cast<UInt>(numSamples)) {
    if (manager->bufferPos == STANDARD_BUFFERSIZE) {
      manager->renderOneBlock();
      manager->bufferPos = 0;
    }
    
    UInt size = (numSamples - pos) >(STANDARD_BUFFERSIZE - manager->bufferPos) ? (STANDARD_BUFFERSIZE - manager->bufferPos) : ((UInt)numSamples - pos);
    
    for (UInt i = 0; i < manager->master->out.size(); i++) {
      UInt l = size;
      Flt * ptr1 = ((Flt **)output)[i] + pos;
      Flt * ptr2 = manager->master->out[i].getPtr() + manager->bufferPos;

      for (; l > 7; l -= 8, ptr1 += 8, ptr2 += 8) {
        ptr1[0] = ptr2[0] < -1.f ? -1.f : ptr2[0] > 1.f ? 1.f : ptr2[0];
        ptr1[1] = ptr2[1] < -1.f ? -1.f : ptr2[1] > 1.f ? 1.f : ptr2[1];
        ptr1[2] = ptr2[2] < -1.f ? -1.f : ptr2[2] > 1.f ? 1.f : ptr2[2];
        ptr1[3] = ptr2[3] < -1.f ? -1.f : ptr2[3] > 1.f ? 1.f : ptr2[3];
        ptr1[4] = ptr2[4] < -1.f ? -1.f : ptr2[4] > 1.f ? 1.f : ptr2[4];
        ptr1[5] = ptr2[5] < -1.f ? -1.f : ptr2[5] > 1.f ? 1.f : ptr2[5];
        ptr1[6] = ptr2[6] < -1.f ? -1.f : ptr2[6] > 1.f ? 1.f : ptr2[6];
        ptr1[7] = ptr2[7] < -1.f ? -1.f : ptr2[7] > 1.f ? 1.f : ptr2[7];
      }
      while (l--) {
        *ptr1++ = *ptr2 < -1.f ? -1.f : *ptr2 > 1.f ? 1.f : *ptr2;
        ptr2++;
      }
    }
    manager->bufferPos += size;
    pos += size;

  }

	
  return 0;
}


Bool YSE::DEVICE::managerObject::init(bool openDevice) {
  // Offline init skips Pa_Initialize entirely.  On Linux runners without
  // audio hardware (bare GHA Ubuntu), Pa_Initialize probes ALSA/JACK and
  // either hangs or leaves the runner in a state that takes the VM down
  // ~20 s later — even though no PortAudio stream is ever opened.
  // Skipping it is safe: openDevice / addCallback / openStream are the
  // only callers of PortAudio APIs downstream, and they're all gated on
  // openDevice as well.
  if (!openDevice) {
    return deviceManager::init(false);
  }

  if (!initDone) {
    err = Pa_Initialize();
    if (err != paNoError) {
      audioDeviceError(err);
      return false;
    }
    initDone = true;
  }

  deviceManager::init(true);
  return true;
}

void YSE::DEVICE::managerObject::addCallback() {
  // setup with default device
  PaStreamParameters params;
  params.device = Pa_GetDefaultOutputDevice();
  if (params.device == paNoDevice) {
    INTERNAL::LogImpl().emit(E_WARNING, "No default audio output device found.");
    return;
  }
  const PaDeviceInfo * info = Pa_GetDeviceInfo(params.device);
  params.channelCount = info->maxOutputChannels;
  params.sampleFormat = paFloat32 | paNonInterleaved;
  params.suggestedLatency = info->defaultHighOutputLatency;
#if defined(PA_USE_ASIO)
  if (Pa_GetHostApiInfo(info->hostApi)->type == paASIO) {
    long min, max, pref;
    PaAsio_GetAvailableLatencyValues(params.device, &min, &max, &pref, NULL);
    params.suggestedLatency = pref;
  }
#endif
  params.hostApiSpecificStreamInfo = nullptr;
  // Session-locked: once the lock is set at the end of system::initShared(),
  // SAMPLERATE can only be re-written to its current value (e.g. by
  // pause()/resume() cycles which reopen the stream against the same device).
  // A debug assert catches genuine mid-session rate-change attempts; the
  // write itself is skipped so SAMPLERATE-derived caches (LFO tables, reverb
  // tunings, ADSR breakpoints) stay coherent.
  {
    const UInt newRate = (UInt)info->defaultSampleRate;
    assert(!INTERNAL::Global().isSampleRateLocked() || newRate == SAMPLERATE);
    if (!INTERNAL::Global().isSampleRateLocked()) {
      SAMPLERATE = newRate;
    }
  }

  err = Pa_OpenStream(
    &stream
    , NULL
    , &params
    , SAMPLERATE
    , paFramesPerBufferUnspecified
    , paNoFlag
    , paCallback
    , this
  );

  if (err != paNoError) {
    audioDeviceError(err);
    return;
  } else open = true;

  // Cache the negotiated output latency in samples for the live API.
  if (const PaStreamInfo * sinfo = Pa_GetStreamInfo(stream)) {
    activeOutputLatencySamples.store(
      (int)(sinfo->outputLatency * (double)SAMPLERATE),
      std::memory_order_release);
  }

  err = Pa_StartStream(stream);
  if (err != paNoError) {
    audioDeviceError(err);
    return;
  } else started = true;

  {
    std::string hostName = Pa_GetHostApiInfo(Pa_GetDeviceInfo(params.device)->hostApi)->name;
    INTERNAL::LogImpl().emit(E_DEBUG, "Audio device: " + hostName +
      " @ " + std::to_string(SAMPLERATE) + " Hz" +
      ", suggested latency " + std::to_string((int)(params.suggestedLatency * 1000)) + " ms");
  }
}

void YSE::DEVICE::managerObject::close() {
  if (started) {
    err = Pa_StopStream(stream);
    if (err != paNoError) {
      audioDeviceError(err);
    }
    started = false;
  }

  if(open) {
    err = Pa_CloseStream(stream);
    if (err != paNoError) {
      audioDeviceError(err);
    }
    open = false;
  }

  // No active device — the live getters report 0 until the next open.
  activeBufferSize.store(0, std::memory_order_release);
  activeOutputLatencySamples.store(0, std::memory_order_release);
}

void YSE::DEVICE::managerObject::terminate() {
  if (initDone) {
    err = Pa_Terminate();
    if (err != paNoError) {
      audioDeviceError(err);
    }
    initDone = false;
  }
}

void YSE::DEVICE::managerObject::pause() {
	close();
}

void YSE::DEVICE::managerObject::resume() {
	addCallback();
}

unsigned int YSE::DEVICE::managerObject::GetCallbacksSinceLastUpdate() {
	unsigned int result = callbacksSinceLastUpdate;
	callbacksSinceLastUpdate = 0;
	return result;
}

void YSE::DEVICE::managerObject::updateDeviceList() {
  devices.clear();

  Int count = Pa_GetDeviceCount();
  if (count < 0) {
    audioDeviceError(count);
    return;
  }

  for (Int i = 0; i < count; i++) {
    const PaDeviceInfo * info = Pa_GetDeviceInfo(i);
    const PaHostApiInfo * hostInfo = Pa_GetHostApiInfo(info->hostApi);
    YSE::device d;

    d.setID(i);
    d.setName(info->name);
    d.setTypeName(hostInfo->name);

    for (Int j = 0; j < info->maxInputChannels; j++) {
      d.addInputChannelName(std::string("in ") + std::to_string(j + 1));
    }

    for (Int j = 0; j < info->maxOutputChannels; j++) {
      d.addOutputChannelName(std::string("out ") + std::to_string(j + 1));
    }

    d.setInputLatency((Int)info->defaultLowInputLatency);
    d.setOutputLatency((Int)info->defaultLowOutputLatency);
    d.addAvailableSampleRate((Int)info->defaultSampleRate);
    
    devices.push_back(d);

  }

	const PaHostApiInfo * hostInfo = Pa_GetHostApiInfo(Pa_GetDefaultHostApi());
	defaultTypeName = hostInfo->name;
	
	const PaDeviceInfo * deviceInfo = Pa_GetDeviceInfo(Pa_GetDefaultOutputDevice());
	if (deviceInfo != nullptr) {
		defaultDeviceName = deviceInfo->name;
	}
}

void YSE::DEVICE::managerObject::openDevice(const YSE::deviceSetup & object) {
  if (!initDone) return;
  close();

  PaStreamParameters params;
  params.device = object.out->getID();
  const PaDeviceInfo * info = Pa_GetDeviceInfo(params.device);
  params.channelCount = object.getOutputChannels();
  params.sampleFormat = paFloat32 | paNonInterleaved;
  params.suggestedLatency = info->defaultHighOutputLatency;
#if defined(PA_USE_ASIO)
  if (Pa_GetHostApiInfo(info->hostApi)->type == paASIO) {
    long min, max, pref;
    PaAsio_GetAvailableLatencyValues(params.device, &min, &max, &pref, NULL);
    params.suggestedLatency = pref;
  }
#endif
  params.hostApiSpecificStreamInfo = nullptr;
  // See note at the addCallback() writer above.
  {
    const UInt newRate = (UInt)info->defaultSampleRate;
    assert(!INTERNAL::Global().isSampleRateLocked() || newRate == SAMPLERATE);
    if (!INTERNAL::Global().isSampleRateLocked()) {
      SAMPLERATE = newRate;
    }
  }

  err = Pa_OpenStream(
    &stream
    , NULL
    , &params
    , SAMPLERATE
    , object.bufferSize == 0 ? paFramesPerBufferUnspecified : object.bufferSize
    , paNoFlag
    , paCallback
    , this
  );

  if (err != paNoError) {
    audioDeviceError(err);
    return;
  }
  else open = true;

  // Cache live device state. When the caller pinned a non-zero bufferSize we
  // can populate it immediately; otherwise the first paCallback captures it.
  if (object.bufferSize != 0) {
    activeBufferSize.store((int)object.bufferSize, std::memory_order_release);
  }
  if (const PaStreamInfo * sinfo = Pa_GetStreamInfo(stream)) {
    activeOutputLatencySamples.store(
      (int)(sinfo->outputLatency * (double)SAMPLERATE),
      std::memory_order_release);
  }

  err = Pa_StartStream(stream);
  if (err != paNoError) {
    audioDeviceError(err);
    return;
  }
  else started = true;
}

void YSE::DEVICE::managerObject::audioDeviceError(PaError /*error*/) {
  INTERNAL::LogImpl().emit(E_AUDIODEVICE, Pa_GetErrorText(err));
}

Flt YSE::DEVICE::managerObject::cpuLoad() {
  if (stream != nullptr) {
    return (Flt)Pa_GetStreamCpuLoad(stream);
  }
  return 0.f;
}

double YSE::DEVICE::managerObject::getActiveSampleRate() const {
  // SAMPLERATE retains its last value across close(); gate on the open flag so
  // consumers see a clean "no device" → 0 transition.
  return open ? (double)SAMPLERATE : 0.0;
}

int YSE::DEVICE::managerObject::getActiveBufferSize() const {
  return activeBufferSize.load(std::memory_order_acquire);
}

int YSE::DEVICE::managerObject::getActiveOutputLatency() const {
  return activeOutputLatencySamples.load(std::memory_order_acquire);
}

#endif // PORTAUDIO_BACKEND
