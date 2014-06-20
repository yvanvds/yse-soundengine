/*
  ==============================================================================

    directsoundDevice.cpp
    Created: 16 Jun 2014 8:40:47pm
    Author:  yvan

  ==============================================================================
*/

#include "directsoundDevice.h"
#include "../../../headers/constants.hpp"

const int DXBUFFERSIZE = (YSE::STANDARD_BUFFERSIZE*3) << 2;

YSE::IO::directSoundDevice::directSoundDevice(const std::wstring & name, LPGUID id) : 
ioDevice(name), deviceObject(nullptr), outputBuffer(nullptr) {
  if (id == nullptr) {
    setDefault(true);
  }
  else {
    this->id = (*id);
  }
}

YSE::IO::directSoundDevice::~directSoundDevice() {
  close();
}

bool YSE::IO::directSoundDevice::open() {
  if (deviceObject != nullptr) {
    // this device is already open!
    assert(false);
    return false;
  }

  {
    HRESULT hr;
    if (isDefault()) {
      hr = DirectSoundCreate8(nullptr, &deviceObject, nullptr);
    }
    else {
      hr = DirectSoundCreate8(&id, &deviceObject, nullptr);
    }
    if (FAILED(hr)) {
      assert(false);
      setLastError(L"DirectSound: error while creating device");
      close();
      return false;
    }
  }

  {
    HRESULT hr = deviceObject->SetCooperativeLevel(GetDesktopWindow(), DSSCL_PRIORITY);
    if (FAILED(hr)) {
      assert(false);
      setLastError(L"DirectSound: error while setting priority level");
      close();
      return false;
    }
  }

  {
    LPDIRECTSOUNDBUFFER primaryBuffer;

    DSBUFFERDESC bufferDescription = { 0 };
    bufferDescription.dwSize = sizeof(DSBUFFERDESC);
    bufferDescription.dwFlags = DSBCAPS_PRIMARYBUFFER;
    bufferDescription.dwBufferBytes = 0;
    bufferDescription.lpwfxFormat = nullptr;

    HRESULT hr = deviceObject->CreateSoundBuffer(&bufferDescription, &primaryBuffer, nullptr);
    if (FAILED(hr)) {
      assert(false);
      setLastError(L"DirectSound: error while creating primary buffer");
      close();
      return false;
    }

    WAVEFORMATEX wf;
    memset(&wf, 0, sizeof(WAVEFORMATEX));
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = 2;
    wf.nSamplesPerSec = YSE::SAMPLERATE;
    wf.wBitsPerSample = (unsigned short)16;
    wf.nBlockAlign = (unsigned short)(wf.nChannels * wf.wBitsPerSample / 8);
    wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;
    wf.cbSize = 0;

    hr = primaryBuffer->SetFormat(&wf);
    if (FAILED(hr)) {
      assert(false);
      setLastError(L"DirectSound: error while setting format for primary buffer");
      close();
      return false;
    }

    DSBUFFERDESC secondaryDescription = { 0 };
    secondaryDescription.dwSize = sizeof (DSBUFFERDESC);
    secondaryDescription.dwFlags = DSBCAPS_GLOBALFOCUS | DSBCAPS_GETCURRENTPOSITION2;
    secondaryDescription.dwBufferBytes = DXBUFFERSIZE;
    secondaryDescription.lpwfxFormat = &wf;

    hr = deviceObject->CreateSoundBuffer(&secondaryDescription, &outputBuffer, nullptr);
    if (FAILED(hr)) {
      assert(false);
      setLastError(L"DirectSound: error while creating secondary buffer");
      close();
      return false;
    }

    DWORD datalength;
    unsigned char* bufferData;
    hr = outputBuffer->Lock(0, (DWORD)(DXBUFFERSIZE), (LPVOID*)&bufferData, &datalength, 0, 0, 0);
    if (FAILED(hr)) {
      assert(false);
      setLastError(L"DirectSound: error while locking output buffer");
      close();
      return false;
    }

    memset(bufferData, 0, datalength);
    hr = outputBuffer->Unlock(bufferData, datalength, 0, 0);
    if (FAILED(hr)) {
      assert(false);
      setLastError(L"DirectSound: error while unlocking output buffer");
      close();
      return false;
    }

    hr = outputBuffer->SetCurrentPosition(0);
    if (FAILED(hr)) {
      assert(false);
      setLastError(L"DirectSound: error while positioning output buffer");
      close();
      return false;
    }

    hr = outputBuffer->Play(0, 0, DSBPLAY_LOOPING);
    if (FAILED(hr)) {
      assert(false);
      setLastError(L"DirectSound: error while starting output buffer");
      close();
      return false;
    }
  }

  // Todo: this hardcodes 2 output channels, adjust this later
  outBuffers.emplace_back(STANDARD_BUFFERSIZE);
  outBuffers.emplace_back(STANDARD_BUFFERSIZE);

  isOpen = true;
  return true;
}

void YSE::IO::directSoundDevice::close() {
  stop();

  if (outputBuffer != nullptr) {
    HRESULT hr = outputBuffer->Stop();
    if (FAILED(hr)) {
      assert(false);
      setLastError(L"DirectSound: error while stopping output buffer");
    }

    outputBuffer->Release();
    outputBuffer = nullptr;
  }

  if (deviceObject != nullptr) {
    deviceObject->Release();
    deviceObject = nullptr;
  }
  isOpen = false;
}

void YSE::IO::directSoundDevice::stop() {
  if (isStarted) {
    std::lock_guard<std::mutex> lock(audiolock);
    callback->onStop();
    thread::stop();
  }
}

void YSE::IO::directSoundDevice::start(ioCallback * callback) {
  if (isOpen && callback != nullptr && !isStarted) {
    this->callback = callback;
    this->callback->onStart();
    thread::start();
    isStarted = true;
  }
  else {
    // this should not happen
    assert(false);
  }
}

void YSE::IO::directSoundDevice::run() {

  // fill first buffer
  callback->onCallback(inBuffers, outBuffers);
  int writePos = 0; // this contains the last position a sample was written to

  while (!threadShouldExit()) {
    {
      // make sure the output buffer exists
      if (outputBuffer == nullptr) {
        assert(false);
      }

      

      // get current position
      DWORD playCursor, writeCursor;
      for (;;) {
        HRESULT hr = outputBuffer->GetCurrentPosition(&playCursor, &writeCursor);
        if (hr == MAKE_HRESULT(1, 0x878, 150)) { // DSERR_BUFFERLOST
          outputBuffer->Restore();
          continue;
        }

        if (SUCCEEDED(hr)) break;

        assert(false);
        return;
      }

      int playWriteGap = (int)(writeCursor - playCursor);
      if (playWriteGap < 0) playWriteGap += DXBUFFERSIZE;

      int bytesEmpty = (int)(playCursor - writePos);
      if (bytesEmpty < 0) bytesEmpty += DXBUFFERSIZE;

      if (bytesEmpty >(DXBUFFERSIZE - playWriteGap)) {
        writePos = writeCursor;
        bytesEmpty = DXBUFFERSIZE - playWriteGap;
      }

      // fill if needed
      if (bytesEmpty >=  STANDARD_BUFFERSIZE << 2) {
        int* buf1 = nullptr;
        int* buf2 = nullptr;
        DWORD dwSize1 = 0;
        DWORD dwSize2 = 0;

        // request buffer
        for (;;) {
          HRESULT hr = outputBuffer->Lock(writePos, (DWORD)(STANDARD_BUFFERSIZE << 2), (void**)&buf1, &dwSize1, (void**)&buf2, &dwSize2, 0);
          if (hr == MAKE_HRESULT(1, 0x878, 150)) { // DSERR_BUFFERLOST
            outputBuffer->Restore();
            continue;
          }
          if (SUCCEEDED(hr)) break;

          assert(false);
          return;
        }

        const float * left = outBuffers[0].getBuffer();
        const float * right = outBuffers[1].getBuffer();

        int samples1 = (int)(dwSize1 >> 2);
        int samples2 = (int)(dwSize2 >> 2);

        for (int* dest = buf1; --samples1 >= 0;) *dest++ = convertValues(*left++, *right++);
        for (int* dest = buf2; --samples2 >= 0;) *dest++ = convertValues(*left++, *right++);

        outputBuffer->Unlock(buf1, dwSize1, buf2, dwSize2);

        writePos = (writePos + dwSize1 + dwSize2) % DXBUFFERSIZE;
        

        // refill buffer for next time
        callback->onCallback(inBuffers, outBuffers);
      } 
      else {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        
      }
    }
  }
}