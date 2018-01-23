#pragma once
#include "basePage.h"
#include <forward_list>
#include <string>

class DemoVirtual :
  public basePage, YSE::logHandler
{
public:
	DemoVirtual();
	~DemoVirtual();
	
	virtual void ExplainDemo();
	virtual void ShowStatus();

  virtual void AddMessage(const std::string & message);

private:
	void AddSound();

	std::forward_list<YSE::sound> sounds;
	int counter = 0;
};

