#include <iostream>
#include <thread>
#include "cTCPServer.h"

void onDataReceived(const char* data, size_t size) {
    std::cout << "Received: " << std::string(data, size) << std::endl;
}

int main() {
    TCPServer server("TestTCPServer", 8080, onDataReceived);

    // Start the server
    if (server.startThread(5000)) {
        std::cout << "Server started successfully" << std::endl;
    } else {
        std::cerr << "Failed to start server" << std::endl;
        return 1;
    }

    // Let the server run for a while
    std::this_thread::sleep_for(std::chrono::seconds(30));

    // Stop the server
    if (server.stopThread(5000)) {
        std::cout << "Server stopped successfully" << std::endl;
    } else {
        std::cerr << "Failed to stop server" << std::endl;
    }

    return 0;
}
