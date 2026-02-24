#include "../include/KeyboardMouse.h"
#include <cstring>

void KeyboardMouse::Clear() {
    std::memset(m_keyStates, 0, sizeof(m_keyStates));
    m_cursorX = 0;
    m_cursorY = 0;
}

void KeyboardMouse::KeyPressed(uint32_t vk)  { if (vk < 256) m_keyStates[vk] = true; }

void KeyboardMouse::KeyReleased(uint32_t vk) { if (vk < 256) m_keyStates[vk] = false; }

void KeyboardMouse::CursorMoved(int x, int y) {
    m_cursorX = x;
    m_cursorY = y;
}

bool KeyboardMouse::IsPressed(uint32_t vk) const { return (vk < 256) ? m_keyStates[vk] : false; }