/*
  ==============================================================================

    enums.hpp
    Created: 27 Jan 2014 7:17:10pm
    Author:  yvan

  ==============================================================================
*/

#ifndef ENUMS_HPP_INCLUDED
#define ENUMS_HPP_INCLUDED

#include <string>

namespace YSE {

  // basic output configurations
  enum CHANNEL_TYPE {
    CT_AUTO, // will pick stereo when possible
    CT_MONO,
    CT_STEREO,
    CT_QUAD,
    CT_51,
    CT_51SIDE,
    CT_61,
    CT_71,
    CT_CUSTOM, // custom type, you need to set speaker positions yourself if you choose this
  };

  enum REVERB_PRESET {
    REVERB_OFF,
    REVERB_GENERIC,
    REVERB_PADDED,
    REVERB_ROOM,
    REVERB_BATHROOM,
    REVERB_STONEROOM,
    REVERB_LARGEROOM,
    REVERB_HALL,
    REVERB_CAVE,
    //REVERB_ARENA						,
    //REVERB_HANGAR						,
    //REVERB_CARPETTEDHALLWAY	,
    //REVERB_HALLWAY					,
    //REVERB_STONECORRICODOR	,	
    //REVERB_ALLEY						,
    //REVERB_FOREST						,
    //REVERB_CITY							,
    //REVERB_MOUNTAINS				,	
    //REVERB_QUARRY						,
    //REVERB_PLAIN						,
    //REVERB_PARKINGLOT				,
    REVERB_SEWERPIPE,
    REVERB_UNDERWATER,
  };

  enum SOUND_STATUS {
    SS_STOPPED,
    SS_PAUSED,
    SS_PLAYING,
    SS_PLAYING_FULL_VOLUME,
    SS_WANTSTOPLAY,
    SS_WANTSTOPAUSE,
    SS_WANTSTOSTOP,
    SS_WANTSTORESTART,
  };

  // for internal use by sound and soundimplementation
  enum SOUND_INTENT {
    SI_NONE,
    SI_PLAY,
    SI_STOP,
    SI_PAUSE,
    SI_TOGGLE,
    SI_RESTART,
  };

  // this should replace the specific sound & channel implementation states
  enum OBJECT_IMPLEMENTATION_STATE {
    OBJECT_CONSTRUCTED, // the object exists
    OBJECT_CREATED, // create function is done
    OBJECT_SETUP, // object setup is done
    OBJECT_READY, // ready for use
    OBJECT_DONE, // flag for release 
    OBJECT_RELEASE, // flagged for release from inUse list
    OBJECT_DELETE, // flagged for deletion from implementations list
  };

  enum SOUND_IMPLEMENTATION_STATE {
    SIS_CONSTRUCTED, // the object exists
    SIS_CREATED, // create function is done
    SIS_LOADED, // file is loaded
    SIS_READY, // ready for play
    SIS_DONE, // fade and flag for release if done
    SIS_RELEASE, // flagged for release from inUse list
    SIS_DELETE, // flagged for deletion from implementations list
  };


  enum CHANNEL_IMPLEMENTATION_STATE {
    CIS_CONSTRUCTED, // the object exists
    CIS_CREATED,     // create function is done
    CIS_SETUP,      // the setup function has run 
    CIS_READY,       // the object is ready for use
    CIS_DONE,        // sounds should be moved to parent channel
    CIS_RELEASE,     // flagged for release from inUse list
    CIS_DELETE,      // flagged for deletion from implementations list
  };

  enum OUT_TYPE {
    INVALID,
    BANG,
    FLOAT,
    INT,
    BUFFER,
    LIST,
    ANY,
  };

  enum THREAD {
    T_DSP,
    T_GUI,
  };

  // used by utils/error.hpp
  enum ERROR_LEVEL {
    EL_NONE,
    EL_ERROR,
    EL_WARNING,
    EL_DEBUG,
  };

  // used by utils/error.hpp
  enum ERROR_CODE {
    E_ERROR_MESSAGES, // possible errors:
    E_ERROR, // general error
    E_MUTEX_UNSTABLE, // the audio thread locking has become unstable
    E_AUDIODEVICE, // Audio device error: +  message  
    E_FILE_BYTE_COUNT, // Soundfile has wrong size
    E_FILEREADER, // Filereader error: + message
    E_TRACK_NOT_STARTED, // Unable to start a music track
    E_TRACK_TIMER_STOP, // Could not stop track timer
    E_APP_MESSAGE, // log message from the client app

    E_WARNING_MESSAGES, // possible warnings:
    E_WARNING, // general warning
    E_FILE_NOT_FOUND, // soundfile not found
    E_FILE_ERROR, // Error while loading file: + mesage
    E_SOUND_OBJECT_IN_USE, // Sound object already in use when create was called
    E_CHANNEL_OBJECT_IN_USE, // Channel object already in use when create was called
    E_SOUND_OBJECT_NO_INIT, // Sound object used without creating it first  
    E_REVERB_NO_INIT, // Reverb object used without creating it first

    E_DEBUG_MESSAGES, // possible debug messages:
    E_DEBUG, // general debug message
    E_SOUND_ADDED, // Sound added to system
    E_SOUND_DELETED, // Sound deleted from system
    E_SOUND_WRONG, // Objkect error with sound: + message
  };

  // use these when creating custom file callback functions
  enum FILEPOINT {
    FP_CURRENT,
    FP_START,
    FP_END,
  };

  // used by biquad filter
  enum BQ_TYPE {
    BQ_LOWPASS,
    BQ_HIGHPASS,
    BQ_BANDPASS,
    BQ_NOTCH,
    BQ_PEAK,
    BQ_LOWSHELF,
    BQ_HIGHSHELF,
  };

  namespace NOTE { // don't let these clutter up the interface
    enum MIDI_PITCH {
      CM1, // 0   -- C minus 1
      CSM1, // 1  -- S means sharp, F means flat
      DFM1 = 1,
      DM1, // 2
      DSM1, // 3
      EFM1 = 3,
      EM1, // 4
      FM1, // 5
      FSM1, // 6
      GFM1 = 6,
      GM1, // 7
      GSM1, // 8
      AFM1 = 8,
      AM1, // 9
      ASM1, // 10
      BFM1 = 10,
      BM1, // 11
      C0, // 12   -- C 0
      CS0, // 13
      DF0 = 13,
      D0, // 14
      DS0, // 15
      EF0 = 15,
      E0, // 16
      F0, // 17
      FS0, // 18
      GF0 = 18,
      G0, // 19 
      GS0, // 20
      AF0 = 20,
      A0, // 21
      AS0, // 22
      BF0 = 22,
      B0, // 23
      C1, // 24   -- C 1
      CS1, // 25
      DF1 = 25,
      D1, // 26
      DS1, // 27
      EF1 = 27,
      E1, // 28
      F1, // 29
      FS1, // 30
      GF1 = 30,
      G1, // 31 
      GS1, // 32
      AF1 = 32,
      A1, // 33
      AS1, // 34
      BF1 = 34,
      B1, // 35       
      C2, // 36   -- C 2
      CS2, // 37
      DF2 = 37,
      D2, // 38
      DS2, // 39
      EF2 = 39,
      E2, // 40
      F2, // 41
      FS2, // 42
      GF2 = 42,
      G2, // 43 
      GS2, // 44
      AF2 = 44,
      A2, // 45
      AS2, // 46
      BF2 = 46,
      B2, // 47   
      C3, // 48   -- C 3
      CS3, // 49
      DF3 = 49,
      D3, // 50
      DS3, // 51
      EF3 = 51,
      E3, // 52
      F3, // 53
      FS3, // 54
      GF3 = 54,
      G3, // 55 
      GS3, // 56
      AF3 = 56,
      A3, // 57
      AS3, // 58
      BF3 = 58,
      B3, // 59   
      C4, // 60   -- C 4
      CS4, // 61
      DF4 = 61,
      D4, // 62
      DS4, // 63
      EF4 = 63,
      E4, // 64
      F4, // 65
      FS4, // 66
      GF4 = 66,
      G4, // 67 
      GS4, // 68
      AF4 = 68,
      A4, // 69
      AS4, // 70
      BF4 = 70,
      B4, // 71  
      C5, // 72   -- C 5
      CS5, // 73
      DF5 = 73,
      D5, // 74
      DS5, // 75
      EF5 = 75,
      E5, // 76
      F5, // 77
      FS5, // 78
      GF5 = 78,
      G5, // 79 
      GS5, // 80
      AF5 = 80,
      A5, // 81
      AS5, // 82
      BF5 = 82,
      B5, // 83 
      C6, // 84   -- C 6
      CS6, // 85
      DF6 = 85,
      D6, // 86
      DS6, // 87
      EF6 = 87,
      E6, // 88
      F6, // 89
      FS6, // 90
      GF6 = 90,
      G6, // 91 
      GS6, // 92
      AF6 = 92,
      A6, // 93
      AS6, // 94
      BF6 = 94,
      B6, // 95 
      C7, // 96   -- C 7
      CS7, // 97
      DF7 = 97,
      D7, // 98
      DS7, // 99
      EF7 = 99,
      E7, // 100
      F7, // 101
      FS7, // 102
      GF7 = 102,
      G7, // 103 
      GS7, // 104
      AF7 = 104,
      A7, // 105
      AS7, // 106
      BF7 = 106,
      B7, // 107
      C8, // 108   -- C 8
      CS8, // 109
      DF8 = 109,
      D8, // 110
      DS8, // 111
      EF8 = 111,
      E8, // 112
      F8, // 113
      FS8, // 114
      GF8 = 114,
      G8, // 115 
      GS8, // 116
      AF8 = 116,
      A8, // 117
      AS8, // 118
      BF8 = 118,
      B8, // 119
      C9, // 120   -- C 9
      CS9, // 121
      DF9 = 121,
      D9, // 122
      DS9, // 123
      EF9 = 123,
      E9, // 124
      F9, // 125
      FS9, // 126
      GF9 = 126,
      G9, // 127 
    };
  }
}





#endif  // ENUMS_HPP_INCLUDED
