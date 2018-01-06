#include "stdafx.h"
#include "basePage.h"

#include <stdlib.h>
#include <iostream>
#include <conio.h>


#include "yse.hpp"

basePage::basePage()
{
}


void basePage::ShowMenu() {
	if (title.size() > 0) {
		// if no txt is set, we assume an error message and do not clear the screen
		system("cls");
		std::cout << "===================================================================" << std::endl;
		std::cout << title << std::endl;
		std::cout << "===================================================================" << std::endl;
		std::cout << std::endl;
	}
	
	ExplainDemo();
  std::cout << std::endl;

	for (unsigned int i = 0; i < Actions.size(); i++) {
		std::cout << "  " << Actions[i].Key() << ": " << Actions[i].Text() << std::endl;
	}
	std::cout << std::endl;

	std::cout << "  0: exit this page." << std::endl;

	std::cout << std::endl;
}

void basePage::AddAction(char key, const std::string & text, func f) {
	Actions.emplace_back(key, text);
	Actions.back().Connect(f);
}

void basePage::Run() {
	ShowMenu();

	while (true) {
		if (_kbhit()) {
			char ch = _getch();
			
			if (ch == '0') return;
			for (unsigned int i = 0; i < Actions.size(); i++) {
				if (Actions[i].HasKey(ch)) {
          lastAction = ch;
					Actions[i].Execute();
				}
			}
		}

		/* the sleep function can be used to make sure the update function doesn't run at full
		speed. In a simple demo it does not make sense to update that much. In real software
		this should probably not be used. Just call YSE::System.update() in your main program loop.
		*/
		YSE::System().sleep(100);

		/* The update function activates all changes you made to sounds and channels since the last call.
		This function locks the audio processing thread for a short time. Calling it more than 50 times
		a second is really overkill, so call it once in your main program loop, not after changing every setting.
		*/
		YSE::System().update();

		ShowStatus();
	}
}