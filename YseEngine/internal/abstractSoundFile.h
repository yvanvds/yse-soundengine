/*
  ==============================================================================

    abstractSoundFile.h
    Created: 28 Jan 2014 11:49:20am
    Author:  yvan

  ==============================================================================
*/

#ifndef ABSTRACTSOUNDFILE_H_INCLUDED
#define ABSTRACTSOUNDFILE_H_INCLUDED

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

    class abstractSoundFile : public threadPoolJob {
    public:
      //abstractSoundFile(const File               & file    );
      abstractSoundFile(const std::string        & fileName, bool interleaved);
      
      abstractSoundFile(      YSE::DSP::buffer   * buffer);
      abstractSoundFile(      MULTICHANNELBUFFER * buffer);
      
      // will be called by constructor

      virtual ~abstractSoundFile();

      Bool create(Bool stream = false);
      void run(); // load from disk
      virtual void loadStreaming() = 0;
      virtual void loadNonStreaming() = 0;

      Bool read(std::vector<DSP::buffer> & filebuffer, Flt& pos, UInt length, Flt speed, Bool loop, SOUND_STATUS & intent, Flt & volume); 
      
      //Bool contains(const File & file);
      virtual Bool contains(const std::string        & fileName);
      virtual Bool contains(      YSE::DSP::buffer   * buffer  );
      virtual Bool contains(      MULTICHANNELBUFFER * buffer  );

      // get
      Int       channels(); // number of channels in source
      UInt      length  (); // length of the source in frames
      FILESTATE getState(); // current state of the file, (ready, invalid or loading)

      // set
      abstractSoundFile & reset(); // indicate that stream needs to reset (after stop)

      // to keep track of clients
      void attach (SOUND::implementationObject * impl);
      void release(SOUND::implementationObject * impl);
      bool inUse();

    protected:
      static Bool readNonInterleaved(abstractSoundFile * file, std::vector<DSP::buffer> & filebuffer, Flt& pos, UInt length, Flt speed, Bool loop, SOUND_STATUS & intent, Flt & volume);
      static Bool readInterleaved(abstractSoundFile * file, std::vector<DSP::buffer> & filebuffer, Flt& pos, UInt length, Flt speed, Bool loop, SOUND_STATUS & intent, Flt & volume);

      std::string fileName;

      // pointer to a (multichannel) soundfile
      Bool useInterleavedBuffer;
      const Flt ** _buffer; // this version is used when a non interleaved buffer is used
      Flt * _iBuffer; // this version is used when an interleaved buffer is used

      // used for passing an audio buffer as a sound source
      YSE::DSP::buffer   * _audioBuffer       ;
      MULTICHANNELBUFFER * _multiChannelBuffer;
      
      std::atomic<FILESTATE> state;
      
      Flt _sampleRateAdjustment;
      int _length;
      int _channels;

      // for streaming
      Bool _streaming ;
      UInt _streamSize;
      Bool _endReached;
      Int  _streamPos ;
      Bool _needsReset;
      

      virtual Bool fillStream(Bool loop) = 0;

      // for keeping track of objects using this file      
      std::forward_list<SOUND::implementationObject*> clientList;
      Flt idleTime;

    private:
      // default constructor should only be used internally
      abstractSoundFile(bool interleaved);
    };

  }
}



#endif  // ABSTRACTSOUNDFILE_H_INCLUDED
