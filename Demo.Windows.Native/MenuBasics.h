#pragma once
#include "basePage.h"
class BasicsMenu :
	public basePage
{
public:
	BasicsMenu();
	
	void PlaySoundDemo();
	void SoundPropsDemo();
	void Demo3DDemo();
	void VirtualDemo();
  void ChannelDemo();
  void ReverbDemo();
  void OcclusionDemo();
  void StreamingDemo();
  void FilePosDemo();
};

