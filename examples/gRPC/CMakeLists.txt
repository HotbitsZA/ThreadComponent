cmake_minimum_required(VERSION 3.15)
project(grpc_async_example CXX)

# Require C++17 or higher for modern gRPC integrations
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Locate the mandatory dependencies
find_package(Threads REQUIRED)
find_package(protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

# 2. Paths for generated protobuf files
set(PROTO_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(PROTO_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")
include_directories(
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROTO_OUT_DIR}"
)

# 3. Automation rules to compile .proto to .cc/.h files
set(PROTO_FILE "${PROTO_SRC_DIR}/gRPCAnalytics.proto")
set(GENERATED_PROTO_SRC "${PROTO_OUT_DIR}/gRPCAnalytics.pb.cc")
set(GENERATED_PROTO_HDR "${PROTO_OUT_DIR}/gRPCAnalytics.pb.h")
set(GENERATED_GRPC_SRC  "${PROTO_OUT_DIR}/gRPCAnalytics.grpc.pb.cc")
set(GENERATED_GRPC_HDR  "${PROTO_OUT_DIR}/gRPCAnalytics.grpc.pb.h")

add_custom_command(
    OUTPUT "${GENERATED_PROTO_SRC}" "${GENERATED_PROTO_HDR}" "${GENERATED_GRPC_SRC}" "${GENERATED_GRPC_HDR}"
    COMMAND protobuf::protoc
    ARGS --gRPC_out="${PROTO_OUT_DIR}" --cpp_out="${PROTO_OUT_DIR}"
         -I "${PROTO_SRC_DIR}" --plugin=protoc-gen-gRPC=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
         "${PROTO_FILE}"
    DEPENDS "${PROTO_FILE}"
)

# 4. Define the Executables
add_executable(generic_server 
    gRPCGenericServerExample.cpp 
    ${GENERATED_PROTO_SRC} 
    ${GENERATED_GRPC_SRC}
)

add_executable(async_server 
    gRPCAsyServerExample1.cpp 
    ${GENERATED_PROTO_SRC} 
    ${GENERATED_GRPC_SRC}
)

add_executable(grpc_server 
    gRPCServerExample1.cpp 
    ${GENERATED_PROTO_SRC} 
    ${GENERATED_GRPC_SRC}
)

add_executable(grpc_client 
    gRPCClientExample1.cpp 
    ${GENERATED_PROTO_SRC} 
    ${GENERATED_GRPC_SRC}
)

# 5. Link the Targets to gRPC binary libraries
target_link_libraries(generic_server
    gRPC::grpc++
    protobuf::libprotobuf
    Threads::Threads
)

target_link_libraries(async_server
    gRPC::grpc++
    protobuf::libprotobuf
    Threads::Threads
)

target_link_libraries(grpc_server
    gRPC::grpc++
    protobuf::libprotobuf
    Threads::Threads
)

target_link_libraries(grpc_client
    gRPC::grpc++
    protobuf::libprotobuf
    Threads::Threads
)
