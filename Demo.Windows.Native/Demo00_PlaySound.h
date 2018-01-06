#pragma once
#include "basePage.h"
#include "yse.hpp"

class DemoPlaySound :
	public basePage
{
public:
	DemoPlaySound();
	
	void ToggleSound();
	void PlaySound();
	void StopSound();
	void PauseSound();

private:
	YSE::sound sound;
};

