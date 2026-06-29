#include "pch.h"
#include "Canvas.h"
#include "GraphicsCore.h"
#include "GuiButton.h"
#include "GuiWindow.h"

#include <cstdlib> // rand / RAND_MAX (donor used darwiniaRandom for cosmetic window jitter)

static std::vector<GuiWindow*> s_windows;

static void (*clearDraw)(int, int, int, int) = nullptr;
static void (*tooltipCallback)(GuiWindow*, GuiButton*) = nullptr;

static int buttonDownMouseX = 0;
static int buttonDownMouseY = 0;

static int mouseX = 0;
static int mouseY = 0;
static bool lmb = false;
static bool rmb = false;

static int mouseDownWindowX = 0;
static int mouseDownWindowY = 0;

static int tooltipTimer = 0;

static int maximiseOldX = 0;
static int maximiseOldY = 0;
static int maximiseOldW = 0;
static int maximiseOldH = 0;

static std::string mouseDownWindow = "None"; // Which window have I just clicked in
static std::string windowFocus = "None"; // Which window is at the front
static std::string popupWindow = "None"; // Current popup window
static std::string maximisedWindow = "None"; // Which window is maximised
static std::string currentButton = "None"; // Current highlighted button

void Canvas::Startup() {}

void Canvas::EclUpdateMouse(int _mouseX, int _mouseY, bool _lmb, bool _rmb)
{
  int oldMouseX = mouseX;
  int oldMouseY = mouseY;

  mouseX = _mouseX;
  mouseY = _mouseY;

  if (!_lmb && !lmb && !_rmb && !rmb) // No buttons changed, mouse move only
  {
    GuiWindow* currentWindow = EclGetWindow(windowFocus.c_str());
    if (currentWindow)
    {
      GuiButton* button = currentWindow->GetButton(mouseX - currentWindow->m_x, mouseY - currentWindow->m_y);
      if (button)
      {
        if (strcmp(currentButton.c_str(), button->m_name.c_str()) != 0)
        {
          currentButton = button->m_name;
          tooltipTimer = Timer::Core::GetTotalSeconds() + 1.0f;
        }
        else
        {
          if (Timer::Core::GetTotalSeconds() > tooltipTimer)
          {
            if (tooltipCallback)
              tooltipCallback(currentWindow, button);
          }
        }
      }
      else
      {
        if (strcmp(currentButton.c_str(), "None") != 0)
        {
          currentButton = "None";
          if (tooltipCallback)
            tooltipCallback(nullptr, nullptr);
        }
      }
    }
    else
    {
      if (strcmp(currentButton.c_str(), "None") != 0)
      {
        currentButton = "None";
        if (tooltipCallback)
          tooltipCallback(nullptr, nullptr);
      }
    }
  }
  else if (_lmb && !lmb) // Left button down
  {
    buttonDownMouseX = mouseX;
    buttonDownMouseY = mouseY;

    GuiWindow* currentWindow = EclGetWindow(mouseX, mouseY);
    if (currentWindow)
    {
      windowFocus = currentWindow->m_name;
      EclBringWindowToFront(currentWindow->m_name.c_str());
      mouseDownWindow = currentWindow->m_name;

      GuiButton* button = currentWindow->GetButton(mouseX - currentWindow->m_x, mouseY - currentWindow->m_y);
      if (button)
      {
        currentButton = button->m_name;
        button->MouseDown();
      }
      else
      {
        currentButton = "None";
        currentWindow->MouseEvent(true, false, false, true);
        if (currentWindow->m_movable)
        {
          mouseDownWindowX = mouseX - currentWindow->m_x;
          mouseDownWindowY = mouseY - currentWindow->m_y;
        }
      }
    }
    else
    {
      if (strcmp(windowFocus.c_str(), "None") != 0)
        windowFocus = "None";
    }

    lmb = true;
  }
  else if (_lmb && lmb) // Left button dragged
  {
    buttonDownMouseX = mouseX;
    buttonDownMouseY = mouseY;

    if (strcmp(mouseDownWindow.c_str(), "None") != 0)
    {
      GuiWindow* window = EclGetWindow(mouseDownWindow.c_str());
      GuiButton* button = window->GetButton(mouseX - window->m_x, mouseY - window->m_y);

      if (button)
        button->MouseDown();
      else
      {
        if (strcmp(currentButton.c_str(), "None") == 0)
        {
          int newWidth = window->m_w;
          int newHeight = window->m_h;
          bool sizeChanged = false;

          if (oldMouseY > window->m_y + window->m_h - 4 && oldMouseY < window->m_y + window->m_h + 4)
          {
            newHeight = mouseY - window->m_y;
            sizeChanged = true;
          }

          if (oldMouseX > window->m_x + window->m_w - 4 && oldMouseX < window->m_x + window->m_w + 4)
          {
            newWidth = mouseX - window->m_x;
            sizeChanged = true;
          }

          if (sizeChanged)
          {
            if (window->m_resizable)
            {
              if (newWidth < 60)
                newWidth = 60;
              if (newHeight < 40)
                newHeight = 40;
              EclSetWindowSize(mouseDownWindow.c_str(), newWidth, newHeight);
            }
          }
          else
          {
            if (window->m_movable)
              EclSetWindowPosition(mouseDownWindow.c_str(), mouseX - mouseDownWindowX, mouseY - mouseDownWindowY);
          }
        }
      }
    }
  }
  else if (!_lmb && lmb) // Left button up
  {
    mouseDownWindow = "None";
    lmb = false;

    GuiWindow* currentWindow = EclGetWindow(buttonDownMouseX, buttonDownMouseY);
    if (currentWindow)
    {
      GuiButton* button = currentWindow->GetButton(buttonDownMouseX - currentWindow->m_x, buttonDownMouseY - currentWindow->m_y);
      if (button)
        button->MouseUp();
      else
      {
        currentWindow->MouseEvent(true, false, true, false);
        EclRemovePopup();
      }
    }
    else
      EclRemovePopup();
  }

  if (_rmb && !rmb)
  {
    // Right button down
  }
  else if (_rmb && rmb)
  {
    // Right button dragged
  }
  else if (!_rmb && rmb)
  {
    // Right button up
  }
}

void Canvas::EclUpdateKeyboard(int keyCode, bool shift, bool ctrl, bool alt)
{
  GuiWindow* currentWindow = EclGetWindow(windowFocus.c_str());
  if (currentWindow)
    currentWindow->Keypress(keyCode, shift, ctrl, alt);
}

void Canvas::EclRegisterTooltipCallback(void (*_callback)(GuiWindow*, GuiButton*)) { tooltipCallback = _callback; }

void Canvas::Render()
{
  //
  // Render any maximised Window?

  if (strcmp(maximisedWindow.c_str(), "None") != 0)
  {
    GuiWindow* maximised = EclGetWindow(maximisedWindow.c_str());
    if (maximised)
    {
      if (clearDraw)
        clearDraw(maximised->m_x, maximised->m_y, maximised->m_w, maximised->m_h);
      maximised->Render(true);
    }
    else
      EclUnMaximize();
  }

  for (int i = static_cast<int>(s_windows.size()) - 1; i >= 0; --i)
  {
    auto window = s_windows[i];
    window->Render(window->m_name == windowFocus);
  }
}

void Canvas::EclUpdate()
{
  for (int i = 0; i < static_cast<int>(s_windows.size()); ++i)
  {
    GuiWindow* window = s_windows[i];
    window->Update();
  }
}

void Canvas::Shutdown() { { for (auto p : s_windows) delete p; s_windows.clear(); } }

std::string Canvas::EclGetCurrentButton() { return currentButton; }

std::string Canvas::EclGetCurrentClickedButton()
{
  if (lmb)
    return currentButton;
  return "None";
}

void Canvas::EclRegisterWindow(GuiWindow* window, const GuiWindow* parent)
{
  if (EclGetWindow(window->m_name.c_str()))
    return;

  if (parent && window->m_x == 0 && window->m_y == 0)
  {
    // We should place the window in a decent location
    int left = Neuron::Graphics::Core::GetOutputSize().Width / 2 - parent->m_x;
    int above = Neuron::Graphics::Core::GetOutputSize().Height / 2 - parent->m_y;
    if (left > window->m_w / 2)
      window->m_x = static_cast<int>(parent->m_x + parent->m_w * static_cast<float>(rand()) / static_cast<float>(RAND_MAX));
    else
      window->m_x = static_cast<int>(parent->m_x - window->m_w * static_cast<float>(rand()) / static_cast<float>(RAND_MAX));
    if (above > window->m_h / 2)
      window->m_y = static_cast<int>(parent->m_y + parent->m_h * static_cast<float>(rand()) / static_cast<float>(RAND_MAX));
    else
      window->m_y = static_cast<int>(parent->m_y - window->m_h / 2 * static_cast<float>(rand()) / static_cast<float>(RAND_MAX));
  }

  window->MakeAllOnScreen();
  s_windows.insert(s_windows.begin(), window);
  window->Create();
}

void Canvas::EclRegisterPopup(GuiWindow* window)
{
  EclRemovePopup();
  popupWindow = window->m_name;
  EclRegisterWindow(window);
}

void Canvas::EclRemovePopup()
{
  if (EclGetWindow(popupWindow.c_str()))
    EclRemoveWindow(popupWindow.c_str());
  popupWindow = "None";
}

void Canvas::EclRemoveWindow(std::string_view name)
{
  int index = EclGetWindowIndex(name);
  if (index != -1)
  {
    GuiWindow* window = s_windows[index];
    s_windows.erase(s_windows.begin() + (index));
    delete window;

    if (mouseDownWindow == name)
      mouseDownWindow = "None";

    if (windowFocus == name)
      windowFocus = "None";
  }
}

void Canvas::EclSetWindowPosition(std::string_view name, int x, int y)
{
  GuiWindow* window = EclGetWindow(name);
  if (window)
  {
    window->m_x = x;
    window->m_y = y;
  }
}

void Canvas::EclSetWindowSize(std::string_view name, int w, int h)
{
  GuiWindow* window = EclGetWindow(name);
  if (window)
  {
    window->Remove();
    window->m_w = w;
    window->m_h = h;
    window->Create();
  }
}

void Canvas::EclBringWindowToFront(std::string_view name)
{
  int index = EclGetWindowIndex(name);
  if (index != -1)
  {
    GuiWindow* window = s_windows[index];
    s_windows.erase(s_windows.begin() + (index));
    s_windows.insert(s_windows.begin(), window);
  }
}

bool Canvas::EclMouseInWindow(const GuiWindow* window) { return (EclGetWindow(mouseX, mouseY) == window); }

bool Canvas::EclMouseInButton(const GuiWindow* window, const GuiButton* button)
{
  auto bounds = button->GetBounds();
  return (EclMouseInWindow(window) && mouseX >= window->m_x + bounds.X && mouseX <= window->m_x + bounds.X + bounds.Width && mouseY >=
    window->m_y + bounds.Y && mouseY <= window->m_y + bounds.Y + bounds.Height);
}

bool Canvas::EclIsTextEditing()
{
  GuiWindow* currentWindow = EclGetWindow(windowFocus);
  return (currentWindow && strcmp(currentWindow->m_currentTextEdit.c_str(), "None") != 0);
}

int Canvas::EclGetWindowIndex(std::string_view name)
{
  for (int i = 0; i < static_cast<int>(s_windows.size()); ++i)
  {
    GuiWindow* window = s_windows[i];
    if (window->m_name == name)
      return i;
  }
  return -1;
}

GuiWindow* Canvas::EclGetWindow(std::string_view name)
{
  int index = EclGetWindowIndex(name);
  if (index == -1)
    return nullptr;
  return s_windows[index];
}

GuiWindow* Canvas::EclGetWindow(int x, int y)
{
  for (int i = 0; i < static_cast<int>(s_windows.size()); ++i)
  {
    GuiWindow* window = s_windows[i];
    if (x >= window->m_x && x <= window->m_x + window->m_w && y >= window->m_y && y <= window->m_y + window->m_h)
      return window;
  }

  return nullptr;
}

void Canvas::EclMaximizeWindow(std::string_view name)
{
  EclUnMaximize();
  GuiWindow* w = EclGetWindow(name);
  if (w)
  {
    maximisedWindow = name;
    mouseDownWindow = name;
    windowFocus = name;
    maximiseOldX = w->m_x;
    maximiseOldY = w->m_y;
    maximiseOldW = w->m_w;
    maximiseOldH = w->m_h;
    w->SetPosition(0, 0);
    w->SetSize(Neuron::Graphics::Core::GetOutputSize().Width, Neuron::Graphics::Core::GetOutputSize().Height);
  }
}

void Canvas::EclUnMaximize()
{
  GuiWindow* w = EclGetWindow(maximisedWindow.c_str());
  maximisedWindow = "None";

  if (w)
  {
    w->SetPosition(maximiseOldX, maximiseOldY);
    w->SetSize(maximiseOldW, maximiseOldH);
  }
}

std::vector<GuiWindow*>* Canvas::EclGetWindows() { return &s_windows; }

std::string Canvas::EclGetCurrentFocus() { return windowFocus; }

void Canvas::EclSetCurrentFocus(std::string_view name) { windowFocus = name; }
