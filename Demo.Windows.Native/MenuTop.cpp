#include "stdafx.h"
#include "MenuTop.h"
#include "MenuBasics.h"
#include "MenuOther.h"
#include "MenuDsp.h"

TopMenu::TopMenu()
{
	SetTitle("YSE Console Demo");

	AddAction('1', "Basic Audio Examples", std::bind(&TopMenu::BasicAudioExamples, this));
	AddAction('2', "Dsp Examples", std::bind(&TopMenu::DspExamples, this));
	AddAction('3', "Composition Examples", std::bind(&TopMenu::ComposerExamples, this));
	AddAction('4', "File Manipulation Examples", std::bind(&TopMenu::FileExamples, this));
	AddAction('5', "Other Examples", std::bind(&TopMenu::OtherExamples, this));
}

void TopMenu::BasicAudioExamples()
{
	BasicsMenu menu;
	menu.Run();
	ShowMenu();
}

void TopMenu::DspExamples()
{
  MenuDsp menu;
  menu.Run();
  ShowMenu();
}

void TopMenu::ComposerExamples()
{
}

void TopMenu::FileExamples()
{
}

void TopMenu::OtherExamples()
{
  OtherMenu menu;
  menu.Run();
  ShowMenu();
}



