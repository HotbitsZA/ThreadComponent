#include <iostream>
#include <memory>
#include <string>
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

        // Performance Note: Keep this logic brief to maintain low latency
        std::cout << "Received metric from: " << request->device_id()
                  << " Value: " << request->reading_value() << "\n";

        // Build the fast binary response
        reply->set_success(true);
        reply->set_message("Metric processed successfully.");

        return Status::OK;
    }
};

void RunServer()
{
    std::string server_address("0.0.0.0:50051");
    AnalyticsServiceImpl service;

    ServerBuilder builder;
    // Listen on the port without authentication for local testing
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "High-performance Server listening on " << server_address << "\n";
    server->Wait();
}

int main()
{
    RunServer();
    return 0;
}
