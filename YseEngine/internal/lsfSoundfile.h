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

#ifdef YSE_WINDOWS
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

      Bool fillStream(Bool loop);

      SndfileHandle * handle;

    };

  }

}



#endif  // LSFSOUNDFILE_H_INCLUDED
#endif // LIBSOUNDFILE_BACKEND