#pragma once

#include <string>
#include <functional>

typedef std::function<void()> func;

class menuAction
{
public:
	menuAction(char key, const std::string & menuText);
	
	void Connect(func f);
	void Execute();
	bool HasKey(char key);
	const std::string & Text();
	const char Key() { return key;  }

private:
	char key;
	std::string menuText;

	func f;
};

