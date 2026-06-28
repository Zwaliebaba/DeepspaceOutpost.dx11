#pragma once

namespace Neuron::Tasks
{
  class Core
  {
    public:
      static void Startup();
      static void Shutdown();
  };

  class Thread : NonCopyable
  {
    public:
      Thread(std::string _name = "");
      virtual ~Thread();

      /*
       * Begins execution of a new thread at the pure virtual method Thread::Run().
       * Execution of the thread is guaranteed to have started after this function
       * returns.
       */
      bool Start();

      /*
       * Requests that the thread exit gracefully.
       * Returns immediately; thread execution is guaranteed to be complete after
       * a subsequent call to Thread::Wait.
       */
      bool Stop();

      /*
       * Waits for thread to finish.
       * Note:  This does not stop a thread, you have to do this on your own.
       * Returns false immediately if the thread is not started or has been waited
       * on before.
       */
      bool Wait();

      /*
       * Returns true if the calling thread is this Thread object.
       */
      bool IsCurrentThread() const { return std::this_thread::get_id() == GetThreadId(); }

      bool IsRunning() const { return m_running; }
      bool StopRequested() const { return m_requestStop; }

      std::thread::id GetThreadId() const { return m_threadObj->get_id(); }

      /*
       * Gets the thread return value.
       * Returns true if the thread has exited and the return value was available,
       * or false if the thread has yet to finish.
       */
      bool GetReturnValue(void** _ret) const;

      /*
       * Binds (if possible, otherwise sets the affinity of) the thread to the
       * specific processor specified by _procNumber.
       */
      bool BindToProcessor(DWORD_PTR _procNumber) { return SetThreadAffinityMask(GetThreadHandle(), 1ull << _procNumber); }

      /*
       * Sets the thread priority to the specified priority.
       *
       * _prio can be one of: THREAD_PRIORITY_LOWEST, THREAD_PRIORITY_BELOW_NORMAL,
       * THREAD_PRIORITY_NORMAL, THREAD_PRIORITY_ABOVE_NORMAL, THREAD_PRIORITY_HIGHEST.
       * On Windows, any of the other priorites as defined by SetThreadPriority
       * are supported as well.
       *
       * Note that it may be necessary to first set the threading policy or
       * scheduling algorithm to one that supports thread priorities if not
       * supported by default, otherwise this call will have no effect.
       */
      bool SetPriority(int _prio) { return SetThreadPriority(GetThreadHandle(), _prio); }

      /*
       * Returns the thread object of the current thread if it exists.
       */
      static Thread* GetCurrentThread() { return s_currentThread; }

      /*
       * Sets the currently executing thread's name to where supported; useful
       * for debugging.
       */
      static void SetName(const std::string& _name) {}

      /*
       * Returns the number of processors/cores configured and active on this machine.
       */
      static unsigned int GetNumberOfProcessors() { return std::thread::hardware_concurrency(); }

    protected:
      std::string m_name;

      virtual void* Run() = 0;

    private:
      [[nodiscard]] HANDLE GetThreadHandle() const { return m_threadObj->native_handle(); }

      static void ThreadProc(Thread* _thr);

      void* m_retval = nullptr;
      bool m_joinable = false;
      std::atomic<bool> m_requestStop;
      std::atomic<bool> m_running;
      std::mutex m_mutex;
      std::mutex m_startFinishedMutex;

      std::thread* m_threadObj = nullptr;

      inline static thread_local Thread* s_currentThread = nullptr;
  };
}
