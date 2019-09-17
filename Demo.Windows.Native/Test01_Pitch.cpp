#include "stdafx.h"
#include "Test01_Pitch.h"


Test01_Pitch::Test01_Pitch()
{
	sound1.create("../TestResources/a440_44100_16b.wav", nullptr, true);
	sound2.create("../TestResources/a440_44100_8b.wav", nullptr, true);
	sound3.create("../TestResources/a440_22050_16b.wav", nullptr, true);
	sound4.create("../TestResources/a440_44100_16b.ogg", nullptr, true);
	
	SetTitle("All sounds Should have equal Pitch (440Hz)");
	AddAction('1', "Toggle 44100, 16 bit wav sample", std::bind(&Test01_Pitch::toggleSound1, this));
	AddAction('2', "Toggle 44100, 8 bit wav sample", std::bind(&Test01_Pitch::toggleSound2, this));
	AddAction('3', "Toggle 22050, 16 bit wav sample", std::bind(&Test01_Pitch::toggleSound3, this));
	AddAction('4', "Toggle 44100, 16 bit ogg sample", std::bind(&Test01_Pitch::toggleSound4, this));
}

void Test01_Pitch::toggleSound1()
{
	sound1.toggle();
}

void Test01_Pitch::toggleSound2()
{
	sound2.toggle();
}

void Test01_Pitch::toggleSound3()
{
	sound3.toggle();
}

void Test01_Pitch::toggleSound4()
{
	sound4.toggle();
}

