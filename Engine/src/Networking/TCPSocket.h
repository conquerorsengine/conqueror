#pragma once

#include "NetworkTypes.h"
#include <string>
#include <vector>
#include <functional>

namespace Conqueror
{
    /// Simple blocking TCP socket wrapper using POSIX sockets.
    /// Used for lobby, chat, file transfer — NOT for real-time gameplay.
    class TCPSocket
    {
    public:
        TCPSocket();
        ~TCPSocket();

        TCPSocket(const TCPSocket&) = delete;
        TCPSocket& operator=(const TCPSocket&) = delete;

        // ── Server ──
        /// Bind and listen on a port
        bool Listen(uint16_t port, int backlog = 16);
        /// Accept a new client connection. Returns the client socket fd, or -1.
        int  Accept();

        // ── Client ──
        /// Connect to a remote host
        bool Connect(const std::string& host, uint16_t port);

        // ── I/O ──
        /// Send data on a specific socket fd (or the main socket if fd == -1)
        bool Send(const uint8_t* data, size_t size, int socketFd = -1);
        /// Receive data. Returns bytes read, 0 on disconnect, -1 on error.
        int  Receive(uint8_t* buffer, size_t bufferSize, int socketFd = -1);

        // ── Lifecycle ──
        void Close();
        bool IsValid() const { return m_Socket >= 0; }
        int  GetSocket() const { return m_Socket; }

        /// Set socket to non-blocking mode
        void SetNonBlocking(bool nonBlocking);
        void SetNonBlocking(int fd, bool nonBlocking);

    private:
        int m_Socket = -1;
    };
}
