#include "cHTTPSServer_V2.h"

#include <algorithm>
#include <utility>

cHTTPSServer_V2::cHTTPSServer_V2(
    std::string name,
    std::uint16_t port,
    std::string certificateFile,
    std::string privateKeyFile,
    std::string verifyFile,
    std::size_t ioThreadCount)
    : cBaseWorker_V2(std::move(name)),
      m_server(std::make_unique<server_type>(
          certificateFile,
          privateKeyFile,
          verifyFile)),
      m_ioThreadCount((ioThreadCount == 0U) ? 1U : ioThreadCount),
      m_certificateFile(std::move(certificateFile)),
      m_privateKeyFile(std::move(privateKeyFile)),
      m_verifyFile(std::move(verifyFile))
{
  m_server->config.port = port;
}

cHTTPSServer_V2::~cHTTPSServer_V2() noexcept
{
  (void)stopThread();
}

cHTTPSServer_V2::server_type &cHTTPSServer_V2::server() noexcept
{
  return *m_server;
}

const cHTTPSServer_V2::server_type &cHTTPSServer_V2::server() const noexcept
{
  return *m_server;
}

std::shared_ptr<SimpleWeb::io_context> cHTTPSServer_V2::ioContext() const noexcept
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_io;
}

void cHTTPSServer_V2::setIOThreadCount(std::size_t ioThreadCount) noexcept
{
  if (isRunning())
  {
    return;
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  m_ioThreadCount = (ioThreadCount == 0U) ? 1U : ioThreadCount;
}

std::size_t cHTTPSServer_V2::ioThreadCount() const noexcept
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_ioThreadCount;
}

std::uint16_t cHTTPSServer_V2::configuredPort() const noexcept
{
  return m_server->config.port;
}

std::string cHTTPSServer_V2::bindAddress() const
{
  return m_server->config.address;
}

std::uint16_t cHTTPSServer_V2::listeningPort() const noexcept
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_listeningPort;
}

std::string cHTTPSServer_V2::certificateFile() const noexcept
{
  return m_certificateFile;
}

std::string cHTTPSServer_V2::privateKeyFile() const noexcept
{
  return m_privateKeyFile;
}

std::string cHTTPSServer_V2::verifyFile() const noexcept
{
  return m_verifyFile;
}

std::string cHTTPSServer_V2::lastError() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_lastError;
}

bool cHTTPSServer_V2::startThread(duration_type waitForStartTimeout)
{
  resetStartState();

  if (!cBaseWorker_V2::startThread(duration_type::zero()))
  {
    return false;
  }

  return waitUntilListening(waitForStartTimeout);
}

bool cHTTPSServer_V2::startThread(std::uint16_t waitForStartTimeoutMilliSec)
{
  return startThread(duration_type{waitForStartTimeoutMilliSec});
}

bool cHTTPSServer_V2::preRun()
{
  try
  {
    auto io = std::make_shared<SimpleWeb::io_context>();

    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_io = io;
      m_userOnError = m_server->on_error;
      m_server->io_service = io;
      m_server->config.thread_pool_size = 1U;
      m_server->on_error =
          [this](request_ptr request, const SimpleWeb::error_code &error)
      {
        updateHeartbeat();
        setLastError(error.message());

        const auto userHandler = m_userOnError;
        if (userHandler)
        {
          userHandler(std::move(request), error);
        }
      };
    }

    return true;
  }
  catch (const std::exception &error)
  {
    resolveStartIfPending(false, 0U, error.what());
  }
  catch (...)
  {
    resolveStartIfPending(false, 0U, "Failed to initialize HTTPS server");
  }

  return false;
}

void cHTTPSServer_V2::run()
{
  try
  {
    m_server->start([this](unsigned short port)
                    {
      updateHeartbeat();
      resolveStartIfPending(true, port, {}); });

    processWork();

    resolveStartIfPending(
        false,
        0U,
        stopRequested() ? "HTTPS server stopped" : "HTTPS server exited unexpectedly");
  }
  catch (const std::exception &error)
  {
    resolveStartIfPending(false, 0U, error.what());
    throw;
  }
  catch (...)
  {
    resolveStartIfPending(false, 0U, "Unhandled HTTPS server exception");
    throw;
  }

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_server->on_error = m_userOnError;
    m_server->io_service.reset();
    m_userOnError = {};
    m_io.reset();
  }
}

void cHTTPSServer_V2::stopTriggered()
{
  try
  {
    m_server->stop();
  }
  catch (...)
  {
  }

  auto io = ioContext();
  if (io)
  {
    io->stop();
  }

  resolveStartIfPending(false, 0U, "HTTPS server stop requested");
}

void cHTTPSServer_V2::processWork()
{
  auto io = ioContext();
  if (!io)
  {
    return;
  }

  const auto runIo = [this, io]() noexcept
  {
    try
    {
      io->run();
    }
    catch (const std::exception &error)
    {
      setLastError(error.what());
      io->stop();
      requestStop();
    }
    catch (...)
    {
      setLastError("Unhandled io_context exception");
      io->stop();
      requestStop();
    }
  };

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_ioThreads.clear();

    for (std::size_t index = 1U; index < m_ioThreadCount; ++index)
    {
      m_ioThreads.emplace_back(runIo);
    }
  }

  runIo();

  std::vector<std::thread> threadsToJoin;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    threadsToJoin.swap(m_ioThreads);
  }

  for (auto &thread : threadsToJoin)
  {
    if (thread.joinable())
    {
      thread.join();
    }
  }
}

void cHTTPSServer_V2::resetStartState()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_listeningPort = 0U;
  m_startResolved = false;
  m_startSucceeded = false;
  m_lastError.clear();
}

void cHTTPSServer_V2::resolveStartIfPending(
    bool success,
    std::uint16_t port,
    std::string errorMessage)
{
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_startResolved)
    {
      if (!errorMessage.empty())
      {
        m_lastError = std::move(errorMessage);
      }
      return;
    }

    m_startResolved = true;
    m_startSucceeded = success;
    m_listeningPort = success ? port : 0U;
    m_lastError = std::move(errorMessage);
  }

  m_startCondition.notify_all();
}

void cHTTPSServer_V2::setLastError(std::string errorMessage)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_lastError = std::move(errorMessage);
}

bool cHTTPSServer_V2::waitUntilListening(duration_type waitForStartTimeout)
{
  if (waitForStartTimeout == duration_type::zero())
  {
    return true;
  }

  std::unique_lock<std::mutex> lock(m_mutex);
  const bool completed = m_startCondition.wait_for(
      lock,
      waitForStartTimeout,
      [this]()
      {
        return m_startResolved;
      });

  if (!completed)
  {
    return false;
  }

  return m_startSucceeded;
}
