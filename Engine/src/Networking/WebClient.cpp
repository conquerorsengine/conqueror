#include "WebClient.h"
#include "TCPSocket.h"
#include "Core/Utils/URLUtils.h"
#include "Core/Utils/Crypto.h"
#include "Core/Logging/Log.h"
#include <unordered_map>
#include <vector>
#include <sstream>

// Windows uyumluluğu (Linux kodu aynen durur): MSVC CRT'de popen/pclose yoktur,
// _popen/_pclose aynı imzayla vardır.
#ifdef _WIN32
#define CQ_POPEN(cmd, mode) _popen(cmd, mode)
#define CQ_PCLOSE(stream) _pclose(stream)
#else
#define CQ_POPEN(cmd, mode) popen(cmd, mode)
#define CQ_PCLOSE(stream) pclose(stream)
#endif


namespace Conqueror::Networking {

    static std::unordered_map<int, TCPSocket*> s_WebSockets;
    static int s_NextWsID = 1;

    HttpResponse WebClient::HttpRequest(const std::string& url, const std::string& method, const std::string& body) {
        HttpResponse response;
        std::string host = URLUtils::GetHost(url);
        std::string path = URLUtils::GetPath(url);
        if (path.empty()) path = "/";
        
        TCPSocket socket;
        // Connect to port 80 (HTTPS not supported in this simple client yet)
        if (!socket.Connect(host, 80)) {
            response.Status = 0;
            return response;
        }
        
        std::string req = method + " " + path + " HTTP/1.1\r\n";
        req += "Host: " + host + "\r\n";
        req += "Connection: close\r\n";
        if (!body.empty()) {
            req += "Content-Length: " + std::to_string(body.length()) + "\r\n";
        }
        req += "\r\n";
        req += body;
        
        socket.Send((const uint8_t*)req.data(), req.size());
        
        std::string fullRes;
        uint8_t buf[4096];
        while (true) {
            int r = socket.Receive(buf, sizeof(buf));
            if (r <= 0) break;
            fullRes.append((char*)buf, r);
        }
        
        // Parse HTTP
        size_t headerEnd = fullRes.find("\r\n\r\n");
        if (headerEnd != std::string::npos) {
            response.Body = fullRes.substr(headerEnd + 4);
            size_t space1 = fullRes.find(' ');
            if (space1 != std::string::npos) {
                size_t space2 = fullRes.find(' ', space1 + 1);
                if (space2 != std::string::npos) {
                    try {
                        response.Status = std::stoi(fullRes.substr(space1 + 1, space2 - space1 - 1));
                    } catch (...) {}
                }
            }
        }
        
        socket.Close();
        return response;
    }

    int WebClient::WebSocketConnect(const std::string& url) {
        std::string host = URLUtils::GetHost(url);
        std::string path = URLUtils::GetPath(url);
        if (path.empty()) path = "/";
        
        TCPSocket* socket = new TCPSocket();
        if (!socket->Connect(host, 80)) {
            delete socket;
            return -1;
        }
        
        // WebSocket Handshake
        std::string key = "dGhlIHNhbXBsZSBub25jZQ=="; // mock key
        std::string req = "GET " + path + " HTTP/1.1\r\n";
        req += "Host: " + host + "\r\n";
        req += "Upgrade: websocket\r\n";
        req += "Connection: Upgrade\r\n";
        req += "Sec-WebSocket-Key: " + key + "\r\n";
        req += "Sec-WebSocket-Version: 13\r\n\r\n";
        
        socket->Send((const uint8_t*)req.data(), req.size());
        
        uint8_t buf[4096];
        int r = socket->Receive(buf, sizeof(buf));
        if (r <= 0) {
            delete socket;
            return -1;
        }
        
        std::string res((char*)buf, r);
        if (res.find("101 Switching Protocols") == std::string::npos) {
            delete socket;
            return -1;
        }
        
        int id = s_NextWsID++;
        s_WebSockets[id] = socket;
        return id;
    }

    bool WebClient::WebSocketSend(int wsId, const std::string& message) {
        if (s_WebSockets.find(wsId) == s_WebSockets.end()) return false;
        TCPSocket* socket = s_WebSockets[wsId];
        
        // Frame: FIN=1, Opcode=1 (text)
        std::vector<uint8_t> frame;
        frame.push_back(0x81); 
        
        size_t len = message.length();
        if (len <= 125) {
            frame.push_back((uint8_t)(len | 0x80)); // Mask bit = 1
        } else if (len <= 65535) {
            frame.push_back(126 | 0x80);
            frame.push_back((len >> 8) & 0xFF);
            frame.push_back(len & 0xFF);
        } else {
            return false; // too large for this simple client
        }
        
        // Masking key
        uint8_t mask[4] = { 0x12, 0x34, 0x56, 0x78 };
        frame.push_back(mask[0]); frame.push_back(mask[1]);
        frame.push_back(mask[2]); frame.push_back(mask[3]);
        
        for (size_t i = 0; i < len; ++i) {
            frame.push_back(message[i] ^ mask[i % 4]);
        }
        
        return socket->Send(frame.data(), frame.size());
    }

    std::string WebClient::WebSocketReceive(int wsId) {
        if (s_WebSockets.find(wsId) == s_WebSockets.end()) return "";
        TCPSocket* socket = s_WebSockets[wsId];
        
        // Very simple unmasked frame reading (servers don't mask)
        uint8_t header[2];
        int r = socket->Receive(header, 2);
        if (r < 2) return "";
        
        int len = header[1] & 0x7F;
        if (len == 126) {
            uint8_t ext[2];
            socket->Receive(ext, 2);
            len = (ext[0] << 8) | ext[1];
        } else if (len == 127) {
            return ""; // Not supported
        }
        
        if (len == 0) return "";
        
        std::vector<uint8_t> payload(len);
        int total = 0;
        while (total < len) {
            r = socket->Receive(payload.data() + total, len - total);
            if (r <= 0) return "";
            total += r;
        }
        
        return std::string(payload.begin(), payload.end());
    }
    
    void WebClient::WebSocketDisconnect(int wsId) {
        if (s_WebSockets.find(wsId) != s_WebSockets.end()) {
            s_WebSockets[wsId]->Close();
            delete s_WebSockets[wsId];
            s_WebSockets.erase(wsId);
        }
    }

    HttpResponse WebClient::HttpsRequest(const std::string& url,
                                          const std::string& method,
                                          const std::string& body,
                                          const std::unordered_map<std::string, std::string>& headers)
    {
        HttpResponse response;
        response.Status = 0;

        // Construct curl command
        std::string cmd = "curl -s -w \"\\n%{http_code}\" -X " + method;

        for (const auto& [key, value] : headers)
        {
            // Escape double quotes in header values
            std::string val = value;
            size_t pos = 0;
            while ((pos = val.find('"', pos)) != std::string::npos) {
                val.replace(pos, 1, "\\\"");
                pos += 2;
            }
            cmd += " -H \"" + key + ": " + val + "\"";
        }

        if (!body.empty())
        {
            // Escape body for single quote bash shell passing or use temporary data
            std::string escapedBody = body;
            size_t pos = 0;
            while ((pos = escapedBody.find('\'', pos)) != std::string::npos) {
                escapedBody.replace(pos, 1, "'\\''");
                pos += 4;
            }
            cmd += " -d '" + escapedBody + "'";
        }

        cmd += " '" + url + "'";

        FILE* pipe = CQ_POPEN(cmd.c_str(), "r");
        if (!pipe)
        {
            response.Body = "Failed to run curl binary";
            return response;
        }

        char buffer[4096];
        std::string rawOutput = "";
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
        {
            rawOutput += buffer;
        }
        CQ_PCLOSE(pipe);

        // Extract HTTP status code from the last line (added by -w "\n%{http_code}")
        size_t lastNewline = rawOutput.rfind('\n');
        if (lastNewline != std::string::npos && lastNewline + 1 < rawOutput.size())
        {
            std::string codeStr = rawOutput.substr(lastNewline + 1);
            // Trim whitespace/newlines
            codeStr.erase(codeStr.find_last_not_of(" \n\r\t") + 1);
            try {
                response.Status = std::stoi(codeStr);
            } catch (...) {}

            response.Body = rawOutput.substr(0, lastNewline);
        }
        else
        {
            response.Body = rawOutput;
        }

        return response;
    }

    HttpResponse WebClient::HttpsStreamRequest(const std::string& url,
                                                 const std::string& method,
                                                 const std::string& body,
                                                 const std::unordered_map<std::string, std::string>& headers,
                                                 std::function<void(const std::string& line)> onLineReceived)
    {
        HttpResponse response;
        response.Status = 200;

        // Use curl -N (no buffer) and -s (silent) to get real-time stream lines
        std::string cmd = "curl -N -s -X " + method;

        for (const auto& [key, value] : headers)
        {
            std::string val = value;
            size_t pos = 0;
            while ((pos = val.find('"', pos)) != std::string::npos) {
                val.replace(pos, 1, "\\\"");
                pos += 2;
            }
            cmd += " -H \"" + key + ": " + val + "\"";
        }

        if (!body.empty())
        {
            std::string escapedBody = body;
            size_t pos = 0;
            while ((pos = escapedBody.find('\'', pos)) != std::string::npos) {
                escapedBody.replace(pos, 1, "'\\''");
                pos += 4;
            }
            cmd += " -d '" + escapedBody + "'";
        }

        cmd += " '" + url + "'";

        FILE* pipe = CQ_POPEN(cmd.c_str(), "r");
        if (!pipe)
        {
            response.Status = 0;
            response.Body = "Failed to run curl binary";
            return response;
        }

        char buffer[1024];
        std::string currentLine = "";
        std::string fullBody = "";

        while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
        {
            std::string chunk(buffer);
            fullBody += chunk;
            currentLine += chunk;

            size_t nlPos;
            while ((nlPos = currentLine.find('\n')) != std::string::npos)
            {
                std::string line = currentLine.substr(0, nlPos);
                currentLine.erase(0, nlPos + 1);

                if (!line.empty() && line.back() == '\r')
                    line.pop_back();

                if (onLineReceived)
                {
                    onLineReceived(line);
                }
            }
        }
        CQ_PCLOSE(pipe);

        if (!currentLine.empty() && onLineReceived)
        {
            onLineReceived(currentLine);
        }

        response.Body = fullBody;
        return response;
    }

}
