cmake_minimum_required(VERSION 3.15)
project(grpc_async_example CXX)

# Require C++17 or higher for modern gRPC integrations
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 1. Locate the mandatory dependencies
find_package(Threads REQUIRED)
find_package(protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

# 2. Directory layout
#    - source directory contains the .proto and (committed) generated headers
#    - build directory receives freshly generated .pb/.grpc.pb artifacts
#    - ThreadComponent root hosts cGenericGrpcWorker.h / cGrpcServerWorker.h
set(THREADCOMPONENT_DIR "${CMAKE_CURRENT_LIST_DIR}/../..")
set(PROTO_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(PROTO_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")

# 3. Automation rules to compile .proto to .cc/.h files
set(PROTO_FILE "${PROTO_SRC_DIR}/gRPCAnalytics.proto")
set(GENERATED_PROTO_SRC "${PROTO_OUT_DIR}/gRPCAnalytics.pb.cc")
set(GENERATED_PROTO_HDR "${PROTO_OUT_DIR}/gRPCAnalytics.pb.h")
set(GENERATED_GRPC_SRC  "${PROTO_OUT_DIR}/gRPCAnalytics.grpc.pb.cc")
set(GENERATED_GRPC_HDR  "${PROTO_OUT_DIR}/gRPCAnalytics.grpc.pb.h")

add_custom_command(
    OUTPUT "${GENERATED_PROTO_SRC}" "${GENERATED_PROTO_HDR}"
           "${GENERATED_GRPC_SRC}" "${GENERATED_GRPC_HDR}"
    COMMAND protobuf::protoc
    ARGS --gRPC_out="${PROTO_OUT_DIR}" --cpp_out="${PROTO_OUT_DIR}"
         -I "${PROTO_SRC_DIR}" --plugin=protoc-gen-gRPC=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
         "${PROTO_FILE}"
    DEPENDS "${PROTO_FILE}"
)

# 4. Shared generated sources + per-target setup
set(GRPC_GENERATED_SOURCES
    "${GENERATED_PROTO_SRC}"
    "${GENERATED_GRPC_SRC}"
)

function(add_grpc_example target_name source_file)
    add_executable("${target_name}"
        "${CMAKE_CURRENT_SOURCE_DIR}/${source_file}"
        ${GRPC_GENERATED_SOURCES})
    target_include_directories("${target_name}" PRIVATE
        "${PROTO_SRC_DIR}"
        "${PROTO_OUT_DIR}"
        "${THREADCOMPONENT_DIR}")
    target_compile_options("${target_name}" PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries("${target_name}" PRIVATE
        gRPC::grpc++
        protobuf::libprotobuf
        Threads::Threads)
endfunction()

# 5. Order the generated sources before the example sources in a given
#    executable (avoids non-deterministic protoc re-runs inside the build dir).
set_source_files_properties(${GRPC_GENERATED_SOURCES} PROPERTIES GENERATED TRUE)

# 6. Define the Executables
#    generic_server      -> CRTP engine via cGenericGrpcWorker.h
#    worker_server       -> concrete engine via cGrpcServerWorker.h
#    async_server        -> manual async reference implementation
#    grpc_server         -> classic synchronous gRPC server
#    grpc_client         -> blocking client
add_grpc_example(generic_server gRPCGenericServerExample.cpp)
add_grpc_example(worker_server gRPCWorkerServerExample.cpp)
add_grpc_example(async_server gRPCAsyServerExample1.cpp)
add_grpc_example(grpc_server gRPCServerExample1.cpp)
add_grpc_example(grpc_client gRPCClientExample1.cpp)