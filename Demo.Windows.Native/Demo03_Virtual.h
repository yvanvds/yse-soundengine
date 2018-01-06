#pragma once
#include "basePage.h"
#include <forward_list>

class DemoVirtual :
	public basePage
{
public:
	DemoVirtual();
	~DemoVirtual();
	
	virtual void ExplainDemo();
	virtual void ShowStatus();

	static void ShowMessage(const char * message);

private:
	void AddSound();

	std::forward_list<YSE::sound> sounds;
	int counter = 0;
};

