#pragma once

#include "Core/Base/Base.h"
#include <string>

namespace Conqueror::Networking {

    struct HttpResponse {
        int Status = 0;
        std::string Body;
    };

    class CQ_API WebClient {
    public:
        // Basic HTTP GET
        static HttpResponse HttpRequest(const std::string& url, const std::string& method = "GET", const std::string& body = "");
        
        // WebSocket client handles
        static int WebSocketConnect(const std::string& url);
        static bool WebSocketSend(int wsId, const std::string& message);
        static std::string WebSocketReceive(int wsId);
        static void WebSocketDisconnect(int wsId);
    };

}
