#include "pch.h"
#include "EventManager.h"

LRESULT EventManager::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  LRESULT ans = -1;

  for (auto i = sm_eventprocs.begin(); i != sm_eventprocs.end(); ++i)
  {
    ans = (*i)(hWnd, message, wParam, lParam);
    if (ans != -1)
      break;
  }

  return ans;
}

void EventManager::AddEventProcessor(WNDPROC _driver)
{
  std::scoped_lock lock(sm_sync);

  DEBUG_ASSERT(_driver != nullptr);
  sm_eventprocs.emplace_back(_driver);
}

void EventManager::RemoveEventProcessor(WNDPROC _driver)
{
  std::scoped_lock lock(sm_sync);

  for (auto i = sm_eventprocs.begin(); i != sm_eventprocs.end();)
  {
    if (_driver == *i)
      i = sm_eventprocs.erase(i);
    else
      ++i;
  }
}
