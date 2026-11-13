#ifndef CPPHTTPLIB_HTTPLIB_H
#define CPPHTTPLIB_HTTPLIB_H

/**
 * Single-Header C++17 HTTP/1.1 Server (cpp-httplib compatible interface)
 * Supports Get(), Post(), Delete(), Options(), regex path captures (req.matches),
 * query parameters (req.has_param / req.get_param_value), headers, cookies,
 * static file serving, and concurrent multi-threaded request handling.
 */

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#ifndef _WIN32
#include <thread>
#endif
#include <vector>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #ifdef _MSC_VER
    #pragma comment(lib, "ws2_32.lib")
  #endif
  using socket_t = SOCKET;
  constexpr socket_t kInvalidSocket = INVALID_SOCKET;
  inline void closeSocket(socket_t s) { closesocket(s); }
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
  #include <ifaddrs.h>
  using socket_t = int;
  constexpr socket_t kInvalidSocket = -1;
  inline void closeSocket(socket_t s) { close(s); }
#endif

namespace httplib {

struct Request {
    std::string method;
    std::string path;
    std::string raw_target;
    std::string body;
    std::map<std::string, std::string> headers;
    std::map<std::string, std::string> params;
    std::smatch matches;

    bool has_header(const std::string& key) const {
        std::string lower = toLower(key);
        return headers.find(lower) != headers.end();
    }

    std::string get_header_value(const std::string& key) const {
        std::string lower = toLower(key);
        auto it = headers.find(lower);
        return (it != headers.end()) ? it->second : "";
    }

    bool has_param(const std::string& key) const {
        return params.find(key) != params.end();
    }

    std::string get_param_value(const std::string& key) const {
        auto it = params.find(key);
        return (it != params.end()) ? it->second : "";
    }

    static std::string toLower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }
};

struct Response {
    int status = 200;
    std::string body;
    std::string content_type = "text/plain; charset=utf-8";
    std::map<std::string, std::string> headers;

    void set_content(std::string data, std::string type) {
        body = std::move(data);
        content_type = std::move(type);
    }

    void set_header(const std::string& key, const std::string& val) {
        headers[key] = val;
    }
};

class Server {
public:
    using Handler = std::function<void(const Request&, Response&)>;

private:
    struct Route {
        std::string method;
        std::regex pattern;
        Handler handler;
    };

    std::vector<Route> routes_;
    std::string mount_point_;
    std::string base_dir_;
    std::atomic<bool> running_{false};
    socket_t listen_sock_ = kInvalidSocket;

public:
    Server() = default;
    ~Server() { stop(); }

    Server& Get(const std::string& pattern, Handler h) {
        routes_.push_back({"GET", std::regex("^" + pattern + "$"), std::move(h)});
        return *this;
    }

    Server& Post(const std::string& pattern, Handler h) {
        routes_.push_back({"POST", std::regex("^" + pattern + "$"), std::move(h)});
        return *this;
    }

    Server& Delete(const std::string& pattern, Handler h) {
        routes_.push_back({"DELETE", std::regex("^" + pattern + "$"), std::move(h)});
        return *this;
    }

    Server& Options(const std::string& pattern, Handler h) {
        routes_.push_back({"OPTIONS", std::regex("^" + pattern + "$"), std::move(h)});
        return *this;
    }

    bool set_mount_point(const std::string& mount_point, const std::string& dir) {
        mount_point_ = mount_point;
        base_dir_ = dir;
        return true;
    }

    void stop() {
        running_ = false;
        if (listen_sock_ != kInvalidSocket) {
            closeSocket(listen_sock_);
            listen_sock_ = kInvalidSocket;
        }
    }

    static std::string detectLanIp() {
#ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0) {
            char host[256] = {0};
            if (gethostname(host, sizeof(host)) == 0) {
                struct hostent* phe = gethostbyname(host);
                if (phe != nullptr) {
                    for (int i = 0; phe->h_addr_list[i] != nullptr; ++i) {
                        struct in_addr addr;
                        std::memcpy(&addr, phe->h_addr_list[i], sizeof(struct in_addr));
                        std::string candidate(inet_ntoa(addr));
                        if (candidate != "127.0.0.1" && candidate.rfind("169.254.", 0) != 0) {
                            return candidate;
                        }
                    }
                }
            }
        }
        return "127.0.0.1";
#else
        struct ifaddrs* ifaddr = nullptr;
        if (getifaddrs(&ifaddr) == 0 && ifaddr != nullptr) {
            std::string found = "127.0.0.1";
            for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
                if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
                char buf[INET_ADDRSTRLEN] = {0};
                auto* sin = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
                inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf));
                std::string ip(buf);
                if (ip != "127.0.0.1" && !ip.empty()) {
                    found = ip;
                    break;
                }
            }
            freeifaddrs(ifaddr);
            return found;
        }
        return "127.0.0.1";
#endif
    }

    bool listen(const std::string& host, int port) {
#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
        listen_sock_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listen_sock_ == kInvalidSocket) {
            return false;
        }

        int opt = 1;
#ifdef _WIN32
        setsockopt(listen_sock_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
        setsockopt(listen_sock_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(port));
        if (host == "0.0.0.0" || host.empty()) {
            addr.sin_addr.s_addr = INADDR_ANY;
        } else {
#ifdef _WIN32
            addr.sin_addr.s_addr = inet_addr(host.c_str());
#else
            inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
#endif
        }

        if (::bind(listen_sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            closeSocket(listen_sock_);
            listen_sock_ = kInvalidSocket;
            return false;
        }

        if (::listen(listen_sock_, 64) < 0) {
            closeSocket(listen_sock_);
            listen_sock_ = kInvalidSocket;
            return false;
        }

        running_ = true;
        while (running_) {
            sockaddr_in clientAddr{};
#ifdef _WIN32
            int clientLen = sizeof(clientAddr);
#else
            socklen_t clientLen = sizeof(clientAddr);
#endif
            socket_t clientSock = ::accept(listen_sock_, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
            if (clientSock == kInvalidSocket) {
                if (!running_) break;
                continue;
            }
#ifdef _WIN32
            struct ClientArgs {
                Server* srv;
                socket_t sock;
                static DWORD WINAPI entry(LPVOID p) {
                    ClientArgs* a = static_cast<ClientArgs*>(p);
                    a->srv->handleClient(a->sock);
                    delete a;
                    return 0;
                }
            };
            ClientArgs* cargs = new ClientArgs{this, clientSock};
            HANDLE hThread = CreateThread(nullptr, 0, &ClientArgs::entry, cargs, 0, nullptr);
            if (hThread) CloseHandle(hThread);
            else { closeSocket(clientSock); delete cargs; }
#else
            std::thread(&Server::handleClient, this, clientSock).detach();
#endif
        }
        return true;
    }

private:
    static std::string urlDecode(const std::string& str) {
        std::string out;
        out.reserve(str.size());
        for (size_t i = 0; i < str.size(); ++i) {
            if (str[i] == '%' && i + 2 < str.size()) {
                int hexVal = 0;
                std::istringstream iss(str.substr(i + 1, 2));
                if (iss >> std::hex >> hexVal) {
                    out.push_back(static_cast<char>(hexVal));
                    i += 2;
                    continue;
                }
            } else if (str[i] == '+') {
                out.push_back(' ');
                continue;
            }
            out.push_back(str[i]);
        }
        return out;
    }

    static void parseQueryString(const std::string& qs, std::map<std::string, std::string>& params) {
        std::stringstream ss(qs);
        std::string pair;
        while (std::getline(ss, pair, '&')) {
            if (pair.empty()) continue;
            auto eq = pair.find('=');
            if (eq == std::string::npos) {
                params[urlDecode(pair)] = "";
            } else {
                params[urlDecode(pair.substr(0, eq))] = urlDecode(pair.substr(eq + 1));
            }
        }
    }

    static std::string statusText(int code) {
        switch (code) {
            case 200: return "OK";
            case 201: return "Created";
            case 204: return "No Content";
            case 400: return "Bad Request";
            case 401: return "Unauthorized";
            case 403: return "Forbidden";
            case 404: return "Not Found";
            case 409: return "Conflict";
            case 500: return "Internal Server Error";
            default:  return "OK";
        }
    }

    static std::string guessMimeType(const std::string& path) {
        auto dot = path.rfind('.');
        std::string ext = (dot != std::string::npos) ? path.substr(dot) : "";
        if (ext == ".html") return "text/html; charset=utf-8";
        if (ext == ".css")  return "text/css; charset=utf-8";
        if (ext == ".js")   return "application/javascript; charset=utf-8";
        if (ext == ".json") return "application/json; charset=utf-8";
        if (ext == ".svg")  return "image/svg+xml";
        if (ext == ".png")  return "image/png";
        if (ext == ".ico")  return "image/x-icon";
        return "text/plain; charset=utf-8";
    }

    bool tryServeStatic(const std::string& reqPath, Response& res) const {
        if (base_dir_.empty()) return false;
        std::string rel = reqPath;
        if (rel == "/" || rel.empty()) rel = "/index.html";
        else if (rel == "/login")   rel = "/index.html";
        else if (rel == "/student") rel = "/student.html";
        else if (rel == "/admin")   rel = "/admin.html";

        // Prevent directory traversal
        if (rel.find("..") != std::string::npos) return false;

        std::string fullPath = base_dir_ + rel;
        std::ifstream in(fullPath, std::ios::in | std::ios::binary);
        if (!in.is_open()) return false;

        std::ostringstream ss;
        ss << in.rdbuf();
        res.status = 200;
        res.set_content(ss.str(), guessMimeType(fullPath));
        return true;
    }

    void handleClient(socket_t sock) {
        std::string raw;
        char buf[4096];
        size_t headerEnd = std::string::npos;
        size_t contentLength = 0;

        while (true) {
#ifdef _WIN32
            int n = ::recv(sock, buf, sizeof(buf), 0);
#else
            ssize_t n = ::recv(sock, buf, sizeof(buf), 0);
#endif
            if (n <= 0) break;
            raw.append(buf, static_cast<size_t>(n));
            if (headerEnd == std::string::npos) {
                headerEnd = raw.find("\r\n\r\n");
                if (headerEnd != std::string::npos) {
                    // Parse Content-Length if present
                    std::string hdrBlock = raw.substr(0, headerEnd);
                    std::istringstream iss(hdrBlock);
                    std::string line;
                    while (std::getline(iss, line)) {
                        if (!line.empty() && line.back() == '\r') line.pop_back();
                        auto colon = line.find(':');
                        if (colon != std::string::npos) {
                            std::string k = Request::toLower(line.substr(0, colon));
                            std::string v = line.substr(colon + 1);
                            while (!v.empty() && v.front() == ' ') v.erase(v.begin());
                            if (k == "content-length") {
                                try { contentLength = static_cast<size_t>(std::stoul(v)); } catch (...) {}
                            }
                        }
                    }
                }
            }
            if (headerEnd != std::string::npos) {
                size_t bodyRecvd = raw.size() - (headerEnd + 4);
                if (bodyRecvd >= contentLength) break;
            }
            if (raw.size() > 2 * 1024 * 1024) break;
        }

        if (headerEnd == std::string::npos) {
            closeSocket(sock);
            return;
        }

        Request req;
        std::istringstream hss(raw.substr(0, headerEnd));
        std::string reqLine;
        std::getline(hss, reqLine);
        if (!reqLine.empty() && reqLine.back() == '\r') reqLine.pop_back();

        {
            std::istringstream rlss(reqLine);
            rlss >> req.method >> req.raw_target;
        }

        auto qPos = req.raw_target.find('?');
        if (qPos != std::string::npos) {
            req.path = req.raw_target.substr(0, qPos);
            parseQueryString(req.raw_target.substr(qPos + 1), req.params);
        } else {
            req.path = req.raw_target;
        }

        std::string hLine;
        while (std::getline(hss, hLine)) {
            if (!hLine.empty() && hLine.back() == '\r') hLine.pop_back();
            auto colon = hLine.find(':');
            if (colon != std::string::npos) {
                std::string k = Request::toLower(hLine.substr(0, colon));
                std::string v = hLine.substr(colon + 1);
                while (!v.empty() && v.front() == ' ') v.erase(v.begin());
                req.headers[k] = v;
            }
        }
        req.body = raw.substr(headerEnd + 4, contentLength);

        Response res;
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Session-Token");

        if (req.method == "OPTIONS") {
            res.status = 204;
        } else {
            bool matched = false;
            for (const auto& route : routes_) {
                if (route.method != req.method) continue;
                std::smatch m;
                if (std::regex_match(req.path, m, route.pattern)) {
                    req.matches = m;
                    try {
                        route.handler(req, res);
                    } catch (const std::exception& ex) {
                        res.status = 500;
                        res.set_content(std::string("{\"ok\":false,\"error\":\"Internal server error\"}"),
                                        "application/json");
                    }
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                if (req.method == "GET" && tryServeStatic(req.path, res)) {
                    matched = true;
                } else {
                    res.status = 404;
                    res.set_content("{\"ok\":false,\"error\":\"Route not found\",\"rule\":\"HTTP_404\"}",
                                    "application/json");
                }
            }
        }

        std::ostringstream respStream;
        respStream << "HTTP/1.1 " << res.status << " " << statusText(res.status) << "\r\n";
        respStream << "Content-Type: " << res.content_type << "\r\n";
        respStream << "Content-Length: " << res.body.size() << "\r\n";
        respStream << "Connection: close\r\n";
        for (const auto& kv : res.headers) {
            respStream << kv.first << ": " << kv.second << "\r\n";
        }
        respStream << "\r\n";
        respStream << res.body;

        std::string outBytes = respStream.str();
        size_t totalSent = 0;
        while (totalSent < outBytes.size()) {
#ifdef _WIN32
            int sent = ::send(sock, outBytes.data() + totalSent,
                              static_cast<int>(outBytes.size() - totalSent), 0);
#else
            ssize_t sent = ::send(sock, outBytes.data() + totalSent,
                                  outBytes.size() - totalSent, 0);
#endif
            if (sent <= 0) break;
            totalSent += static_cast<size_t>(sent);
        }
        closeSocket(sock);
    }
};

} // namespace httplib

#endif // CPPHTTPLIB_HTTPLIB_H
