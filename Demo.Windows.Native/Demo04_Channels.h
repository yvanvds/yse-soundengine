#pragma once
#include "basePage.h"
class DemoChannels :
	public basePage
{
public:
	DemoChannels();
	~DemoChannels();
	
	virtual void ExplainDemo();

private:
	void MasterIncVol();
	void MasterDecVol();

	void CustomIncVol();
	void CustomDecVol();

	void MusicIncVol();
	void MusicDecVol();

	void CustomDelete();

	// normally you wouldn't use pointers for this, but this 
	// demonstrates what happens if you delete a channel object
	YSE::channel * customChannel;
	YSE::sound kick, pulse;
};

