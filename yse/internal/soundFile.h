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
#include "../dsp/buffer.hpp"
#include "../headers/enums.hpp"
#include "customFileReader.h"
#include <forward_list>
#include "threadPool.h"

namespace YSE {

  namespace INTERNAL {

    enum FILESTATE {
      NEW,      // don't access from within the audio callback!
      LOADING,  // don't access from within the audio callback!
      READY,    // at this point a soundfile should only be accessed inside the audio callback
      INVALID,  // don't access from within the audio callback!
    };

    class soundFile : public threadPoolJob {
    public:

      Bool create(Bool stream = false);
      virtual void run(); // load from disk

      Bool read(std::vector<DSP::buffer> & filebuffer, Flt& pos, UInt length, Flt speed, Bool loop, SOUND_STATUS & intent, Flt & volume);

      Bool contains(const File & file);
      // alternative for custom IO()
      Bool contains(const char * fileName);
      Bool contains(YSE::DSP::buffer * buffer);
      Bool contains(MULTICHANNELBUFFER * buffer);

      // get
      Int       channels(); // number of channels in source
      UInt      length(); // length of the source in frames
      FILESTATE getState(); // current state of the file, (ready, invalid or loading)

      // set
      soundFile & reset(); // indicate that stream needs to reset (after stop)

      // to keep track of clients
      void attach(SOUND::implementationObject * impl);
      void release(SOUND::implementationObject * impl);
      bool inUse();

      soundFile(const File & file);
      // alternative for custom IO()
      soundFile(const char * fileName);
      soundFile(YSE::DSP::buffer * buffer);
      soundFile(MULTICHANNELBUFFER * buffer);
      ~soundFile();

      // used for passing juce BinaryData as a sound source
      Bool contains(juce::InputStream * source);
      soundFile(juce::InputStream * source);


    private:
      File              file;
      std::string fileName;

      // used for passing juce BinaryData as a sound source
      juce::InputStream * source;

      // used for passing an audio buffer as a sound source
      YSE::DSP::buffer * _audioBuffer;
      MULTICHANNELBUFFER * _multiChannelBuffer;

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
      ScopedPointer<AudioFormatReader> streamReader;

      Bool fillStream(Bool loop);
      void resetStream();

      // for keeping track of objects using this file
      
      std::forward_list<SOUND::implementationObject*> clientList;
      Flt idleTime;

      // for virtual IO
      //fileData _customFileData;

      //friend class SOUND::managerObject;
    };

  }
}



#endif  // SOUNDFILE_H_INCLUDED
