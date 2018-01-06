/*
  ==============================================================================

    juceSoundFile.h
    Created: 28 Jul 2016 9:29:26pm
    Author:  yvan

  ==============================================================================
*/

#if JUCE_BACKEND

#ifndef JUCESOUNDFILE_H_INCLUDED
#define JUCESOUNDFILE_H_INCLUDED


#include "JuceHeader.h"
#include "../classes.hpp"
#include "../headers/types.hpp"
#include "../dsp/buffer.hpp"
#include "../headers/enums.hpp"
#include "customFileReader.h"
#include <forward_list>
#include "threadPool.h"
#include "abstractSoundFile.h"

namespace YSE {

  namespace INTERNAL {

    class soundFile : public abstractSoundFile {
    public:
      // inherit base class constructors
      using abstractSoundFile::abstractSoundFile;

      virtual void loadStreaming();
      virtual void loadNonStreaming();

    private:
      Bool fillStream(Bool loop);
      AudioSampleBuffer _fileBuffer; // contains the actual sound buffer
      ScopedPointer<AudioFormatReader> streamReader;
    };

  }
}



#endif  // JUCESOUNDFILE_H_INCLUDED
#endif JUCE_BACKEND