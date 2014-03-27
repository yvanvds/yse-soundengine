/*
  ==============================================================================

    soundFile.h
    Created: 28 Jan 2014 11:49:20am
    Author:  yvan

  ==============================================================================
*/

#ifndef SOUNDFILE_H_INCLUDED
#define SOUNDFILE_H_INCLUDED

#include "JuceHeader.h"
#include "../classes.hpp"
#include "../headers/types.hpp"
#include "../dsp/sample.hpp"
#include "../headers/enums.hpp"

namespace YSE {

  namespace INTERNAL {

    enum FILESTATE {
      NEW,      // don't access from within the audio callback!
      LOADING,  // don't access from within the audio callback!
      READY,    // at this point a soundfile should only be accessed inside the audio callback
      INVALID,  // don't access from within the audio callback!
    };

    class soundFile : public ThreadPoolJob {
    public:

      Bool create(Bool stream = false);
      JobStatus runJob(); // load from disk

      Bool read(std::vector<DSP::sample> & filebuffer, Flt& pos, UInt length, Flt speed, Bool loop, SOUND_STATUS & intent, Flt & volume);

      Bool contains(const File & file);

      // get
      Int       channels(); // number of channels in source
      UInt      length(); // length of the source in frames
      FILESTATE getState(); // current state of the file, (ready, invalid or loading)

      // set
      soundFile & reset(); // indicate that stream needs to reset (after stop)

      // to keep track of clients
      void release() { clients--; }
      bool inUse();

      soundFile(const File & file);
      ~soundFile();

    private:
      File              file;
      AudioSampleBuffer _buffer; // contains the actual sound buffer
      std::atomic<FILESTATE> state;
      Flt               _sampleRateAdjustment;
      int               _length;

      // for streaming
      Bool _streaming;
      UInt _streamSize;
      Bool _endReached;
      Int  _streamPos;
      Bool _needsReset;

      Bool fillStream(Bool loop);
      void resetStream();

      // for keeping track of objects using this file
      aUInt clients;
      Flt idleTime;

      // for virtual IO
      //fileData _customFileData;

      friend class SOUND::managerObject;
    };

  }
}



#endif  // SOUNDFILE_H_INCLUDED
