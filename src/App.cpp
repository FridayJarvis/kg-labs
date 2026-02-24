#include "../include/App.h"
#include "../include/Window.h"
#include "../include/KeyboardMouse.h"
#include <windows.h>
#include <windowsx.h>
#include <algorithm>
#include <DirectXMath.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "d3dcompiler.lib")

static uint64_t ReadTimer()
{
    LARGE_INTEGER t{};
    QueryPerformanceCounter(&t);
    return static_cast<uint64_t>(t.QuadPart);
}

static double TimerFrequency()
{
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    return static_cast<double>(f.QuadPart);
}

Application::~Application()
{
    delete m_gfx;
    delete m_inputMgr;
    delete m_wnd;
}

bool Application::Init(HINSTANCE hInst, int cmdShow)
{
    m_wnd      = new AppWindow();
    m_inputMgr = new KeyboardMouse();
    m_inputMgr->Clear();

    if (!m_wnd->Init(this, hInst, cmdShow, 1024, 768, L"D3D12 Cube"))
        return false;

    m_tickToSeconds = 1.0 / TimerFrequency();
    m_lastTimestamp = ReadTimer();

    m_gfx = new GraphicsEngine();

    RECT rc{};
    GetClientRect(m_wnd->Handle(), &rc);
    uint32_t w = (uint32_t)(rc.right - rc.left);
    uint32_t h = (uint32_t)(rc.bottom - rc.top);
    if (!m_gfx->Setup(m_wnd->Handle(), w, h)) return false;

    return true;
}

void Application::DrawFrame() { if (m_gfx) m_gfx->RenderFrame(); }

int Application::MainLoop()
{
    MSG msg{};
    while (!m_shouldClose)
    {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                m_shouldClose = true;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        const uint64_t now = ReadTimer();
        const double dt = (now - m_lastTimestamp) * m_tickToSeconds;
        m_lastTimestamp = now;

        Tick(static_cast<float>(dt));
        DrawFrame();
    }

    return 0;
}

void Application::Tick(float deltaTime)
{
    using namespace DirectX;

    if (m_inputMgr && m_inputMgr->IsPressed(VK_ESCAPE))
        m_shouldClose = true;

    if (!m_inputMgr || !m_gfx) return;

    const bool rmb = m_inputMgr->IsPressed(VK_RBUTTON);
    if (rmb)
    {
        HWND hWnd = m_wnd ? m_wnd->Handle() : nullptr;
        if (!hWnd) return;

        RECT rc{};
        GetClientRect(hWnd, &rc);

        POINT centerClient{ (rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2 };
        POINT centerScreen = centerClient;
        ClientToScreen(hWnd, &centerScreen);

        if (m_firstRmbFrame)
        {
            SetCursorPos(centerScreen.x, centerScreen.y);
            m_firstRmbFrame = false;
            return;
        }

        POINT curScreen{};
        GetCursorPos(&curScreen);

        const int dx = curScreen.x - centerScreen.x;
        const int dy = curScreen.y - centerScreen.y;

        const float sensitivity = 0.004f;
        m_yawAngle   += dx * sensitivity;
        m_pitchAngle -= dy * sensitivity;

        const float limit = DirectX::XM_PIDIV2 - 0.05f;
        m_pitchAngle = std::clamp(m_pitchAngle, -limit, limit);

        SetCursorPos(centerScreen.x, centerScreen.y);
    }

    float moveSpeed = 4.5f;
    if (m_inputMgr->IsPressed(VK_SHIFT)) moveSpeed = 10.0f;

    XMVECTOR fwd   = XMVector3Normalize(XMVectorSet(sinf(m_yawAngle), 0.0f, cosf(m_yawAngle), 0.0f));
    XMVECTOR right = XMVector3Normalize(XMVector3Cross(XMVectorSet(0, 1, 0, 0), fwd));
    XMVECTOR up    = XMVectorSet(0, 1, 0, 0);

    XMVECTOR movement = XMVectorZero();
    if (m_inputMgr->IsPressed('W')) movement = XMVectorAdd(movement, fwd);
    if (m_inputMgr->IsPressed('S')) movement = XMVectorSubtract(movement, fwd);
    if (m_inputMgr->IsPressed('D')) movement = XMVectorAdd(movement, right);
    if (m_inputMgr->IsPressed('A')) movement = XMVectorSubtract(movement, right);
    if (m_inputMgr->IsPressed('E')) movement = XMVectorAdd(movement, up);
    if (m_inputMgr->IsPressed('Q')) movement = XMVectorSubtract(movement, up);

    if (!XMVector3Equal(movement, XMVectorZero()))
    {
        movement = XMVector3Normalize(movement);
        const float dist = moveSpeed * deltaTime;
        XMVECTOR pos = XMVectorSet(m_cameraPosition.x, m_cameraPosition.y, m_cameraPosition.z, 1.0f);
        pos = XMVectorAdd(pos, XMVectorScale(movement, dist));
        XMStoreFloat3(&m_cameraPosition, pos);
    }

    m_gfx->UpdateCamera(m_cameraPosition, m_yawAngle, m_pitchAngle);
}

LRESULT Application::ProcessMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CLOSE:
    case WM_DESTROY:
        m_shouldClose = true;
        PostQuitMessage(0);
        return 0;

    case WM_KEYDOWN:
        if (m_inputMgr) m_inputMgr->KeyPressed(static_cast<uint32_t>(wParam));
        return 0;

    case WM_KEYUP:
        if (m_inputMgr) m_inputMgr->KeyReleased(static_cast<uint32_t>(wParam));
        return 0;

    case WM_RBUTTONDOWN:
    {
        if (m_inputMgr) m_inputMgr->KeyPressed(VK_RBUTTON);

        m_rightMouseLook = true;
        m_firstRmbFrame  = true;

        GetCursorPos(&m_storedCursorPos);
        ShowCursor(FALSE);
        SetCapture(hWnd);
        return 0;
    }

    case WM_RBUTTONUP:
    {
        if (m_inputMgr) m_inputMgr->KeyReleased(VK_RBUTTON);

        m_rightMouseLook = false;

        SetCursorPos(m_storedCursorPos.x, m_storedCursorPos.y);
        ShowCursor(TRUE);
        ReleaseCapture();
        return 0;
    }

    case WM_MOUSEMOVE:
        if (m_inputMgr) m_inputMgr->CursorMoved(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;

    case WM_SIZE:
    {
        uint32_t w = (uint32_t)LOWORD(lParam);
        uint32_t h = (uint32_t)HIWORD(lParam);

        if (w == 0 || h == 0) return 0;

        if (m_gfx) m_gfx->HandleResize(w, h);
        return 0;
    }

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
}