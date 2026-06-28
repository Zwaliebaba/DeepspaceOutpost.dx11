#include "pch.h"
#include "TasksCore.h"

using namespace Neuron::Tasks;

void Core::Startup() {}

void Core::Shutdown() {}

Thread::Thread(std::string _name)
  : m_name(std::move(_name)),
    m_requestStop(false),
    m_running(false) {}

Thread::~Thread()
{
  // kill the thread if running
  if (!m_running)
    Wait();
  else
  {
    m_running = false;

    TerminateThread(GetThreadHandle(), 0);
    CloseHandle(GetThreadHandle());
  }

  // Make sure start finished mutex is unlocked before it's destroyed
  if (m_startFinishedMutex.try_lock())
    m_startFinishedMutex.unlock();
}

bool Thread::Start()
{
  std::lock_guard lock(m_mutex);

  if (m_running)
    return false;

  m_requestStop = false;

  // The mutex may already be locked if the thread is being restarted
  // FIXME: what if this fails, or if already locked by same thread?
  std::unique_lock sfLock(m_startFinishedMutex, std::try_to_lock);

  try { m_threadObj = NEW std::thread(ThreadProc, this); }
  catch ([[maybe_unused]] const std::system_error& e) { return false; }

  while (!m_running) { Sleep(1); }

  // Allow spawned thread to continue
  sfLock.unlock();

  m_joinable = true;

  return true;
}

bool Thread::Stop()
{
  m_requestStop = true;
  return true;
}

bool Thread::Wait()
{
  std::lock_guard lock(m_mutex);

  if (!m_joinable)
    return false;

  m_threadObj->join();

  delete m_threadObj;
  m_threadObj = nullptr;

  assert(m_running == false);
  m_joinable = false;
  return true;
}

bool Thread::GetReturnValue(void** _ret) const
{
  if (m_running)
    return false;

  *_ret = m_retval;
  return true;
}

void Thread::ThreadProc(Thread* _thr)
{
  s_currentThread = _thr;

  SetName(_thr->m_name);

  _thr->m_running = true;

  // Wait for the thread that started this one to finish initializing the
  // thread handle so that GetThreadId/GetThreadHandle will work.
  std::unique_lock sfLock(_thr->m_startFinishedMutex);

  _thr->m_retval = _thr->Run();

  _thr->m_running = false;
  // Unlock m_startFinishedMutex to prevent data race condition on Windows.
  // On Windows with VS2017 build TerminateThread is called and this mutex is not
  // released. We try to unlock it from caller thread and it's refused by system.
  sfLock.unlock();
}
