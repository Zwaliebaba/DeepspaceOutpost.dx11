#pragma once

#include "Widget.h"

#include <string>

class GuiWindow;

class GuiButton : public Widget
{
  public:
    GuiButton();
    ~GuiButton() override = default;

    virtual void Render(int realX, int realY, bool highlighted, bool clicked);
    virtual void SetProperties(const std::string& _name, int _x, int _y, int _w = -1, int _h = -1, const std::string& _caption = {},
                               const std::string& _tooltip = {});
    void SetDisabled(bool _disabled = true);
    void UpdateButtonHighlight();

    virtual void SetCaption(const std::string& _caption) { m_caption = _caption; }
    virtual void SetTooltip(const std::string& _tooltip) { m_tooltip = _tooltip; }
    virtual void SetParent(GuiWindow* _parent) { m_parent = _parent; }

    virtual void MouseUp() {}
    virtual void MouseDown() {}
    virtual void MouseMove() {}
    virtual void Keypress(int keyCode, bool shift, bool ctrl, bool alt) {}

    std::string m_name;
    std::string m_caption;
    std::string m_tooltip;

    float m_fontSize;
    bool m_centered;
    bool m_disabled;
    bool m_highlightedThisFrame;
    bool m_mouseHighlightMode;

  protected:
    GuiWindow* m_parent{};
};

class BorderlessButton : public GuiButton
{
  public:
    BorderlessButton();
    void Render(int realX, int realY, bool highlighted, bool clicked) override;
};

class CloseButton : public GuiButton
{
  public:
    CloseButton();
    void Render(int realX, int realY, bool highlighted, bool clicked) override;
    void MouseUp() override;

    bool m_iconised;
};

class GameExitButton : public GuiButton
{
  void MouseUp() override;
};

class InvertedBox : public GuiButton
{
  public:
    void Render(int realX, int realY, bool highlighted, bool clicked) override;
};

class LabelButton : public GuiButton
{
  public:
    void Render(int realX, int realY, bool highlighted, bool clicked) override;
};
