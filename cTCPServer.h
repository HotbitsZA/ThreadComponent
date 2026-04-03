#pragma once

#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <functional>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include "cBaseWorker.h"

class TCPServer : public cBaseWorker
{
public:
    using DataCallback = std::function<void(const char *, size_t)>;

    TCPServer(const std::string &name, int port, DataCallback callback)
        : cBaseWorker(name), m_port(port), m_serverSocket(-1), m_dataCallback(callback) {}

    ~TCPServer()
    {
        if (m_serverSocket != -1)
        {
            close(m_serverSocket);
        }
    }

protected:
    bool preRun() override
    {
        m_serverSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (m_serverSocket < 0)
        {
            std::cerr << "Error opening socket" << std::endl;
            return false;
        }

        sockaddr_in serverAddr;
        std::memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY;
        serverAddr.sin_port = htons(m_port);

        if (bind(m_serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
        {
            std::cerr << "Error binding socket" << std::endl;
            return false;
        }

        listen(m_serverSocket, 5);
        return true;
    }

    void run() override
    {
        while (continueRunning())
        {
            sockaddr_in clientAddr;
            socklen_t clientLen = sizeof(clientAddr);
            int clientSocket = accept(m_serverSocket, (struct sockaddr *)&clientAddr, &clientLen);
            if (clientSocket < 0)
            {
                if (continueRunning())
                {
                    std::cerr << "Error on accept" << std::endl;
                }
                continue;
            }

            std::thread(&TCPServer::handleClient, this, clientSocket).detach();
        }
    }

    void stopTriggered() override
    {
        close(m_serverSocket);
        m_serverSocket = -1;
    }

private:
    void handleClient(int clientSocket)
    {
        char buffer[256];
        while (continueRunning())
        {
            ssize_t n = read(clientSocket, buffer, sizeof(buffer));
            if (n <= 0)
            {
                break;
            }

            if (m_dataCallback)
            {
                m_dataCallback(buffer, n);
            }
        }
        close(clientSocket);
    }

    int m_port;
    int m_serverSocket;
    DataCallback m_dataCallback;
};
