#include <iostream>
#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>
#include "gRPCAnalytics.grpc.pb.h" // Generated header

using analytics::AnalyticsService;
using analytics::MetricRequest;
using analytics::MetricResponse;
using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

class AnalyticsClient
{
public:
    AnalyticsClient(std::shared_ptr<Channel> channel)
        : stub_(AnalyticsService::NewStub(channel)) {}

    void SendDeviceData(const std::string &device_id, double value)
    {
        MetricRequest request;
        request.set_device_id(device_id);
        request.set_reading_value(value);
        request.set_timestamp(1771746400); // 2026 Epoch timestamp example

        MetricResponse reply;
        ClientContext context;

        // The actual network call happens here
        Status status = stub_->SendMetric(&context, request, &reply);

        if (status.ok())
        {
            std::cout << "Success: " << reply.message() << "\n";
        }
        else
        {
            std::cout << "RPC failed: " << status.error_message() << "\n";
        }
    }

private:
    std::unique_ptr<AnalyticsService::Stub> stub_;
};

int main()
{
    // Connect to the server over HTTP/2
    auto channel = grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials());
    AnalyticsClient client(channel);

    // Fire a quick tracking request
    client.SendDeviceData("Sensor_A1", 41.85);
    client.SendDeviceData("Sensor_A2", 42.85);
    client.SendDeviceData("Sensor_A3", 43.85);
    return 0;
}
