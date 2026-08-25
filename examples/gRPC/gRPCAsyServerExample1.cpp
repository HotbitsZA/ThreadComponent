#include <cassert>
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

class AsyncServer final
{
public:
    ~AsyncServer()
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

    void Run()
    {
        std::string server_address("0.0.0.0:50051");
        ServerBuilder builder;

        builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
        builder.RegisterService(&service_);

        // Request a completion queue for the asynchronous event loop
        cq_ = builder.AddCompletionQueue();
        server_ = builder.BuildAndStart();
        std::cout << "Asynchronous Server listening on " << server_address << "\n";

        // Start the event loop thread
        HandleRpcs();
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
                std::cout << "Async received: " << request_.device_id() << "\n";
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
        void *tag; // Identifies which specific request event ready
        bool ok;

        // Non-blocking event loop engine
        while (cq_->Next(&tag, &ok))
        {
            assert(ok);
            static_cast<CallData *>(tag)->Proceed();
        }
    }

    std::unique_ptr<ServerCompletionQueue> cq_;
    AnalyticsService::AsyncService service_;
    std::unique_ptr<Server> server_;
};

int main()
{
    AsyncServer server;
    server.Run();
    return 0;
}
