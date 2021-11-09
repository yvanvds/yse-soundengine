// Demo.Windows.Native.cpp : Defines the entry point for the console application.
//


#include "stdafx.h"
#ifdef WIN32
#include <conio.h>
#endif

#include <iostream>
#include <cstdlib>
#include "../YseEngine/yse.hpp"
#include "MenuTop.h"

int main()
{
	YSE::System().init();

	TopMenu topMenu;
 	topMenu.Run();

	YSE::System().close();
	return 0;
}

