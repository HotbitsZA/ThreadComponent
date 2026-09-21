#include <atomic>
#include <cassert>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <grpcpp/grpcpp.h>
#include "gRPCAnalytics.grpc.pb.h"

using analytics::AnalyticsService;
using analytics::MetricRequest;
using analytics::MetricResponse;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerCompletionQueue;
using grpc::ServerContext;
using grpc::Status;

// Plain, minimal reference implementation of an async gRPC server. It exists
// to show exactly what the engine in cGenericGrpcWorker.h abstracts away. See
// gRPCGenericServerExample.cpp / gRPCWorkerServerExample.cpp for the framework
// driven versions.
class AsyncServer final
{
public:
    ~AsyncServer()
    {
        Shutdown();
    }

    void Run()
    {
        std::string server_address("0.0.0.0:50051");
        ServerBuilder builder;

        builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
        builder.RegisterService(&service_);

        // Request a completion queue for the asynchronous event loop
        cq_ = builder.AddCompletionQueue();
        server_ = builder.BuildAndStart();
        if (!server_)
        {
            throw std::runtime_error("OS failed to bind " + server_address);
        }
        std::cout << "Asynchronous Server listening on " << server_address << "\n";

        HandleRpcs();
    }

    // Graceful stop: stops new connections, then unblocks + drains the loop.
    void Shutdown()
    {
        if (server_)
        {
            server_->Shutdown();
        }
        if (cq_)
        {
            cq_->Shutdown();
        }
    }

private:
    // Class to maintain the state of an individual RPC request lifecycle
    class CallData
    {
    public:
        CallData(AnalyticsService::AsyncService *service, ServerCompletionQueue *cq)
            : service_(service), cq_(cq), responder_(&ctx_), status_(CREATE)
        {
            Proceed(); // Instantly jump into the creation state
        }

        void Proceed()
        {
            if (status_ == CREATE)
            {
                status_ = PROCESS;
                // Request the system to start listening for this specific method call
                service_->RequestSendMetric(&ctx_, &request_, &responder_, cq_, cq_, this);
            }
            else if (status_ == PROCESS)
            {
                // Spawn a new CallData instance to handle the NEXT incoming client request
                new CallData(service_, cq_);

                // Execute business logic (Keep it fast!)
                std::cout << "Async received: " << request_.device_id()
                          << " value=" << request_.reading_value() << "\n";
                reply_.set_success(true);
                reply_.set_message("Async processing complete.");

                status_ = FINISH;
                responder_.Finish(reply_, Status::OK, this);
            }
            else
            {
                assert(status_ == FINISH);
                delete this; // Memory cleanup for completed request
            }
        }

    private:
        AnalyticsService::AsyncService *service_;
        ServerCompletionQueue *cq_;
        ServerContext ctx_;
        MetricRequest request_;
        MetricResponse reply_;
        grpc::ServerAsyncResponseWriter<MetricResponse> responder_;

        enum CallStatus
        {
            CREATE,
            PROCESS,
            FINISH
        };
        CallStatus status_;
    };

    void HandleRpcs()
    {
        // Spawn the first initial call handler
        new CallData(&service_, cq_.get());
        void *tag; // Identifies which specific request event is ready
        bool ok;

        // Blocking event loop engine
        while (cq_->Next(&tag, &ok))
        {
            if (!ok)
            {
                // Request was cancelled or the server shut down mid-flight:
                // reclaim the tag (it is delivered exactly once) and continue.
                delete static_cast<CallData *>(tag);
                continue;
            }
            static_cast<CallData *>(tag)->Proceed();
        }
    }

    std::unique_ptr<ServerCompletionQueue> cq_;
    AnalyticsService::AsyncService service_;
    std::unique_ptr<Server> server_;
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

    AsyncServer server;

    // Run the blocking event loop on its own thread so the main thread can
    // react to termination signals and orchestrate a graceful shutdown.
    std::thread eventLoop([&server]() { server.Run(); });

    while (!g_shutdownRequested.load(std::memory_order_acquire))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::cout << "Shutdown signal received.\n";
    server.Shutdown(); // unblocks eventLoop via the completion queue
    eventLoop.join();

    std::cout << "Async server stopped cleanly.\n";
    return 0;
}