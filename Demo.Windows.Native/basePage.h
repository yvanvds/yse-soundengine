#pragma once

#include <vector>
#include "menuAction.h"
#include "yse.hpp"
#include <iostream>

class basePage
{
public:
	basePage();
	void Run();
	void ShowMenu();

protected:
    void AddAction(char key, const std::string & text, func f);
	
	void SetTitle(const std::string & title) { this->title = title; }
	
	virtual void ExplainDemo() {}
	virtual void ShowStatus() {}

  char lastAction;

private:
	std::string title;
	std::vector<menuAction> Actions;
  
};

