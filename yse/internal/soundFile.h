/*
  ==============================================================================

    soundFile.h
    Created: 28 Jan 2014 11:49:20am
    Author:  yvan

  ==============================================================================
*/

#ifndef SOUNDFILE_H_INCLUDED
#define SOUNDFILE_H_INCLUDED

#include "../headers/defines.hpp"
#include "../headers/types.hpp"
#include "../headers/enums.hpp"
#include "JuceHeader.h"
#include "../dsp/sample.hpp"

namespace YSE {
  namespace INTERNAL {

    enum FILESTATE {
      NEW,
      LOADING,
      READY,
      INVALID,
    };

    class soundFile {
    public:

      Bool create(const File &file, Bool stream = false);
      Bool read(std::vector<DSP::sample> & filebuffer, Flt& pos, UInt length, Flt speed, Bool loop, SOUND_STATUS & intent, Flt & volume);

      // get
      Int       channels(); // number of channels in source
      UInt      length(); // length of the source in frames
      FILESTATE state(); // current state of the file, (ready, invalid or loading)

      // set
      soundFile & reset(); // indicate that stream needs to reset (after stop)

      // to keep track of clients
      void release() { clients--; }
      bool inUse();

      soundFile();
      ~soundFile();

    private:
      File              _file;
      AudioSampleBuffer _buffer; // contains the actual sound buffer
      FILESTATE         _state;
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
      UInt clients;
      Flt idleTime;

      // for virtual IO
      //fileData _customFileData;

      friend class soundManager;
    };

  }
}



#endif  // SOUNDFILE_H_INCLUDED
