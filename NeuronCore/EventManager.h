#pragma once

#include <typeindex>
#include "Event.h"

class EventManager
{
  public:
    using ListenerId = std::size_t;

    template <typename EventT>
    static ListenerId Subscribe(std::function<void(const EventT&)> _listener)
    {
      std::scoped_lock lock(sm_mutex);

      const ListenerId id = sm_nextId++;
      const std::type_index typeIdx = typeid(EventT);
      auto wrapper = [fn = std::move(_listener)](const void* evt) { fn(*static_cast<const EventT*>(evt)); };
      sm_listeners[typeIdx].push_back(ListenerEntry{id, std::move(wrapper)});
      return id;
    }

    template <typename EventT>
    static void Unsubscribe(ListenerId _id)
    {
      std::scoped_lock lock(sm_mutex);
      if (_id == 0)
        return;

      const std::type_index typeIdx = typeid(EventT);
      const auto it = sm_listeners.find(typeIdx);
      if (it == sm_listeners.end())
        return;
      auto& entries = it->second;
      entries.erase(std::remove_if(entries.begin(), entries.end(), [_id](const ListenerEntry& _entry) { return _entry.m_id == _id; }),
                    entries.end());
      if (entries.empty())
        sm_listeners.erase(it);
    }

    template <typename EventT>
    static void Publish(const EventT& _event)
    {
      std::vector<ListenerEntry> entriesCopy;
      {
        std::scoped_lock lock(sm_mutex);
        const std::type_index typeIdx = typeid(EventT);
        const auto it = sm_listeners.find(typeIdx);
        if (it == sm_listeners.end())
          return;
        entriesCopy = it->second;
      }

      for (const auto& [m_id, m_callback] : entriesCopy)
        m_callback(&_event);
    }

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    static void AddEventProcessor(WNDPROC _driver);
    static void RemoveEventProcessor(WNDPROC _driver);

  protected:
    inline static std::vector<WNDPROC> sm_eventprocs;
    inline static std::mutex sm_sync;

  private:
    struct ListenerEntry
    {
      ListenerId m_id;
      std::function<void(const void*)> m_callback;
    };

    inline static std::mutex sm_mutex;
    inline static std::unordered_map<std::type_index, std::vector<ListenerEntry>> sm_listeners;
    inline static ListenerId sm_nextId{1};
};
