#pragma once

// EventManager - the engine's Win32 window-message dispatcher (NeuronCore).
//
// Registered WNDPROC processors are tried in turn for each window message; the
// first one to handle it (returns != -1) wins, otherwise the chain falls through
// to DefWindowProc. This is the platform message pump's fan-out, nothing more.
//
// A generic in-process pub/sub (Subscribe/Publish over a base Event type) once
// lived here too; that role now belongs to Neuron::Msg::MessageBus - the project's
// single event/pub-sub mechanism (see NeuronCore/Messages/) - so only the Windows-
// message feature remains here.

class EventManager
{
  public:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    static void AddEventProcessor(WNDPROC _driver);
    static void RemoveEventProcessor(WNDPROC _driver);

  protected:
    inline static std::vector<WNDPROC> sm_eventprocs;
    inline static std::mutex sm_sync;
};
