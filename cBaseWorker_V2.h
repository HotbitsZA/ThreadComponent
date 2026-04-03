#pragma once

/**
  cBaseWorker_V2

  C++17 RAII-oriented base class for background workers.

  Design goals:
    - no detached threads
    - no raw ownership / no new / delete
    - cooperative shutdown
    - deterministic state transitions
    - minimal surface area for derived workers

  Important lifetime note:
    If a derived worker owns blocking resources that are used by `run()`
    (for example sockets, file descriptors, or condition variables), the
    derived destructor should call `stopThread()` in its destructor body.
    This ensures the worker thread has fully exited before derived members
    are destroyed.
*/

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

class cBaseWorker_V2
{
public:
  using clock_type = std::chrono::steady_clock;
  using duration_type = std::chrono::milliseconds;

  enum class enm_State : std::uint8_t
  {
    Unknown = 0,
    Starting,
    Running,
    Stopping,
    Stopped
  };

  inline static constexpr duration_type kDefaultWaitTimeout{5000};

  virtual ~cBaseWorker_V2() noexcept
  {
    requestStop();
    joinThread();
  }

  cBaseWorker_V2() = delete;
  cBaseWorker_V2(const cBaseWorker_V2 &) = delete;
  cBaseWorker_V2 &operator=(const cBaseWorker_V2 &) = delete;
  cBaseWorker_V2(cBaseWorker_V2 &&) = delete;
  cBaseWorker_V2 &operator=(cBaseWorker_V2 &&) = delete;

  [[nodiscard]] bool startThread(duration_type waitForStartTimeout = kDefaultWaitTimeout)
  {
    joinFinishedThread();

    try
    {
      std::lock_guard<std::mutex> lock(m_stateMutex);
      if ((m_state != enm_State::Stopped) || m_thread.joinable())
      {
        return false;
      }

      m_stopRequested.store(false, std::memory_order_release);
      m_lastUnhandledException = nullptr;
      m_state = enm_State::Starting;
      m_thread = std::thread(&cBaseWorker_V2::threadRunner, this);
    }
    catch (...)
    {
      setState(enm_State::Stopped);
      return false;
    }

    {
      std::lock_guard<std::mutex> lock(m_heartBeatMutex);
      m_heartBeat = clock_type::now();
      m_heartBeatPeriod = duration_type::zero();
    }

    m_stateChanged.notify_all();

    if (waitForStartTimeout == duration_type::zero())
    {
      return true;
    }

    return waitForStart(waitForStartTimeout);
  }

  [[nodiscard]] bool startThread(std::uint16_t waitForStartTimeoutMilliSec)
  {
    return startThread(duration_type{waitForStartTimeoutMilliSec});
  }

  [[nodiscard]] bool stopThread(duration_type waitForStopTimeout = kDefaultWaitTimeout) noexcept
  {
    requestStop();

    if (waitForStopTimeout == duration_type::zero())
    {
      joinFinishedThread();
      return true;
    }

    if (!waitForStopped(waitForStopTimeout))
    {
      return false;
    }

    joinFinishedThread();
    return true;
  }

  [[nodiscard]] bool stopThread(std::uint16_t waitForStopTimeoutMilliSec) noexcept
  {
    return stopThread(duration_type{waitForStopTimeoutMilliSec});
  }

  void requestStop() noexcept
  {
    bool shouldTriggerStop = false;

    {
      std::lock_guard<std::mutex> lock(m_stateMutex);
      m_stopRequested.store(true, std::memory_order_release);

      if ((m_state == enm_State::Starting) || (m_state == enm_State::Running))
      {
        m_state = enm_State::Stopping;
        shouldTriggerStop = true;
      }
      else if (m_state == enm_State::Stopping)
      {
        shouldTriggerStop = true;
      }
    }

    m_stateChanged.notify_all();

    if (shouldTriggerStop)
    {
      try
      {
        stopTriggered();
      }
      catch (...)
      {
        // best effort shutdown hook
      }
    }
  }

  [[nodiscard]] bool isRunning() const noexcept
  {
    const auto state = getState();
    return (state == enm_State::Starting) || (state == enm_State::Running);
  }

  [[nodiscard]] enm_State getState() const noexcept
  {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_state;
  }

  [[nodiscard]] bool stopped() const noexcept
  {
    return getState() == enm_State::Stopped;
  }

  [[nodiscard]] const std::string &name() const noexcept
  {
    return m_name;
  }

  void updateHeartbeat(clock_type::time_point heartBeat = clock_type::now()) noexcept
  {
    std::lock_guard<std::mutex> lock(m_heartBeatMutex);
    m_heartBeatPeriod =
        std::chrono::duration_cast<duration_type>(heartBeat - m_heartBeat);
    m_heartBeat = heartBeat;
  }

  [[nodiscard]] clock_type::time_point lastHeartbeat() const noexcept
  {
    std::lock_guard<std::mutex> lock(m_heartBeatMutex);
    return m_heartBeat;
  }

  [[nodiscard]] duration_type heartBeatPeriod() const noexcept
  {
    std::lock_guard<std::mutex> lock(m_heartBeatMutex);
    return m_heartBeatPeriod;
  }

  [[nodiscard]] std::exception_ptr lastUnhandledException() const noexcept
  {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_lastUnhandledException;
  }

  // Compatibility wrappers for incremental migration from cBaseWorker.
  void UpdateHeartBeat(clock_type::time_point heartBeat) noexcept
  {
    updateHeartbeat(heartBeat);
  }

  [[nodiscard]] clock_type::time_point LastHeartBeat() const noexcept
  {
    return lastHeartbeat();
  }

  [[nodiscard]] std::uint64_t HeartBeatPeriod() const noexcept
  {
    return static_cast<std::uint64_t>(heartBeatPeriod().count());
  }

protected:
  explicit cBaseWorker_V2(std::string name)
      : m_name(std::move(name))
  {
  }

  [[nodiscard]] bool continueRunning() const noexcept
  {
    return !stopRequested();
  }

  [[nodiscard]] bool stopRequested() const noexcept
  {
    return m_stopRequested.load(std::memory_order_acquire);
  }

  void retire() noexcept
  {
    requestStop();
  }

  virtual bool preRun() = 0;
  virtual void run() = 0;
  virtual void stopTriggered() {}

private:
  void threadRunner() noexcept
  {
    try
    {
      if (!preRun())
      {
        setState(enm_State::Stopped);
        return;
      }

      if (stopRequested())
      {
        setState(enm_State::Stopped);
        return;
      }

      setState(enm_State::Running);
      run();
    }
    catch (...)
    {
      std::lock_guard<std::mutex> lock(m_stateMutex);
      m_lastUnhandledException = std::current_exception();
    }

    setState(enm_State::Stopped);
  }

  void setState(enm_State state) noexcept
  {
    {
      std::lock_guard<std::mutex> lock(m_stateMutex);
      m_state = state;
    }

    m_stateChanged.notify_all();
  }

  [[nodiscard]] bool waitForStart(duration_type timeout) const noexcept
  {
    std::unique_lock<std::mutex> lock(m_stateMutex);
    const auto ready = m_stateChanged.wait_for(
        lock,
        timeout,
        [this]()
        {
          return (m_state == enm_State::Running) || (m_state == enm_State::Stopped);
        });

    return ready && (m_state == enm_State::Running);
  }

  [[nodiscard]] bool waitForStopped(duration_type timeout) const noexcept
  {
    std::unique_lock<std::mutex> lock(m_stateMutex);
    return m_stateChanged.wait_for(
        lock,
        timeout,
        [this]()
        {
          return m_state == enm_State::Stopped;
        });
  }

  void joinFinishedThread() noexcept
  {
    std::thread finishedThread;

    {
      std::lock_guard<std::mutex> lock(m_stateMutex);
      if ((m_state == enm_State::Stopped) && m_thread.joinable())
      {
        finishedThread = std::move(m_thread);
      }
    }

    if (finishedThread.joinable())
    {
      finishedThread.join();
    }
  }

  void joinThread() noexcept
  {
    std::thread ownedThread;

    {
      std::lock_guard<std::mutex> lock(m_stateMutex);
      if (m_thread.joinable())
      {
        ownedThread = std::move(m_thread);
      }
    }

    if (ownedThread.joinable())
    {
      ownedThread.join();
    }
  }

  mutable std::mutex m_stateMutex;
  mutable std::condition_variable m_stateChanged;
  std::thread m_thread;
  std::atomic_bool m_stopRequested{false};
  enm_State m_state{enm_State::Stopped};
  std::exception_ptr m_lastUnhandledException;
  const std::string m_name;

  mutable std::mutex m_heartBeatMutex;
  clock_type::time_point m_heartBeat{clock_type::now()};
  duration_type m_heartBeatPeriod{duration_type::zero()};
};
