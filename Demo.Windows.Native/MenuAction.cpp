#include "stdafx.h"
#include "menuAction.h"


menuAction::menuAction(char key, const std::string & menuText)
{
	this->key = key;
	this->menuText = menuText;
}

void menuAction::Connect(func f) {
	this->f = f;
}

void menuAction::Execute() {
	f();
}

bool menuAction::HasKey(char key) {
	return this->key == key;
}

const std::string & menuAction::Text() {
	return menuText;
}