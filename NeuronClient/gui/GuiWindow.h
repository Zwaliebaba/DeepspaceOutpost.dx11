#pragma once

#include <string>
#include <string_view>
#include <vector>

class GuiButton;

class GuiWindow
{
  public:
    GuiWindow(std::string_view _name);
    virtual ~GuiWindow();

    void SetName(std::string_view  _name) { m_name = _name; }
    void SetTitle(std::string_view  _title) { m_title = _title; }
    void SetPosition(int _x, int _y);
    void SetSize(int _w, int _h);
    void SetMovable(bool _movable);
    void MakeAllOnScreen();

    void RegisterButton(GuiButton* button);
    void RemoveButton(std::string_view  _name);

    void BeginTextEdit(std::string_view  _name);
    void EndTextEdit();

    virtual GuiButton* GetButton(std::string_view  _name);
    virtual GuiButton* GetButton(int _x, int _y);

    virtual void Create();
    virtual void Remove();
    virtual void Update();
    virtual void Render(bool hasFocus);

    virtual void Keypress(int keyCode, bool shift, bool ctrl, bool alt);
    virtual void MouseEvent(bool lmb, bool rmb, bool up, bool down);

    // NOTE: the donor's CreateValueControl()/RemoveValueControl() (numeric/string
    // value sliders) depended on InputField/InputScroller, which still rode the legacy
    // gl* path and a cursor/keydefs stack. They are deferred until InputField is
    // ported to ImmediateRenderer (see docs/phase2-4-gui-text.md).

    int GetClientRectX1();
    int GetClientRectY1();
    int GetClientRectX2();
    int GetClientRectY2();

    void SetCurrentButton(const GuiButton* button);

  public:
    float m_x;
    float m_y;
    float m_w;
    float m_h;

    std::string m_name;
    std::string m_title;

    bool m_movable {};
    bool m_resizable;

    std::vector<GuiButton*> m_buttons;
    std::vector<GuiButton*> m_buttonOrder;
    int m_currentButton;
    bool m_buttonChangedThisUpdate;

  public:
    std::string m_currentTextEdit;
};
