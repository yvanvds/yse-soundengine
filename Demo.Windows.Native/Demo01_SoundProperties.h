#pragma once
#include "basePage.h"
#include "yse.hpp"

class DemoSoundProperties : public basePage
{
public:
	DemoSoundProperties();
	
	void IncSpeed();
	void DecSpeed();
	void IncVolume();
	void DecVolume();

private:
	YSE::sound sound;
};

