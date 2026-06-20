#include "TCPSocket.h"
#include "Core/Logging/Log.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>

namespace Conqueror
{
    TCPSocket::TCPSocket() = default;

    TCPSocket::~TCPSocket()
    {
        Close();
    }

    bool TCPSocket::Listen(uint16_t port, int backlog)
    {
        m_Socket = socket(AF_INET, SOCK_STREAM, 0);
        if (m_Socket < 0)
        {
            CQ_CORE_ERROR("[TCP] Failed to create socket: {0}", strerror(errno));
            return false;
        }

        // Allow port reuse
        int opt = 1;
        setsockopt(m_Socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (bind(m_Socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        {
            CQ_CORE_ERROR("[TCP] Failed to bind port {0}: {1}", port, strerror(errno));
            Close();
            return false;
        }

        if (listen(m_Socket, backlog) < 0)
        {
            CQ_CORE_ERROR("[TCP] Failed to listen: {0}", strerror(errno));
            Close();
            return false;
        }

        CQ_CORE_INFO("[TCP] Listening on port {0}", port);
        return true;
    }

    int TCPSocket::Accept()
    {
        sockaddr_in clientAddr{};
        socklen_t addrLen = sizeof(clientAddr);
        int clientFd = accept(m_Socket, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
        if (clientFd >= 0)
        {
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &clientAddr.sin_addr, ip, sizeof(ip));
            CQ_CORE_INFO("[TCP] Client connected from {0}:{1}", ip, ntohs(clientAddr.sin_port));
        }
        return clientFd;
    }

    bool TCPSocket::Connect(const std::string& host, uint16_t port)
    {
        m_Socket = socket(AF_INET, SOCK_STREAM, 0);
        if (m_Socket < 0)
        {
            CQ_CORE_ERROR("[TCP] Failed to create socket: {0}", strerror(errno));
            return false;
        }

        struct addrinfo hints{}, *result;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        std::string portStr = std::to_string(port);
        if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result) != 0)
        {
            CQ_CORE_ERROR("[TCP] Failed to resolve host: {0}", host);
            Close();
            return false;
        }

        if (connect(m_Socket, result->ai_addr, result->ai_addrlen) < 0)
        {
            CQ_CORE_ERROR("[TCP] Failed to connect to {0}:{1}: {2}", host, port, strerror(errno));
            freeaddrinfo(result);
            Close();
            return false;
        }

        freeaddrinfo(result);
        CQ_CORE_INFO("[TCP] Connected to {0}:{1}", host, port);
        return true;
    }

    bool TCPSocket::Send(const uint8_t* data, size_t size, int socketFd)
    {
        int fd = (socketFd >= 0) ? socketFd : m_Socket;
        if (fd < 0) return false;

        size_t totalSent = 0;
        while (totalSent < size)
        {
            ssize_t sent = send(fd, data + totalSent, size - totalSent, MSG_NOSIGNAL);
            if (sent <= 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
                CQ_CORE_ERROR("[TCP] Send failed: {0}", strerror(errno));
                return false;
            }
            totalSent += sent;
        }
        return true;
    }

    int TCPSocket::Receive(uint8_t* buffer, size_t bufferSize, int socketFd)
    {
        int fd = (socketFd >= 0) ? socketFd : m_Socket;
        if (fd < 0) return -1;

        ssize_t received = recv(fd, buffer, bufferSize, 0);
        if (received < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            return -1;
        }
        return static_cast<int>(received);
    }

    void TCPSocket::Close()
    {
        if (m_Socket >= 0)
        {
            close(m_Socket);
            m_Socket = -1;
        }
    }

    void TCPSocket::SetNonBlocking(bool nonBlocking)
    {
        SetNonBlocking(m_Socket, nonBlocking);
    }

    void TCPSocket::SetNonBlocking(int fd, bool nonBlocking)
    {
        if (fd < 0) return;
        int flags = fcntl(fd, F_GETFL, 0);
        if (nonBlocking)
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        else
            fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    }
}
