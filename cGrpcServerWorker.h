#pragma once

#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>
#include "examples/gRPC/gRPCAnalytics.grpc.pb.h" // Your generated protobuf header
#include "cBaseWorker_V2.h"                      // Your base class header

using analytics::AnalyticsService;
using analytics::MetricRequest;
using analytics::MetricResponse;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerCompletionQueue;
using grpc::ServerContext;
using grpc::Status;

class cGrpcServerWorker final : public cBaseWorker_V2
{
public:
    explicit cGrpcServerWorker(std::string name, std::string server_address)
        : cBaseWorker_V2(std::move(name)), m_serverAddress(std::move(server_address)) {}

    ~cGrpcServerWorker() noexcept override
    {
        // Essential: stop the background thread before members are destroyed
        stopThread();
    }

protected:
    // 1. Initialise the gRPC infrastructure
    bool preRun() override
    {
        try
        {
            ServerBuilder builder;
            builder.AddListeningPort(m_serverAddress, grpc::InsecureServerCredentials());
            builder.RegisterService(&m_asyncService);

            // Allocate the asynchronous completion queue
            m_cq = builder.AddCompletionQueue();
            m_server = builder.BuildAndStart();

            if (!m_server)
            {
                return false;
            }

            std::cout << "[" << name() << "] gRPC Server listening on " << m_serverAddress << "\n";
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    // 2. Drive the Asynchronous Event Loop
    void run() override
    {
        // Enqueue the first instance to handle incoming network RPCs
        new CallData(&m_asyncService, m_cq.get(), this);

        void *tag = nullptr;
        bool ok = false;

        // Next() blocks until an event occurs or the queue shuts down
        while (m_cq->Next(&tag, &ok))
        {
            // Keep your base worker heartbeat alive during long runtimes
            updateHeartbeat();

            if (!ok)
            {
                // Event dropped (typically occurs during shutdown), clean up allocation
                delete static_cast<CallData *>(tag);
                continue;
            }

            // Move the individual RPC state machine forward
            static_cast<CallData *>(tag)->Proceed();
        }

        std::cout << "[" << name() << "] Event loop terminated cleanly.\n";
    }

    // 3. Handle Cooperative Shutdown Hooks from cBaseWorker_V2
    void stopTriggered() override
    {
        std::cout << "[" << name() << "] Shutdown triggered. Draining RPCs...\n";

        if (m_server)
        {
            // Stop accepting new incoming requests immediately
            m_server->Shutdown();
        }

        if (m_cq)
        {
            // Drain remaining requests out of the event loop and unblock Next()
            m_cq->Shutdown();
        }
    }

private:
    std::string m_serverAddress;
    AnalyticsService::AsyncService m_asyncService;
    std::unique_ptr<ServerCompletionQueue> m_cq;
    std::unique_ptr<Server> m_server;

    // --- Inner Class to Manage Individual RPC Lifecycles ---
    class CallData
    {
    public:
        CallData(AnalyticsService::AsyncService *service, ServerCompletionQueue *cq, cGrpcServerWorker *parent)
            : m_service(service), m_cq(cq), m_parentWorker(parent), m_responder(&m_ctx), m_status(CREATE)
        {
            Proceed();
        }

        void Proceed()
        {
            if (m_status == CREATE)
            {
                m_status = PROCESS;
                // Wait for the specified method to arrive over the wire
                m_service->RequestSendMetric(&m_ctx, &m_request, &m_responder, m_cq, m_cq, this);
            }
            else if (m_status == PROCESS)
            {
                // If the parent worker is shutting down, stop spawning new listeners
                if (m_parentWorker->continueRunning())
                {
                    new CallData(m_service, m_cq, m_parentWorker);
                }

                // --- Execute actual business logic here ---
                std::cout << "Processing metric from device: " << m_request.device_id() << "\n";

                m_reply.set_success(true);
                m_reply.set_message("Metric written safely via Worker loop.");
                // ------------------------------------------

                m_status = FINISH;
                m_responder.Finish(m_reply, Status::OK, this);
            }
            else
            {
                assert(m_status == FINISH);
                delete this; // Safe self-deletion when connection tracking completes
            }
        }

    private:
        AnalyticsService::AsyncService *m_service;
        ServerCompletionQueue *m_cq;
        cGrpcServerWorker *m_parentWorker;
        ServerContext m_ctx;

        MetricRequest m_request;
        MetricResponse m_reply;
        grpc::ServerAsyncResponseWriter<MetricResponse> m_responder;

        enum enm_RpcStatus
        {
            CREATE,
            PROCESS,
            FINISH
        };
        enm_RpcStatus m_status;
    };
};
