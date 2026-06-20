#pragma once

#include <string>
#include <cstdint>
#include <functional>

namespace Conqueror
{
    /// NAT Traversal using miniupnpc.
    /// Automatically opens ports on the user's router via UPnP protocol
    /// so that external players can connect to a hosted server.
    class NATTraversal
    {
    public:
        NATTraversal();
        ~NATTraversal();

        NATTraversal(const NATTraversal&) = delete;
        NATTraversal& operator=(const NATTraversal&) = delete;

        /// Discover UPnP devices on the local network
        bool Discover();

        /// Open a port on the router. Protocol: "UDP" or "TCP"
        bool OpenPort(uint16_t externalPort, uint16_t internalPort, const std::string& protocol = "UDP",
                      const std::string& description = "Conqueror Engine");

        /// Close a previously opened port
        bool ClosePort(uint16_t externalPort, const std::string& protocol = "UDP");

        /// Get the external (public) IP address from the router
        std::string GetExternalIP() const;

        /// Get the internal (LAN) IP address
        std::string GetInternalIP() const;

        /// Check if UPnP is available on this network
        bool IsAvailable() const { return m_Available; }

    private:
        bool  m_Available = false;
        void* m_UPnPUrls = nullptr;      // UPNPUrls*
        void* m_IGDData = nullptr;        // IGDdatas*
        char  m_LanAddress[64] = {};
    };
}
