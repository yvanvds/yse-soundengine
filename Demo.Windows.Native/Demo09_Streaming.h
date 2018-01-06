#pragma once
#include "basePage.h"

/*
About streaming files
=====================
Advantages:
Streaming from disk is faster to load because only a fraction of the soundfile
is loaded into memory at any time. Because only part of the file is in memory,
it also uses less memory for the file.
Disadvantages:
If you intend to play the file at increased speed, the memory buffer will need
to be replaced much more often. This greatly increases disk usage.
If you loop a file, each part has to be loaded everytime it gets played.
Where non streaming files are loaded into memory only once and can be used by
several instances all playing at different positions or speeds, streaming files
need a buffer for every instance of the file.
Streaming sounds can not be played backwards either.

As a rule of thumb, you should use streaming files for background music or
dialogs. If you may have several instances of the sound playing at once, alter
the speed of the sound or if the sound will be used many times, just load it
into memory.

If in doubt, try loading your sound without streaming and look at the memory
increase of your application in the task manager. If it increases by more than
50 MB, you should at least consider streaming it.

(The memory footprint of a soundfile is more or less equal to it's uncompressed
form, at 44100Hz. Multichannel music can take a lot of bytes!)
*/

class DemoStreaming :
  public basePage
{
public:
  DemoStreaming();
  
  virtual void ExplainDemo();

private:
  void SpeedInc();
  void SpeedDec();
  void Pause();
  void Fade();
  void Play();
  
  YSE::sound sound;
};

