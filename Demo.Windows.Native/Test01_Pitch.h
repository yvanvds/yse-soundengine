#pragma once

#include "basePage.h"
#include "../YseEngine/yse.hpp"

class Test01_Pitch : public basePage 
{
public:
	Test01_Pitch();
	
	void toggleSound1();
	void toggleSound2();
	void toggleSound3();
	void toggleSound4();

private:
	YSE::sound sound1;
	YSE::sound sound2;
	YSE::sound sound3;
	YSE::sound sound4;
};

