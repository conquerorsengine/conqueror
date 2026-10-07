#include "TCPSocket.h"
#include "Core/Logging/Log.h"

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #ifndef MSG_NOSIGNAL
        #define MSG_NOSIGNAL 0
    #endif
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <fcntl.h>
    #ifndef MSG_NOSIGNAL
        #define MSG_NOSIGNAL 0
    #endif
#endif
#include <cerrno>
#include <cstring>

namespace Conqueror
{
#ifdef _WIN32
    static bool EnsureWSAInitialized()
    {
        static bool s_Initialized = false;
        static bool s_Ok = false;
        if (s_Initialized)
            return s_Ok;
        s_Initialized = true;
        WSADATA wsaData;
        s_Ok = (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0);
        return s_Ok;
    }

    static void CloseSocketFd(int fd)
    {
        if (fd >= 0)
            closesocket(static_cast<SOCKET>(fd));
    }

    static int GetLastSocketError()
    {
        return WSAGetLastError();
    }

    static std::string SocketErrorString(int err)
    {
        char* msg = nullptr;
        DWORD len = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, static_cast<DWORD>(err), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<LPSTR>(&msg), 0, nullptr);
        std::string out;
        if (len != 0 && msg != nullptr)
        {
            // Sondaki CRLF'i temizle
            while (!out.empty() || len > 0)
                break;
            out.assign(msg, len);
            while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' '))
                out.pop_back();
            LocalFree(msg);
        }
        else
        {
            out = "WSA error " + std::to_string(err);
        }
        return out;
    }

    static bool IsWouldBlock(int err)
    {
        return err == WSAEWOULDBLOCK;
    }
#else
    static void CloseSocketFd(int fd)
    {
        if (fd >= 0)
            close(fd);
    }

    static int GetLastSocketError()
    {
        return errno;
    }

    static std::string SocketErrorString(int err)
    {
        return strerror(err);
    }

    static bool IsWouldBlock(int err)
    {
        return err == EAGAIN || err == EWOULDBLOCK;
    }
#endif

    TCPSocket::TCPSocket() = default;

    TCPSocket::~TCPSocket()
    {
        Close();
    }

    bool TCPSocket::Listen(uint16_t port, int backlog)
    {
#ifdef _WIN32
        if (!EnsureWSAInitialized())
        {
            CQ_CORE_ERROR("[TCP] WSAStartup failed");
            return false;
        }
        SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET)
        {
            CQ_CORE_ERROR("[TCP] Failed to create socket: {0}", SocketErrorString(GetLastSocketError()));
            return false;
        }
        m_Socket = static_cast<int>(sock);
#else
        m_Socket = socket(AF_INET, SOCK_STREAM, 0);
        if (m_Socket < 0)
        {
            CQ_CORE_ERROR("[TCP] Failed to create socket: {0}", SocketErrorString(GetLastSocketError()));
            return false;
        }
#endif

        // Allow port reuse
        int opt = 1;
        setsockopt(m_Socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (bind(m_Socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        {
            CQ_CORE_ERROR("[TCP] Failed to bind port {0}: {1}", port, SocketErrorString(GetLastSocketError()));
            Close();
            return false;
        }

        if (listen(m_Socket, backlog) < 0)
        {
            CQ_CORE_ERROR("[TCP] Failed to listen: {0}", SocketErrorString(GetLastSocketError()));
            Close();
            return false;
        }

        CQ_CORE_INFO("[TCP] Listening on port {0}", port);
        return true;
    }

    int TCPSocket::Accept()
    {
        sockaddr_in clientAddr{};
#ifdef _WIN32
        int addrLen = sizeof(clientAddr);
        SOCKET clientSock = accept(static_cast<SOCKET>(m_Socket), reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
        if (clientSock == INVALID_SOCKET)
            return -1;
        int clientFd = static_cast<int>(clientSock);
#else
        socklen_t addrLen = sizeof(clientAddr);
        int clientFd = accept(m_Socket, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
#endif
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
#ifdef _WIN32
        if (!EnsureWSAInitialized())
        {
            CQ_CORE_ERROR("[TCP] WSAStartup failed");
            return false;
        }
        SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET)
        {
            CQ_CORE_ERROR("[TCP] Failed to create socket: {0}", SocketErrorString(GetLastSocketError()));
            return false;
        }
        m_Socket = static_cast<int>(sock);
#else
        m_Socket = socket(AF_INET, SOCK_STREAM, 0);
        if (m_Socket < 0)
        {
            CQ_CORE_ERROR("[TCP] Failed to create socket: {0}", SocketErrorString(GetLastSocketError()));
            return false;
        }
#endif

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

        if (connect(m_Socket, result->ai_addr, static_cast<int>(result->ai_addrlen)) < 0)
        {
            CQ_CORE_ERROR("[TCP] Failed to connect to {0}:{1}: {2}", host, port, SocketErrorString(GetLastSocketError()));
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
#ifdef _WIN32
            int chunk = static_cast<int>((size - totalSent) > INT_MAX ? INT_MAX : (size - totalSent));
            int sent = send(static_cast<SOCKET>(fd), reinterpret_cast<const char*>(data + totalSent), chunk, MSG_NOSIGNAL);
            if (sent == SOCKET_ERROR)
            {
                int err = GetLastSocketError();
                if (IsWouldBlock(err)) continue;
                CQ_CORE_ERROR("[TCP] Send failed: {0}", SocketErrorString(err));
                return false;
            }
#else
            ssize_t sent = send(fd, data + totalSent, size - totalSent, MSG_NOSIGNAL);
            if (sent <= 0)
            {
                if (IsWouldBlock(GetLastSocketError())) continue;
                CQ_CORE_ERROR("[TCP] Send failed: {0}", SocketErrorString(GetLastSocketError()));
                return false;
            }
#endif
            totalSent += sent;
        }
        return true;
    }

    int TCPSocket::Receive(uint8_t* buffer, size_t bufferSize, int socketFd)
    {
        int fd = (socketFd >= 0) ? socketFd : m_Socket;
        if (fd < 0) return -1;

#ifdef _WIN32
        int len = static_cast<int>(bufferSize > INT_MAX ? INT_MAX : bufferSize);
        int received = recv(static_cast<SOCKET>(fd), reinterpret_cast<char*>(buffer), len, 0);
        if (received == SOCKET_ERROR)
        {
            int err = GetLastSocketError();
            if (IsWouldBlock(err)) return 0;
            return -1;
        }
        return received;
#else
        ssize_t received = recv(fd, buffer, bufferSize, 0);
        if (received < 0)
        {
            if (IsWouldBlock(GetLastSocketError())) return 0;
            return -1;
        }
        return static_cast<int>(received);
#endif
    }

    void TCPSocket::Close()
    {
        if (m_Socket >= 0)
        {
            CloseSocketFd(m_Socket);
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
#ifdef _WIN32
        u_long mode = nonBlocking ? 1UL : 0UL;
        ioctlsocket(static_cast<SOCKET>(fd), FIONBIO, &mode);
#else
        int flags = fcntl(fd, F_GETFL, 0);
        if (nonBlocking)
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        else
            fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
#endif
    }
}
