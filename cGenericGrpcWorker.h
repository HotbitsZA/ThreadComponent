#pragma once

#include <atomic>
#include <cassert>
#include <chrono>
#include <exception>
#include <memory>
#include <string>
#include <utility>

#include <grpcpp/grpcpp.h>

#include "cBaseWorker_V2.h"

// ---------------------------------------------------------------------------
// gRPCServiceTraits
//   Structural trait template that groups the generated protobuf types that a
//   concrete gRPC worker specialises over.
// ---------------------------------------------------------------------------
template <typename TService, typename TRequest, typename TResponse>
struct gRPCServiceTraits
{
    using ServiceType = TService;
    using RequestType = TRequest;
    using ResponseType = TResponse;
};

// ---------------------------------------------------------------------------
// cGenericGrpcWorker<DerivedWorker, Traits>
//   CRTP base class that turns a cBaseWorker_V2 background thread into a fully
//   asynchronous gRPC server driving ONE unary RPC.
//
//   Derived workers MUST provide:
//     * static constexpr RequestRpcFn GetRpcBinding()  -- the protoc generated
//       RequestXxx member-function pointer on the AsyncService.
//     * grpc::Status OnExecuteRpc(ServerContext&, const RequestType&,
//                                 ResponseType&) override
//       -- business logic; simply return the Status to send back to the client.
//
//   Features / guarantees:
//     * The completion queue is polled with AsyncNext(), not Next(), so the
//       base-worker heartbeat stays fresh even while the server is completely
//       idle, and cooperative stop requests are honoured promptly.
//     * A fresh listener is opened for every incoming request until shutdown;
//       no RPC is ever silently dropped or leaked.
//     * Exceptions thrown from OnExecuteRpc() are caught and converted into a
//       gRPC INTERNAL status instead of tearing down the whole worker loop.
//     * Correct shutdown ordering (Server::Shutdown() -> CompletionQueue
//       drain), with a fully drained completion queue before it is destroyed.
//     * The actually-bound port is exposed (supports ephemeral ':0' binds).
// ---------------------------------------------------------------------------
template <typename DerivedWorker, typename Traits>
class cGenericGrpcWorker : public cBaseWorker_V2
{
public:
    using ServiceAsyncType = typename Traits::ServiceType::AsyncService;
    using RequestType = typename Traits::RequestType;
    using ResponseType = typename Traits::ResponseType;
    using ResponseWriter = grpc::ServerAsyncResponseWriter<ResponseType>;

    using RequestRpcFn = void (ServiceAsyncType::*)(
        grpc::ServerContext *,
        RequestType *,
        ResponseWriter *,
        grpc::CompletionQueue *,
        grpc::ServerCompletionQueue *,
        void *);

    /// Maximum time AsyncNext() may block on the completion queue before the
    /// background worker refreshes its heartbeat while idle.
    inline static constexpr std::chrono::milliseconds kIdlePollInterval{1000};

    explicit cGenericGrpcWorker(std::string name, std::string server_address)
        : cBaseWorker_V2(std::move(name)),
          m_serverAddress(std::move(server_address))
    {
    }

    ~cGenericGrpcWorker() noexcept override
    {
        (void)this->stopThread();
    }

    // The full listening address that was requested at construction time.
    [[nodiscard]] const std::string &serverAddress() const noexcept
    {
        return m_serverAddress;
    }

    // The port that was actually bound once the server is running. Useful for
    // ephemeral binds such as "127.0.0.1:0". Returns 0 while not running.
    [[nodiscard]] int boundPort() const noexcept
    {
        return m_boundPort.load(std::memory_order_acquire);
    }

    // Human readable reason for the most recent startup failure.
    [[nodiscard]] const std::string &lastError() const noexcept
    {
        return m_lastError;
    }

protected:
    bool preRun() override
    {
        try
        {
            grpc::ServerBuilder builder;
            int selectedPort = 0;
            builder.AddListeningPort(m_serverAddress,
                                     grpc::InsecureServerCredentials(),
                                     &selectedPort);
            builder.RegisterService(&m_asyncService);

            m_cq = builder.AddCompletionQueue();
            m_server = builder.BuildAndStart();

            if (!m_server)
            {
                m_lastError = "BuildAndStart() returned a null server";
                return false;
            }

            m_boundPort.store(selectedPort, std::memory_order_release);
            m_lastError.clear();
            return true;
        }
        catch (const std::exception &e)
        {
            m_lastError = std::string("gRPC server startup failed: ") + e.what();
            return false;
        }
        catch (...)
        {
            m_lastError = "gRPC server startup failed: unknown exception";
            return false;
        }
    }

    void run() override
    {
        // Arm the very first listener so incoming RPCs can be accepted.
        new CallData(&m_asyncService, m_cq.get(), static_cast<DerivedWorker *>(this));

        void *tag = nullptr;
        bool ok = false;

        while (true)
        {
            const auto deadline = std::chrono::system_clock::now() + kIdlePollInterval;
            const auto event = m_cq->AsyncNext(&tag, &ok, deadline);

            if (event == grpc::CompletionQueue::NextStatus::TIMEOUT)
            {
                // No traffic -- keep the heartbeat alive so monitoring never
                // flags a healthy, idle server as dead.
                this->updateHeartbeat();
                continue;
            }

            this->updateHeartbeat();

            if (event == grpc::CompletionQueue::NextStatus::SHUTDOWN)
            {
                // Queue is shut down AND fully drained; safe to destroy it.
                break;
            }

            // event == GOT_EVENT
            auto *call = static_cast<CallData *>(tag);

            if (!ok)
            {
                // The RPC was cancelled or the server is shutting down while
                // the corresponding operation was still in flight. The tag is
                // delivered exactly once -- reclaim it and keep draining.
                delete call;
                continue;
            }

            call->Proceed();
        }
    }

    void stopTriggered() override
    {
        // Cooperative shutdown sequence. Order matters:
        //   1. Stop accepting new RPCs.
        //   2. Shut the completion queue down (unblocks run(); drains tags).
        if (m_server)
        {
            m_server->Shutdown();
        }
        if (m_cq)
        {
            m_cq->Shutdown();
        }
    }

public:
    // ------------------------------------------------------------------
    // Business logic contract (CRTP dispatch target).
    // ------------------------------------------------------------------
    // Called on the worker thread for every successfully received request.
    // Implementations must fill `reply` and return the Status to send back.
    virtual grpc::Status OnExecuteRpc(grpc::ServerContext &,
                                      const RequestType &,
                                      ResponseType &) = 0;

private:
    std::string m_serverAddress;
    std::string m_lastError;
    std::atomic<int> m_boundPort{0};
    ServiceAsyncType m_asyncService;
    std::unique_ptr<grpc::ServerCompletionQueue> m_cq;
    std::unique_ptr<grpc::Server> m_server;

    // ------------------------------------------------------------------
    // Per-RPC lifecycle handler. One instance exists per accepted RPC.
    //
    // Raw new/delete is deliberate and standard for gRPC async servers:
    // each tag delivered by the completion queue is owned by exactly one
    // CallData, and every submitted operation is guaranteed to produce a
    // single completion event.
    // ------------------------------------------------------------------
    class CallData
    {
    public:
        CallData(ServiceAsyncType *service, grpc::ServerCompletionQueue *cq, DerivedWorker *parent)
            : m_service(service), m_cq(cq), m_parentWorker(parent), m_responder(&m_ctx), m_status(CREATE)
        {
            Proceed(); // arm the listener for the next request
        }

        // Standard 3-state async unary state machine. Advanced once per event
        // delivered by the completion queue.
        void Proceed()
        {
            if (m_status == CREATE)
            {
                m_status = PROCESS;

                // CRTP compilation hook: the concrete subclass binds the
                // protoc-generated RequestSendMetric-style method pointer.
                RequestRpcFn requestFn = DerivedWorker::GetRpcBinding();
                (m_service->*requestFn)(&m_ctx, &m_request, &m_responder, m_cq, m_cq, this);
            }
            else if (m_status == PROCESS)
            {
                // Keep servicing as long as the worker is running.
                if (m_parentWorker->continueRunning())
                {
                    new CallData(m_service, m_cq, m_parentWorker);
                }

                grpc::Status status;
                try
                {
                    status = m_parentWorker->OnExecuteRpc(m_ctx, m_request, m_reply);
                }
                catch (const std::exception &e)
                {
                    // Never let business-logic exceptions tear down the loop.
                    status = grpc::Status(grpc::StatusCode::INTERNAL,
                                          std::string("Unhandled exception in OnExecuteRpc: ") + e.what());
                }
                catch (...)
                {
                    status = grpc::Status(grpc::StatusCode::INTERNAL,
                                          "Unhandled exception in OnExecuteRpc");
                }

                m_status = FINISH;
                m_responder.Finish(m_reply, status, this);
            }
            else
            {
                assert(m_status == FINISH);
                delete this; // Finish tag delivered: no further events for this RPC
            }
        }

    private:
        ServiceAsyncType *m_service;
        grpc::ServerCompletionQueue *m_cq;
        DerivedWorker *m_parentWorker;
        grpc::ServerContext m_ctx;

        RequestType m_request;
        ResponseType m_reply;
        ResponseWriter m_responder;

        enum enm_RpcStatus
        {
            CREATE,
            PROCESS,
            FINISH
        };
        enm_RpcStatus m_status;
    };
};