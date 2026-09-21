# ThreadComponent

ThreadComponent is a small C++17 networking and concurrency toolkit from Hotbits. It collects reusable worker, queue, container, TCP, HTTP, and HTTPS components that are designed to be practical in service-style applications and easy to embed in larger projects.

The current focus is the V2 API set:

- `cBaseWorker_V2` for cooperative background workers
- `cThreadQueue_V2` for simple thread-safe queued handoff
- `simple_container_V2` for thread-safe sequence-style buffering
- `TCPClient_V2`, `TCPServer_V2`, and `cTCPSyncClient_V2` for TCP client/server use cases
- `cHTTPServer_V2` and `cHTTPSServer_V2` for Simple-Web-Server based HTTP services
- `cHTTPClient` for outbound HTTP requests with libcurl
- `cGenericGrpcWorker` and `cGrpcServerWorker` for asynchronous gRPC servers that inherit the cooperative worker lifecycle (`heartbeat`, graceful shutdown, restart support)

Legacy headers are still present for compatibility, but the maintained examples and build targets are centered on the V2 classes and the currently self-contained components.

## Requirements

- C++17 compiler
- CMake 3.16+
- OpenSSL development package
- libcurl development package
- POSIX-style sockets environment
  - tested shape is macOS/Linux
- Optional: gRPC C++ (`grpc++`) and Protobuf development packages, plus the
  `protoc` and `grpc_cpp_plugin` tools. Only needed to build the gRPC examples.
- The sibling `OpenSource` dependency tree expected by this project:
  - `OpenSource/Simple-Web-Server`
  - `OpenSource/asio/asio-1.36.0/include`
  - `OpenSource/gzip/gzip-hpp-0.1.0/include`
  - `OpenSource/json/rapidjson-1.1.0/include`

By default the root `CMakeLists.txt` assumes this folder lives under:

`<repo>/Software/ThreadComponent`

and that dependencies live under:

`<repo>/OpenSource`

If your checkout layout is different, pass `THREADCOMPONENT_REPOSITORY_ROOT` or `THREADCOMPONENT_OPENSOURCE_DIR` to CMake.

## Build

From the project root:

```bash
cmake -S . -B build
cmake --build build
```

If the `OpenSource` folder is somewhere else:

```bash
cmake -S . -B build -DTHREADCOMPONENT_OPENSOURCE_DIR=/path/to/OpenSource
cmake --build build
```

Example binaries are written to:

`build/bin/`

## Example Targets

The curated examples built by CMake are:

- `baseWorkerExample_V2.bin`
- `threadQueueExample_V2.bin`
- `simpleContainerExample_V2.bin`
- `clientTest_V2.bin`
- `serverTest_V2.bin`
- `tcpSyncClientExample_V2.bin`
- `httpClientExample.bin`
- `httpServerTest.bin`
- `httpServerTest_V2.bin`
- `httpsServerTest.bin`
- `httpsServerTest_V2.bin`

The HTTPS examples use the sample certificate files in `examples/certs/`.

When a gRPC + Protobuf toolchain is detected, the following gRPC examples are
also built (see `examples/gRPC/README` or `examples/README.md`):

- `generic_server`
- `worker_server`
- `async_server`
- `grpc_server`
- `grpc_client`

The gRPC examples are optional: machines without gRPC installed still build
the rest of the project. They can also be configured and built standalone
from `examples/gRPC/`.

## Running Examples

Examples are intentionally small and self-contained:

- `baseWorkerExample_V2.bin`
  - starts a derived worker, emits heartbeat updates, then stops cleanly
- `threadQueueExample_V2.bin`
  - shows producer/consumer style queue handoff
- `simpleContainerExample_V2.bin`
  - demonstrates batch insert, timed grab, and snapshot usage
- `serverTest_V2.bin`
  - starts a TCP server on port `8080`
- `clientTest_V2.bin`
  - connects to `127.0.0.1:8080` and sends interactive messages
- `tcpSyncClientExample_V2.bin`
  - performs one synchronous TCP request/response exchange
- `httpClientExample.bin`
  - sends an outbound HTTP request with `cHTTPClient`
- `httpServerTest.bin` and `httpServerTest_V2.bin`
  - start HTTP servers with `/health` and `/echo` routes
- `httpsServerTest.bin` and `httpsServerTest_V2.bin`
  - start HTTPS servers with the bundled demo certificate
- gRPC servers and client (built from `examples/gRPC/`, optional dependencies):
  - generic_server, worker_server, async_server, and grpc_server all listen
    for `analytics.AnalyticsService` metrics on `0.0.0.0:50051`
  - `generic_server` demonstrates the CRTP engine `cGenericGrpcWorker`
  - `worker_server` demonstrates the concrete `cGrpcServerWorker` specialization
  - `async_server` is the manual, framework-free async reference implementation
  - `grpc_server` is the classic synchronous gRPC server
  - `grpc_client` sends a few metrics to `localhost:50051` and prints replies

The `examples/README.md` file describes each example in a little more detail.

## Project Layout

- `cBaseWorker_V2.h`
  - RAII-oriented worker thread base class
- `cThreadQueue_V2.h`
  - thread-safe queue wrapper
- `simple_container_V2.h`
  - thread-safe sequence container
- `cTCPClient_V2.h`
  - asynchronous TCP client
- `cTCPServer_V2.h`
  - asynchronous TCP server
- `cTCPSyncClient_V2.h`
  - synchronous request/response TCP client
- `cHTTPClient.h` / `cHTTPClient.cpp`
  - libcurl-backed HTTP client
- `cGenericGrpcWorker.h`
  - generic CRTP engine that runs an asynchronous gRPC server on a
    `cBaseWorker_V2` thread (`heartbeat`, graceful shutdown, restart-friendly)
- `cGrpcServerWorker.h`
  - concrete `analytics.AnalyticsService` specialization of `cGenericGrpcWorker`
- `cHTTPServer*.h/.cpp`
  - HTTP server wrappers
- `cHTTPSServer*.h/.cpp`
  - HTTPS server wrappers
- `examples/`
  - runnable sample programs, the gRPC examples, and TLS demo certs

## Notes

- Not every legacy source file in this folder is built by default.
- The old `clientTest.cpp`, `serverTest.cpp`, and `cTestSimpleContainer.cpp` are retained as reference material, but the supported example set is the curated one wired into `examples/CMakeLists.txt`.
- `cGenericGrpcWorker` currently drives a single unary RPC per worker instance. A worker owns one `gRPCServiceTraits` configuration and inherits the standard `cBaseWorker_V2` lifecycle (`startThread`, `stopThread`, heartbeat monitoring).
- The gRPC worker components require a generated `.proto` service header. `cGrpcServerWorker` ships bound to the `analytics.AnalyticsService` example; generic services derive straight from `cGenericGrpcWorker`.
- This project currently targets POSIX networking APIs. Windows support would need additional socket abstraction work.

## License

This project is licensed under the GNU GPL v3.0. See `LICENSE`.

Third-party libraries and headers keep their own licenses.
