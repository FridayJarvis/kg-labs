#pragma once
#include <windows.h>
#include <cstdint>
#include "D3D12Context.h"

class AppWindow;
class KeyboardMouse;

class Application
{
public:
    ~Application();

    bool Init(HINSTANCE hInst, int cmdShow);
    int  MainLoop();

    LRESULT ProcessMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
    void Tick(float deltaTime);
    void DrawFrame();

private:
    AppWindow*      m_wnd      = nullptr;
    KeyboardMouse*  m_inputMgr = nullptr;
    GraphicsEngine* m_gfx      = nullptr;

    bool m_shouldClose = false;

    uint64_t m_lastTimestamp = 0;
    double   m_tickToSeconds = 0.;

    float m_yawAngle   = 0.8f;
    float m_pitchAngle = 0.f;
    DirectX::XMFLOAT3 m_cameraPosition{ -4.f, 1.5f, -4.f };

    int  m_lastCursorX = 0;
    int  m_lastCursorY = 0;
    bool m_cursorInitialized = false;

    bool  m_rightMouseLook = false;
    POINT m_storedCursorPos{ 0,0 };
    bool  m_firstRmbFrame = false;
};
