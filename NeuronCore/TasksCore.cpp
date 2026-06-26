#include "pch.h"
#include "TasksCore.h"

using namespace Neuron::Tasks;

void Core::Startup() {}

void Core::Shutdown() {}

Thread::Thread(std::string name)
  : m_name(std::move(name)),
    m_request_stop(false),
    m_running(false) {}

Thread::~Thread()
{
  // kill the thread if running
  if (!m_running)
    wait();
  else
  {
    m_running = false;

    TerminateThread(getThreadHandle(), 0);
    CloseHandle(getThreadHandle());
  }

  // Make sure start finished mutex is unlocked before it's destroyed
  if (m_start_finished_mutex.try_lock())
    m_start_finished_mutex.unlock();
}

bool Thread::start()
{
  std::lock_guard lock(m_mutex);

  if (m_running)
    return false;

  m_request_stop = false;

  // The mutex may already be locked if the thread is being restarted
  // FIXME: what if this fails, or if already locked by same thread?
  std::unique_lock sf_lock(m_start_finished_mutex, std::try_to_lock);

  try { m_thread_obj = NEW std::thread(threadProc, this); }
  catch ([[maybe_unused]] const std::system_error& e) { return false; }

  while (!m_running) { Sleep(1); }

  // Allow spawned thread to continue
  sf_lock.unlock();

  m_joinable = true;

  return true;
}

bool Thread::stop()
{
  m_request_stop = true;
  return true;
}

bool Thread::wait()
{
  std::lock_guard lock(m_mutex);

  if (!m_joinable)
    return false;

  m_thread_obj->join();

  delete m_thread_obj;
  m_thread_obj = nullptr;

  assert(m_running == false);
  m_joinable = false;
  return true;
}

bool Thread::getReturnValue(void** ret) const
{
  if (m_running)
    return false;

  *ret = m_retval;
  return true;
}

void Thread::threadProc(Thread* thr)
{
  g_currentThread = thr;

  setName(thr->m_name);

  thr->m_running = true;

  // Wait for the thread that started this one to finish initializing the
  // thread handle so that getThreadId/getThreadHandle will work.
  std::unique_lock sf_lock(thr->m_start_finished_mutex);

  thr->m_retval = thr->run();

  thr->m_running = false;
  // Unlock m_start_finished_mutex to prevent data race condition on Windows.
  // On Windows with VS2017 build TerminateThread is called and this mutex is not
  // released. We try to unlock it from caller thread and it's refused by system.
  sf_lock.unlock();
}
