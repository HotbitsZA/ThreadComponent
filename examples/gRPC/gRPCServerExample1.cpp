#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <grpcpp/grpcpp.h>
#include "gRPCAnalytics.grpc.pb.h" // Generated header

using analytics::AnalyticsService;
using analytics::MetricRequest;
using analytics::MetricResponse;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

// Inherit from the generated base class to implement the logic
class AnalyticsServiceImpl final : public AnalyticsService::Service
{
    Status SendMetric(ServerContext *context, const MetricRequest *request,
                      MetricResponse *reply) override
    {
        (void)context;

        // Performance Note: Keep this logic brief to maintain low latency
        std::cout << "Received metric from: " << request->device_id()
                  << " Value: " << request->reading_value() << "\n";

        // Build the fast binary response
        reply->set_success(true);
        reply->set_message("Metric processed successfully.");

        return Status::OK;
    }
};

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

    std::string server_address("0.0.0.0:50051");
    AnalyticsServiceImpl service;

    ServerBuilder builder;
    // Listen on the port without authentication for local testing
    int selected_port = 0;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials(), &selected_port);
    builder.RegisterService(&service);
    std::unique_ptr<Server> server(builder.BuildAndStart());
    if (!server)
    {
        std::cerr << "Failed to bind " << server_address << "\n";
        return EXIT_FAILURE;
    }
    std::cout << "High-performance Server listening on port " << selected_port << "\n";

    // Server::Wait() blocks indefinitely, so drive it from a helper thread and
    // let the main thread react to Ctrl+C / SIGTERM.
    std::thread waiter([&server]() { server->Wait(); });

    while (!g_shutdownRequested.load(std::memory_order_acquire))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::cout << "Shutdown signal received.\n";
    server->Shutdown();
    waiter.join();

    std::cout << "Sync server stopped cleanly.\n";
    return 0;
}