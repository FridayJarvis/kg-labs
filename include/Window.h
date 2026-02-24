#pragma once
#include <windows.h>

class Application;

class AppWindow
{
public:
    bool Init(Application* owner, HINSTANCE hInst, int cmdShow, int w, int h, const wchar_t* caption);
    HWND Handle() const { return m_handle; }

private:
    static LRESULT CALLBACK WndProcRouter(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);

private:
    HWND m_handle = nullptr;
};