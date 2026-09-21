#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <string>

#include "cGrpcServerWorker.h"

namespace
{
    std::atomic<bool> g_shutdownRequested{false};
}

void SignalHandler(int signal)
{
    if (signal == SIGINT || signal == SIGTERM)
    {
        g_shutdownRequested.store(true, std::memory_order_release);
    }
}

int main()
{
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    std::cout << "--- Initialising cGrpcServerWorker ---\n";

    // cGrpcServerWorker is a thin specialization of the generic CRTP engine:
    // the completion queue, heartbeat and shutdown logic all come free.
    auto worker = std::make_unique<cGrpcServerWorker>("AnalyticsWorkerEngine",
                                                      "0.0.0.0:50051");

    if (!worker->startThread(std::chrono::milliseconds(3000)))
    {
        std::cerr << "CRITICAL ERROR: Failed to start gRPC worker: "
                  << worker->lastError() << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "Worker running. Listening on port " << worker->boundPort()
              << ". Press Ctrl+C to terminate cleanly.\n";

    while (!g_shutdownRequested.load(std::memory_order_acquire))
    {
        // Liveness monitoring: the engine refreshes its heartbeat even while
        // completely idle, so this is a trustworthy health signal.
        std::cout << "[Monitor] State: "
                  << static_cast<int>(worker->getState())
                  << " | Heartbeat period: "
                  << worker->heartBeatPeriod().count() << "ms\n";

        if (worker->lastUnhandledException())
        {
            std::cerr << "CRITICAL ERROR: Worker crashed with an unhandled exception.\n";
            break;
        }

        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    std::cout << "\nGraceful shutdown signal caught. Stopping worker...\n";
    worker->stopThreadAndJoin(std::chrono::milliseconds(5000));
    std::cout << "Worker stopped cleanly.\n";

    return EXIT_SUCCESS;
}