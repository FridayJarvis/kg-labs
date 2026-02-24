#pragma once
#include <cstdint>

class KeyboardMouse
{
public:
    void Clear();

    void KeyPressed(uint32_t vk);
    void KeyReleased(uint32_t vk);
    void CursorMoved(int x, int y);
    bool IsPressed(uint32_t vk) const;

    int CursorX() const { return m_cursorX; }
    int CursorY() const { return m_cursorY; }

private:
    bool m_keyStates[256]{};
    int  m_cursorX = 0;
    int  m_cursorY = 0;
};