#pragma once

namespace Neuron::Tasks
{
  using namespace Windows::Foundation;

  class Core
  {
    public:
      static void Startup();
      static void Shutdown();

      template <typename... ArgTypes>
      static Windows::System::Threading::ThreadPoolTimer RunPeriodic(std::function<void(ArgTypes...)> _function, TimeSpan _time,
                                                                     bool _periodic, ArgTypes... args)
      {
        if (_periodic)
        {
          Windows::System::Threading::TimerElapsedHandler timeElapsedHandler = [=]()-> void { _function(args...); };
          return Windows::System::Threading::ThreadPoolTimer::CreateTimer(timeElapsedHandler, _time);
        }
        Windows::System::Threading::TimerElapsedHandler timeElapsedHandler = [=]()-> void { _function(args...); };
        return Windows::System::Threading::ThreadPoolTimer::CreatePeriodicTimer(timeElapsedHandler, _time);
      }
  };

  class Thread : NonCopyable
  {
    public:
      Thread(std::string name = "");
      virtual ~Thread();

      /*
       * Begins execution of a new thread at the pure virtual method Thread::run().
       * Execution of the thread is guaranteed to have started after this function
       * returns.
       */
      bool start();

      /*
       * Requests that the thread exit gracefully.
       * Returns immediately; thread execution is guaranteed to be complete after
       * a subsequent call to Thread::wait.
       */
      bool stop();

      /*
       * Waits for thread to finish.
       * Note:  This does not stop a thread, you have to do this on your own.
       * Returns false immediately if the thread is not started or has been waited
       * on before.
       */
      bool wait();

      /*
       * Returns true if the calling thread is this Thread object.
       */
      bool isCurrentThread() const { return std::this_thread::get_id() == getThreadId(); }

      bool isRunning() const { return m_running; }
      bool stopRequested() const { return m_request_stop; }

      std::thread::id getThreadId() const { return m_thread_obj->get_id(); }

      /*
       * Gets the thread return value.
       * Returns true if the thread has exited and the return value was available,
       * or false if the thread has yet to finish.
       */
      bool getReturnValue(void** ret) const;

      /*
       * Binds (if possible, otherwise sets the affinity of) the thread to the
       * specific processor specified by proc_number.
       */
      bool bindToProcessor(DWORD_PTR proc_number) { return SetThreadAffinityMask(getThreadHandle(), 1ull << proc_number); }

      /*
       * Sets the thread priority to the specified priority.
       *
       * prio can be one of: THREAD_PRIORITY_LOWEST, THREAD_PRIORITY_BELOW_NORMAL,
       * THREAD_PRIORITY_NORMAL, THREAD_PRIORITY_ABOVE_NORMAL, THREAD_PRIORITY_HIGHEST.
       * On Windows, any of the other priorites as defined by SetThreadPriority
       * are supported as well.
       *
       * Note that it may be necessary to first set the threading policy or
       * scheduling algorithm to one that supports thread priorities if not
       * supported by default, otherwise this call will have no effect.
       */
      bool setPriority(int prio) { return SetThreadPriority(getThreadHandle(), prio); }

      /*
       * Returns the thread object of the current thread if it exists.
       */
      static Thread* getCurrentThread() { return g_currentThread; }

      /*
       * Sets the currently executing thread's name to where supported; useful
       * for debugging.
       */
      static void setName(const std::string& name) {}

      /*
       * Returns the number of processors/cores configured and active on this machine.
       */
      static unsigned int getNumberOfProcessors() { return std::thread::hardware_concurrency(); }

    protected:
      std::string m_name;

      virtual void* run() = 0;

    private:
      [[nodiscard]] HANDLE getThreadHandle() const { return m_thread_obj->native_handle(); }

      static void threadProc(Thread* thr);

      void* m_retval = nullptr;
      bool m_joinable = false;
      std::atomic<bool> m_request_stop;
      std::atomic<bool> m_running;
      std::mutex m_mutex;
      std::mutex m_start_finished_mutex;

      std::thread* m_thread_obj = nullptr;

      inline static thread_local Thread* g_currentThread = nullptr;
  };
}
