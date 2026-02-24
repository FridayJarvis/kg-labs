#include "../include/Window.h"
#include "../include/App.h"
#include <windows.h>

bool AppWindow::Init(Application* owner, HINSTANCE hInst, int cmdShow, int w, int h, const wchar_t* caption)
{
    const wchar_t* clsName = L"D3D12AppWindow";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &AppWindow::WndProcRouter;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = clsName;

    if (!RegisterClassExW(&wc)) return false;

    RECT rc{ 0, 0, w, h };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    m_handle = CreateWindowExW(
        0, clsName, caption, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, hInst, owner
    );

    if (!m_handle) return false;

    ShowWindow(m_handle, cmdShow);
    UpdateWindow(m_handle);
    return true;
}

LRESULT CALLBACK AppWindow::WndProcRouter(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_CREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        auto* owner = reinterpret_cast<Application*>(cs->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(owner));
    }

    auto* owner = reinterpret_cast<Application*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (owner)
        return owner->ProcessMessage(hWnd, msg, wp, lp);

    return DefWindowProcW(hWnd, msg, wp, lp);
}