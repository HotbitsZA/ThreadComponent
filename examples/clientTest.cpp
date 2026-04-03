#include <iostream>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include "cTCPClient.h"

std::atomic<bool> running(true);
std::queue<std::pair<char *, size_t>> sendQueue;
std::mutex queueMutex;
std::condition_variable queueCondition;

void onServerResponse(const char *data, size_t size)
{
    std::cout << "Server Response: " << std::string(data, size) << std::endl;
}

void onSendData(char *&data, size_t &size)
{
    std::unique_lock<std::mutex> lock(queueMutex);
    queueCondition.wait(lock, []
                        { return !sendQueue.empty() || !running; });

    if (!sendQueue.empty())
    {
        auto sendData = sendQueue.front();
        sendQueue.pop();
        data = sendData.first;
        size = sendData.second;
    }
    else
    {
        data = nullptr;
        size = 0;
    }
}

void userInputHandler()
{
    while (running)
    {
        std::string message;
        std::getline(std::cin, message);
        if (message == "exit")
        {
            running = false;
            queueCondition.notify_all();
            break;
        }

        size_t size = message.size();
        char *data = new char[size];
        std::memcpy(data, message.c_str(), size);

        {
            std::lock_guard<std::mutex> lock(queueMutex);
            sendQueue.push({data, size});
        }
        queueCondition.notify_one();
    }
}

int main()
{
    TCPClient client("TestTCPClient", "127.0.0.1", 8080, onServerResponse, onSendData);

    // Start the client
    if (client.startThread(5000))
    {
        std::cout << "Client started successfully" << std::endl;
    }
    else
    {
        std::cerr << "Failed to start client" << std::endl;
        return 1;
    }

    // Start the user input handler thread
    std::thread inputThread(userInputHandler);

    // Let the client run until user stops it
    inputThread.join();

    // Stop the client
    if (client.stopThread(5000))
    {
        std::cout << "Client stopped successfully" << std::endl;
    }
    else
    {
        std::cerr << "Failed to stop client" << std::endl;
    }

    return 0;
}
