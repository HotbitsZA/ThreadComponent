#pragma once

#include <iostream>
#include <thread>
#include <functional>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "cBaseWorker.h"

class TCPClient : public cBaseWorker
{
public:
    using DataCallback = std::function<void(const char *, size_t)>;
    using SendCallback = std::function<void(char *&, size_t &)>;

    TCPClient(const std::string &name, const std::string &serverAddress, int port,
              DataCallback receiveCallback, SendCallback sendCallback)
        : cBaseWorker(name), m_serverAddress(serverAddress), m_port(port),
          m_receiveCallback(receiveCallback), m_sendCallback(sendCallback), m_socket(-1) {}

    ~TCPClient()
    {
        if (m_socket != -1)
        {
            close(m_socket);
        }
    }

protected:
    bool preRun() override
    {
        m_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (m_socket < 0)
        {
            std::cerr << "Error opening socket" << std::endl;
            return false;
        }

        sockaddr_in serverAddr;
        std::memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(m_port);

        if (inet_pton(AF_INET, m_serverAddress.c_str(), &serverAddr.sin_addr) <= 0)
        {
            std::cerr << "Invalid address/ Address not supported" << std::endl;
            return false;
        }

        if (connect(m_socket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
        {
            std::cerr << "Connection Failed" << std::endl;
            return false;
        }

        return true;
    }

    void run() override
    {
        std::thread readThread(&TCPClient::readServerResponses, this);
        std::thread sendThread(&TCPClient::sendDataToServer, this);

        readThread.join();
        sendThread.join();
    }

    void stopTriggered() override
    {
        close(m_socket);
        m_socket = -1;
    }

private:
    void readServerResponses()
    {
        char buffer[256];
        while (continueRunning())
        {
            ssize_t n = read(m_socket, buffer, sizeof(buffer));
            if (n <= 0)
            {
                break;
            }

            if (m_receiveCallback)
            {
                m_receiveCallback(buffer, n);
            }
        }
    }

    void sendDataToServer()
    {
        while (continueRunning())
        {
            char *buffer = nullptr;
            size_t size = 0;
            if (m_sendCallback)
            {
                m_sendCallback(buffer, size);
            }

            if (buffer && size > 0)
            {
                ssize_t n = write(m_socket, buffer, size);
                if (n <= 0)
                {
                    std::cerr << "Error sending data to server" << std::endl;
                    break;
                }
                delete[] buffer; // Assuming sendCallback allocates buffer with new[]
            }
        }
    }

    std::string m_serverAddress;
    int m_port;
    int m_socket;
    DataCallback m_receiveCallback;
    SendCallback m_sendCallback;
};
