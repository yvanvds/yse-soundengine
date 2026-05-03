#include "stdafx.h"
#include "MenuTop.h"

int main() {
    YSE::System().init();
    TopMenu menu;
    menu.Run();
    YSE::System().close();
    return 0;
}
