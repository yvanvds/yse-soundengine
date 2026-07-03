/*
  ==============================================================================

    lsfSoundfile.h
    Created: 28 Jul 2016 9:30:04pm
    Author:  yvan

  ==============================================================================
*/

#if LIBSOUNDFILE_BACKEND

#ifndef LSFSOUNDFILE_H_INCLUDED
#define LSFSOUNDFILE_H_INCLUDED

#ifdef _MSC_VER
#pragma comment(lib, "libsndfile-1.lib")
#endif



#include "abstractSoundFile.h"
#include "sndfile.hh"

namespace YSE {

  namespace INTERNAL {

    class soundFile : public abstractSoundFile {
    public:
      soundFile(const std::string  & fileName);
      soundFile(const std::string & ID, char * fileBuffer, int length);
      soundFile(YSE::DSP::buffer   * buffer);
      soundFile(MULTICHANNELBUFFER * buffer);
      ~soundFile();

      using abstractSoundFile::abstractSoundFile;

      virtual void loadStreaming(); // load from disk
      virtual void loadNonStreaming();

    private:

      // Fill `dest` with up to STREAM_BUFFERSIZE frames from the current handle
      // position, zero-padding the tail at non-loop EOF. Returns the number of
      // real (non-padded) frames written (== STREAM_BUFFERSIZE unless EOF was hit
      // without looping). Does blocking disk I/O — slow pool / load only, never
      // the audio thread (issue #185).
      UInt fillBuffer(Flt * dest, Bool loop);

      // Slow-pool entry point: refill the back buffer and publish it. Overrides
      // the base hook driven by _refillJob.
      void fillBackBuffer() override;

      SndfileHandle * handle;

    };

  }

}



#endif  // LSFSOUNDFILE_H_INCLUDED
#endif // LIBSOUNDFILE_BACKEND