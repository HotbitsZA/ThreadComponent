#pragma once

#include <iostream>
#include <string>
#include <utility>

#include "cGenericGrpcWorker.h"
#include "gRPCAnalytics.grpc.pb.h" // Generated header; put the generated dir on the include path
//                                  (see examples/gRPC/CMakeLists.txt).

// ---------------------------------------------------------------------------
// cGrpcServerWorker
//   Concrete, framework-driven asynchronous gRPC server for the
//   `analytics.AnalyticsService` proto service.
//
//   This class is a *thin specialization* of cGenericGrpcWorker: all thread
//   management, completion-queue plumbing, heartbeat handling and graceful
//   shutdown logic is inherited from the generic engine. Only the protoc
//   method binding and the business logic live here.
// ---------------------------------------------------------------------------
class cGrpcServerWorker final : public cGenericGrpcWorker<
                                    cGrpcServerWorker,
                                    gRPCServiceTraits<analytics::AnalyticsService,
                                                      analytics::MetricRequest,
                                                      analytics::MetricResponse>>
{
public:
    using Self = cGrpcServerWorker;
    using Base = cGenericGrpcWorker<
        Self,
        gRPCServiceTraits<analytics::AnalyticsService,
                          analytics::MetricRequest,
                          analytics::MetricResponse>>;

    // Pull the generic constructor (name, server_address) forward.
    using Base::Base;

    // CRTP hook: bind the protoc-generated RequestSendMetric listener.
    static constexpr RequestRpcFn GetRpcBinding() noexcept
    {
        return &analytics::AnalyticsService::AsyncService::RequestSendMetric;
    }

    // Business logic, invoked on the worker thread for every request. Return
    // the Status that should be sent back to the client.
    grpc::Status OnExecuteRpc(grpc::ServerContext &ctx,
                              const analytics::MetricRequest &request,
                              analytics::MetricResponse &reply) override
    {
        (void)ctx;

        std::cout << "[" << name() << "] Processing metric from device: "
                  << request.device_id()
                  << " | reading: " << request.reading_value()
                  << " | timestamp: " << request.timestamp() << "\n";

        reply.set_success(true);
        reply.set_message("Metric written safely via Worker loop.");
        return grpc::Status::OK;
    }
};