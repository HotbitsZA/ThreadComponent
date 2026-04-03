#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

#include "cBaseWorker_V2.h"

namespace
{
  class ExampleWorker final : public cBaseWorker_V2
  {
  public:
    ExampleWorker()
        : cBaseWorker_V2("ExampleWorker")
    {
    }

    [[nodiscard]] int heartbeatCount() const noexcept
    {
      return m_heartbeatCount.load(std::memory_order_relaxed);
    }

  protected:
    bool preRun() override
    {
      std::lock_guard<std::mutex> lock(m_consoleMutex);
      std::cout << "Worker preRun()" << std::endl;
      return true;
    }

    void run() override
    {
      while (continueRunning())
      {
        ++m_heartbeatCount;
        updateHeartbeat();

        {
          std::lock_guard<std::mutex> lock(m_consoleMutex);
          std::cout << "Heartbeat #" << m_heartbeatCount.load(std::memory_order_relaxed) << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(300));
      }
    }

    void stopTriggered() override
    {
      std::lock_guard<std::mutex> lock(m_consoleMutex);
      std::cout << "Stop requested" << std::endl;
    }

  private:
    std::atomic<int> m_heartbeatCount{0};
    std::mutex m_consoleMutex;
  };
}

int main()
{
  ExampleWorker worker;

  if (!worker.startThread(1000))
  {
    std::cerr << "Failed to start example worker" << std::endl;
    return 1;
  }

  std::this_thread::sleep_for(std::chrono::seconds(2));

  if (!worker.stopThread(2000))
  {
    std::cerr << "Failed to stop example worker cleanly" << std::endl;
    return 1;
  }

  std::cout << "Worker stopped after " << worker.heartbeatCount() << " heartbeats" << std::endl;
  return 0;
}
