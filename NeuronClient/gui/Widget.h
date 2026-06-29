#pragma once

#include <winrt/Windows.Foundation.h> // winrt::Windows::Foundation::Rect

class Widget
{
  public:
    Widget() = default;
    virtual ~Widget() = default;

    winrt::Windows::Foundation::Rect GetBounds() const { return m_bounds; }
    void SetBounds(const winrt::Windows::Foundation::Rect& _bounds) { m_bounds = _bounds; }

    bool IsActive() const { return m_active; }
    void SetActive(bool _active) { m_active = _active; }

  protected:
    bool m_active{};
    bool m_focused{};
    winrt::Windows::Foundation::Rect m_bounds{};
};
