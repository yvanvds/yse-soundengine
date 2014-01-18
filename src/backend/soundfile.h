#pragma once
#include <string>
#include <vector>
#include "headers/types.hpp"

// TODO: check if Windows.h is still needed
#ifdef  WINDOWS
  #include <Windows.h>
#endif

#if defined(USE_PORTAUDIO)
  #include "sndfile.h"
#elif defined(USE_OPENSL)
  #include <opensl.h>
#endif

#include "dsp/sample.hpp"
#include "filesysImpl.h"
#include "headers/enums.hpp"

namespace YSE {
	enum FILESTATE {
		LOADING,
		READY,
		INVALID,
	};

	class soundFile {
	public:
		Bool create(const std::string &fileName, Bool stream = false);
		Bool read(std::vector<DSP::sample> & filebuffer, Flt& pos, UInt length, Flt speed, Bool loop = true);
    Bool read(std::vector<DSP::sample> & filebuffer, Flt& pos, UInt length, Flt speed, Bool loop, SOUND_STATUS & intent, Int & latency, Flt & volume);

    // get
    Int       channels(); // number of channels in source
    UInt      length  (); // length of the source in frames
    FILESTATE state   (); // current state of the file, (ready, invalid or loading)
    Bool      active  (); // if usage > 0 or idletime < 500

    // set
    soundFile & reset  (); // indicate that stream needs to reset (after stop)
    soundFile & claim  (); // increase usage counter (used for removing sounds that are not needed anymore)
    soundFile & release(); // decrease usage counter
    soundFile();
	 ~soundFile();

	private:
		std::string _fileName;

#if defined(USE_PORTAUDIO)
		// libsndfile variables
		SNDFILE * _file     ;
		SF_INFO   _info     ;
		FILESTATE _state    ;
    Flt       _sr_adjust;
#elif defined(USE_OPENSL)

#endif

		DSP::sample _buffer; // contains the actual sound buffer

    // for housekeeping (removing unused sounds)
    Int _usage   ; // counts how many sounds are using this file
    Flt _idleTime; // counts how long a sound is inactive

    // for streaming
    Bool _streaming ;
    UInt _streamSize;
    Bool _endReached;
    Int  _streamPos ;
    Bool _needsReset;

    Bool fillStream (Bool loop);
    void resetStream(         );

    // for virtual IO
    fileData _customFileData;

    friend void LoadSoundFiles();
	};


}
