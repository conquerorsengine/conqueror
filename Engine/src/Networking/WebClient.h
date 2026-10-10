#pragma once

#include "Core/Base/Base.h"
#include <string>
#include <unordered_map>

namespace Conqueror::Networking {

    struct HttpResponse {
        int Status = 0;
        std::string Body;
    };

    class CQ_API WebClient {
    public:
        // Plain HTTP (port 80)
        static HttpResponse HttpRequest(const std::string& url, const std::string& method = "GET", const std::string& body = "");

        // HTTPS-capable request with custom headers (uses libcurl)
        static HttpResponse HttpsRequest(const std::string& url,
                                         const std::string& method,
                                         const std::string& body,
                                         const std::unordered_map<std::string, std::string>& headers);

        // Streaming HTTPS request (calls callback line-by-line in real time)
        static HttpResponse HttpsStreamRequest(const std::string& url,
                                               const std::string& method,
                                               const std::string& body,
                                               const std::unordered_map<std::string, std::string>& headers,
                                               std::function<void(const std::string& line)> onLineReceived);

        // WebSocket client handles
        static int WebSocketConnect(const std::string& url);
        static bool WebSocketSend(int wsId, const std::string& message);
        static std::string WebSocketReceive(int wsId);
        static void WebSocketDisconnect(int wsId);
    };

}
