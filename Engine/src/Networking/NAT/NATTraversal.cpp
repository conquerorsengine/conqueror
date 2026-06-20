#include "NATTraversal.h"
#include "Core/Logging/Log.h"

#include <miniupnpc.h>
#include <upnpcommands.h>
#include <upnperrors.h>

#include <cstring>
#include <cstdlib>

namespace Conqueror
{
    NATTraversal::NATTraversal() = default;

    NATTraversal::~NATTraversal()
    {
        if (m_UPnPUrls)
        {
            FreeUPNPUrls(static_cast<UPNPUrls*>(m_UPnPUrls));
            free(m_UPnPUrls);
            m_UPnPUrls = nullptr;
        }
        if (m_IGDData)
        {
            free(m_IGDData);
            m_IGDData = nullptr;
        }
    }

    bool NATTraversal::Discover()
    {
        CQ_CORE_INFO("[NAT] Discovering UPnP devices...");

        int error = 0;
        UPNPDev* devList = upnpDiscover(2000, nullptr, nullptr, 0, 0, 2, &error);
        if (!devList)
        {
            CQ_CORE_WARN("[NAT] No UPnP devices found (error: {0})", error);
            m_Available = false;
            return false;
        }

        auto* urls = static_cast<UPNPUrls*>(malloc(sizeof(UPNPUrls)));
        auto* data = static_cast<IGDdatas*>(malloc(sizeof(IGDdatas)));
        memset(urls, 0, sizeof(UPNPUrls));
        memset(data, 0, sizeof(IGDdatas));

        char wanAddress[64] = {};
        int status = UPNP_GetValidIGD(devList, urls, data, m_LanAddress, sizeof(m_LanAddress), wanAddress, sizeof(wanAddress));
        freeUPNPDevlist(devList);

        if (status == 0)
        {
            CQ_CORE_WARN("[NAT] No valid IGD found.");
            free(urls);
            free(data);
            m_Available = false;
            return false;
        }

        m_UPnPUrls = urls;
        m_IGDData = data;
        m_Available = true;

        CQ_CORE_INFO("[NAT] UPnP discovered. LAN IP: {0}, IGD status: {1}", m_LanAddress, status);
        return true;
    }

    bool NATTraversal::OpenPort(uint16_t externalPort, uint16_t internalPort,
                                 const std::string& protocol, const std::string& description)
    {
        if (!m_Available)
        {
            CQ_CORE_WARN("[NAT] UPnP not available. Call Discover() first.");
            return false;
        }

        auto* urls = static_cast<UPNPUrls*>(m_UPnPUrls);
        auto* data = static_cast<IGDdatas*>(m_IGDData);

        std::string extPortStr = std::to_string(externalPort);
        std::string intPortStr = std::to_string(internalPort);

        int ret = UPNP_AddPortMapping(
            urls->controlURL,
            data->first.servicetype,
            extPortStr.c_str(),
            intPortStr.c_str(),
            m_LanAddress,
            description.c_str(),
            protocol.c_str(),
            nullptr,  // Remote host (any)
            "0"       // Lease duration (0 = permanent until removal)
        );

        if (ret != 0)
        {
            CQ_CORE_ERROR("[NAT] Failed to open port {0} ({1}): {2}",
                           externalPort, protocol, strupnperror(ret));
            return false;
        }

        CQ_CORE_INFO("[NAT] Port opened: {0}:{1} -> {2}:{3} ({4})",
                      GetExternalIP(), externalPort, m_LanAddress, internalPort, protocol);
        return true;
    }

    bool NATTraversal::ClosePort(uint16_t externalPort, const std::string& protocol)
    {
        if (!m_Available) return false;

        auto* urls = static_cast<UPNPUrls*>(m_UPnPUrls);
        auto* data = static_cast<IGDdatas*>(m_IGDData);

        std::string portStr = std::to_string(externalPort);

        int ret = UPNP_DeletePortMapping(
            urls->controlURL,
            data->first.servicetype,
            portStr.c_str(),
            protocol.c_str(),
            nullptr
        );

        if (ret != 0)
        {
            CQ_CORE_WARN("[NAT] Failed to close port {0}: {1}", externalPort, strupnperror(ret));
            return false;
        }

        CQ_CORE_INFO("[NAT] Port {0} ({1}) closed.", externalPort, protocol);
        return true;
    }

    std::string NATTraversal::GetExternalIP() const
    {
        if (!m_Available) return "unknown";

        auto* urls = static_cast<UPNPUrls*>(m_UPnPUrls);
        auto* data = static_cast<IGDdatas*>(m_IGDData);

        char externalIP[64] = {};
        UPNP_GetExternalIPAddress(urls->controlURL, data->first.servicetype, externalIP);
        return std::string(externalIP);
    }

    std::string NATTraversal::GetInternalIP() const
    {
        return std::string(m_LanAddress);
    }
}
