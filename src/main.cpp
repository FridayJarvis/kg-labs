#include "../include/App.h"
#include <windows.h>

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int cmdShow)
{
    Application application;

    if (!application.Init(hInst, cmdShow))
        return 0;

    return application.MainLoop();
}