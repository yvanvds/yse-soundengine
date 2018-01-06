// Demo.Windows.Native.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <iostream>
#include <cstdlib>
#include <conio.h>

#include "yse.hpp"

#include "MenuTop.h"

int main()
{
	YSE::System().init();

	TopMenu topMenu;
	topMenu.Run();

	YSE::System().close();
	return 0;
}

