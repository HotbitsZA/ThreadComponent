#include <chrono>
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
    explicit AnalyticsClient(std::shared_ptr<Channel> channel)
        : stub_(AnalyticsService::NewStub(channel)), m_channel(std::move(channel)) {}

    // Blocks until the channel is ready or the deadline expires.
    [[nodiscard]] bool waitForReady(std::chrono::system_clock::time_point deadline) const
    {
        return m_channel->WaitForConnected(deadline);
    }

    void SendDeviceData(const std::string &device_id, double value)
    {
        MetricRequest request;
        request.set_device_id(device_id);
        request.set_reading_value(value);
        request.set_timestamp(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count());

        MetricResponse reply;
        ClientContext context;

        // Fail fast if the server does not respond in time.
        context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));

        // The actual network call happens here
        Status status = stub_->SendMetric(&context, request, &reply);

        if (status.ok())
        {
            std::cout << "[" << device_id << "] Success: " << reply.message() << "\n";
        }
        else
        {
            std::cerr << "[" << device_id << "] RPC failed: " << status.error_message()
                      << " (" << static_cast<int>(status.error_code()) << ")\n";
        }
    }

private:
    std::unique_ptr<AnalyticsService::Stub> stub_;
    std::shared_ptr<Channel> m_channel;
};

int main()
{
    // Connect to the server over HTTP/2
    auto channel = grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials());
    AnalyticsClient client(channel);

    // Make sure the server is actually up before firing requests.
    const auto readyDeadline = std::chrono::system_clock::now() + std::chrono::seconds(10);
    if (!client.waitForReady(readyDeadline))
    {
        std::cerr << "Server did not become ready within 10s. Is it running?\n";
        return EXIT_FAILURE;
    }

    // Fire a quick tracking request
    client.SendDeviceData("Sensor_A1", 41.85);
    client.SendDeviceData("Sensor_A2", 42.85);
    client.SendDeviceData("Sensor_A3", 43.85);
    return 0;
}