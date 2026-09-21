# Examples

This folder contains runnable sample programs for the supported ThreadComponent APIs.

## Built By CMake

- `baseWorkerExample_V2.cpp`
  - minimal derived `cBaseWorker_V2` implementation with cooperative shutdown
- `threadQueueExample_V2.cpp`
  - producer/consumer flow using `cThreadQueue_V2`
- `simpleContainerExample_V2.cpp`
  - `simple_container_V2` insert, timed grab, and snapshot usage
- `clientTest_V2.cpp`
  - interactive TCP client using `TCPClient_V2`
- `serverTest_V2.cpp`
  - TCP server using `TCPServer_V2`
- `tcpSyncClientExample_V2.cpp`
  - synchronous request/response use of `cTCPSyncClient_V2`
- `httpClientExample.cpp`
  - outbound HTTP request with `cHTTPClient`
- `httpServerTest.cpp`
  - HTTP server example using `cHTTPServer`
- `httpServerTest_V2.cpp`
  - HTTP server example using `cHTTPServer_V2`
- `httpsServerTest.cpp`
  - HTTPS server example using `cHTTPSServer`
- `httpsServerTest_V2.cpp`
  - HTTPS server example using `cHTTPSServer_V2`

## gRPC Examples

`gRPC/` contains gRPC servers and a client built around the generated
`gRPCAnalytics.proto` service (`analytics.AnalyticsService`). They are built
automatically when a gRPC + Protobuf toolchain is detected; `protoc` and
`grpc_cpp_plugin` generate the `.pb.cc`/`.grpc.pb.cc` sources at build time.
The folder can also be configured and built standalone.

- `gRPCGenericServerExample.cpp` -> `generic_server`
  - CRTP engine `cGenericGrpcWorker`; a derived worker only supplies `GetRpcBinding()`
    and `OnExecuteRpc()`. The engine handles the completion queue, heartbeat,
    graceful shutdown, and exception-safe RPC finishing.
- `gRPCWorkerServerExample.cpp` -> `worker_server`
  - concrete `cGrpcServerWorker` specialization of the generic engine
- `gRPCAsyServerExample1.cpp` -> `async_server`
  - manual, framework-free asynchronous server reference implementation
- `gRPCServerExample1.cpp` -> `grpc_server`
  - classic synchronous `Service::SendMetric` implementation
- `gRPCClientExample1.cpp` -> `grpc_client`
  - blocking client with connection-ready wait and per-call deadline

All servers listen on `0.0.0.0:50051` and respond to the client on
`localhost:50051`. Each server handles SIGINT/SIGTERM for a clean shutdown.

## Legacy Reference Files

These files are intentionally kept as reference material but are not part of the default CMake build:

- `cTestSimpleContainer.cpp`
- `clientTest.cpp`
- `serverTest.cpp`

They depend on older components or repository pieces that are not part of the curated standalone example set.

## TLS Demo Certificates

The HTTPS examples default to the files in:

- `certs/server.crt`
- `certs/server.key`

These are development-only demo files for local testing.
