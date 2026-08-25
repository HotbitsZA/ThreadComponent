#pragma once

#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>
#include "cBaseWorker_V2.h"

// Define a structural trait template to group your gRPC components cleanly
template <typename TService, typename TRequest, typename TResponse>
struct gRPCServiceTraits
{
    using ServiceType = TService;
    using RequestType = TRequest;
    using ResponseType = TResponse;
};

// DerivedWorker: The class implementing the business logic (CRTP)
// Traits: The gRPCServiceTraits configuration
template <typename DerivedWorker, typename Traits>
class cGenericGrpcWorker : public cBaseWorker_V2
{
public:
    using ServiceAsyncType = typename Traits::ServiceType::AsyncService;
    using RequestType = typename Traits::RequestType;
    using ResponseType = typename Traits::ResponseType;
    using RequestRpcFn = void (ServiceAsyncType::*)(
        grpc::ServerContext *,
        RequestType *,
        grpc::ServerAsyncResponseWriter<ResponseType> *,
        grpc::CompletionQueue *,
        grpc::ServerCompletionQueue *,
        void *);

    explicit cGenericGrpcWorker(std::string name, std::string server_address)
        : cBaseWorker_V2(std::move(name)), m_serverAddress(std::move(server_address)) {}

    ~cGenericGrpcWorker() noexcept override
    {
        (void)this->stopThread();
    }

protected:
    bool preRun() override
    {
        try
        {
            grpc::ServerBuilder builder;
            builder.AddListeningPort(m_serverAddress, grpc::InsecureServerCredentials());
            builder.RegisterService(&m_asyncService);

            m_cq = builder.AddCompletionQueue();
            m_server = builder.BuildAndStart();

            return m_server != nullptr;
        }
        catch (...)
        {
            return false;
        }
    }

    void run() override
    {
        // Enqueue the initial event handler
        new CallData(&m_asyncService, m_cq.get(), static_cast<DerivedWorker *>(this));

        void *tag = nullptr;
        bool ok = false;

        while (m_cq->Next(&tag, &ok))
        {
            this->updateHeartbeat();

            if (!ok)
            {
                delete static_cast<CallData *>(tag);
                continue;
            }

            static_cast<CallData *>(tag)->Proceed();
        }
    }

    void stopTriggered() override
    {
        if (m_server)
        {
            m_server->Shutdown();
        }
        if (m_cq)
        {
            m_cq->Shutdown();
        }
    }

private:
    std::string m_serverAddress;
    ServiceAsyncType m_asyncService;
    std::unique_ptr<grpc::ServerCompletionQueue> m_cq;
    std::unique_ptr<grpc::Server> m_server;

    // --- Core Lifecycle Handler for RPC Streams ---
    class CallData
    {
    public:
        CallData(ServiceAsyncType *service, grpc::ServerCompletionQueue *cq, DerivedWorker *parent)
            : m_service(service), m_cq(cq), m_parentWorker(parent), m_responder(&m_ctx), m_status(CREATE)
        {
            Proceed();
        }

        void Proceed()
        {
            if (m_status == CREATE)
            {
                m_status = PROCESS;

                // CRTP compilation hook: extract the method binding pointer from your concrete subclass
                RequestRpcFn requestFn = DerivedWorker::GetRpcBinding();
                (m_service->*requestFn)(&m_ctx, &m_request, &m_responder, m_cq, m_cq, this);
            }
            else if (m_status == PROCESS)
            {
                if (m_parentWorker->continueRunning())
                {
                    new CallData(m_service, m_cq, m_parentWorker);
                }

                // Call the customized business logic defined inside your concrete implementation class
                m_status = FINISH;
                m_parentWorker->OnExecuteRpc(m_ctx, m_request, m_reply, m_responder, this);
            }
            else
            {
                assert(m_status == FINISH);
                delete this;
            }
        }

    private:
        ServiceAsyncType *m_service;
        grpc::ServerCompletionQueue *m_cq;
        DerivedWorker *m_parentWorker;
        grpc::ServerContext m_ctx;

        RequestType m_request;
        ResponseType m_reply;
        grpc::ServerAsyncResponseWriter<ResponseType> m_responder;

        enum enm_RpcStatus
        {
            CREATE,
            PROCESS,
            FINISH
        };
        enm_RpcStatus m_status;
    };
};
