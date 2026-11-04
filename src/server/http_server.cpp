#include "server/http_server.hpp"
#include <iostream>
#include <sstream>
#include <fstream>
#include <cstring>
#include <csignal>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace riskdb {

HttpServer::HttpServer(int port)
    : port_(port) {}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::route(const std::string& method, const std::string& path, HttpHandler handler) {
    routes_[{method, path}] = handler;
}

bool HttpServer::start() {
    if (running_) return true;

    signal(SIGPIPE, SIG_IGN);

    server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_ < 0) {
        std::cerr << "[-] Error creating socket\n";
        return false;
    }

    int opt = 1;
    setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
    setsockopt(server_socket_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(server_socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[-] Error binding socket to port " << port_ << "\n";
        close(server_socket_);
        server_socket_ = -1;
        return false;
    }

    if (listen(server_socket_, 128) < 0) {
        std::cerr << "[-] Error listening on socket\n";
        close(server_socket_);
        server_socket_ = -1;
        return false;
    }

    running_ = true;
    accept_thread_ = std::thread(&HttpServer::accept_loop, this);
    std::cout << "[+] HTTP Server listening on http://0.0.0.0:" << port_ << "\n";
    return true;
}

void HttpServer::stop() {
    if (!running_) return;
    running_ = false;

    if (server_socket_ >= 0) {
        // Shutdown socket to unblock accept()
        shutdown(server_socket_, SHUT_RDWR);
        close(server_socket_);
        server_socket_ = -1;
    }

    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
}

void HttpServer::accept_loop() {
    while (running_) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(server_socket_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);

        if (client_sock < 0) {
            if (!running_) break;
            continue;
        }

        // Spawn a thread or handle client synchronously (short lived requests)
        std::thread([this, client_sock]() {
            try {
                handle_client(client_sock);
            } catch (const std::exception& e) {
                std::cerr << "[-] Exception in client thread: " << e.what() << "\n";
            } catch (...) {
                std::cerr << "[-] Unknown exception in client thread\n";
            }
        }).detach();
    }
}

HttpRequest HttpServer::parse_request(const std::string& raw) {
    HttpRequest req;
    std::istringstream stream(raw);
    std::string line;

    if (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream line_stream(line);
        std::string full_path;
        line_stream >> req.method >> full_path;

        // Parse path and query parameters
        size_t qpos = full_path.find('?');
        if (qpos != std::string::npos) {
            req.path = full_path.substr(0, qpos);
            req.query_string = full_path.substr(qpos + 1);

            std::istringstream qstream(req.query_string);
            std::string pair;
            while (std::getline(qstream, pair, '&')) {
                size_t eq_pos = pair.find('=');
                if (eq_pos != std::string::npos) {
                    req.query_params[pair.substr(0, eq_pos)] = pair.substr(eq_pos + 1);
                } else if (!pair.empty()) {
                    req.query_params[pair] = "";
                }
            }
        } else {
            req.path = full_path;
        }
    }

    // Read headers
    while (std::getline(stream, line)) {
        if (line.empty() || line == "\r") break;
        if (line.back() == '\r') line.pop_back();
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = line.substr(0, colon);
            std::string val = line.substr(colon + 1);
            while (!val.empty() && val.front() == ' ') val.erase(val.begin());
            req.headers[key] = val;
        }
    }

    // Read body
    std::string body_content((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    req.body = body_content;

    return req;
}

std::string HttpServer::read_static_file(const std::string& relative_path, std::string& out_content_type) {
    std::string clean_path = relative_path;
    if (clean_path == "/" || clean_path.empty()) clean_path = "/index.html";

    if (clean_path.find(".html") != std::string::npos) out_content_type = "text/html; charset=utf-8";
    else if (clean_path.find(".css") != std::string::npos) out_content_type = "text/css; charset=utf-8";
    else if (clean_path.find(".js") != std::string::npos) out_content_type = "application/javascript; charset=utf-8";
    else if (clean_path.find(".json") != std::string::npos) out_content_type = "application/json";
    else out_content_type = "text/plain";

    std::vector<std::string> candidates = {
        static_dir_ + clean_path,
        "." + clean_path,
        ".." + clean_path,
        "../web" + clean_path,
        "../web/traditional" + clean_path,
        "./web" + clean_path,
        "./web/traditional" + clean_path,
        "/home/azureuser/riskdb/web" + clean_path,
        "/home/azureuser/riskdb/web/traditional" + clean_path
    };

    for (const auto& candidate : candidates) {
        std::ifstream file(candidate, std::ios::binary);
        if (file.is_open()) {
            std::ostringstream ss;
            ss << file.rdbuf();
            return ss.str();
        }
    }

    return "";
}

void HttpServer::handle_client(int client_sock) {
    struct timeval tv;
    tv.tv_sec = 10;
    tv.tv_usec = 0;
    setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
    setsockopt(client_sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));

    std::vector<char> buffer(16384, 0);
    ssize_t bytes_read = recv(client_sock, buffer.data(), buffer.size() - 1, 0);

    if (bytes_read <= 0) {
        close(client_sock);
        return;
    }

    std::string raw_req(buffer.data(), bytes_read);
    HttpRequest req = parse_request(raw_req);

    HttpResponse res;

    // Handle OPTIONS for CORS pre-flight
    if (req.method == "OPTIONS") {
        res.status_code = 204;
        res.status_text = "No Content";
        res.body = "";
    } else {
        try {
            auto route_it = routes_.find({req.method, req.path});
            if (route_it != routes_.end()) {
                res = route_it->second(req);
            } else {
                // Check static file
                std::string mime;
                std::string file_content = read_static_file(req.path, mime);
                if (!file_content.empty()) {
                    res.status_code = 200;
                    res.status_text = "OK";
                    res.content_type = mime;
                    res.body = file_content;
                } else {
                    res = HttpResponse::not_found("Endpoint or resource not found: " + req.path);
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[-] Error executing route " << req.path << ": " << e.what() << "\n";
            res = HttpResponse::json("{\"error\": \"Internal Server Error\", \"details\": \"" + std::string(e.what()) + "\"}", 500);
        } catch (...) {
            std::cerr << "[-] Unknown error executing route " << req.path << "\n";
            res = HttpResponse::json("{\"error\": \"Internal Server Error\"}", 500);
        }
    }

    std::ostringstream response_stream;
    response_stream << "HTTP/1.1 " << res.status_code << " " << res.status_text << "\r\n";
    response_stream << "Content-Type: " << res.content_type << "\r\n";
    response_stream << "Content-Length: " << res.body.size() << "\r\n";
    response_stream << "Access-Control-Allow-Origin: *\r\n";
    response_stream << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    response_stream << "Access-Control-Allow-Headers: Content-Type, Authorization\r\n";
    response_stream << "Connection: close\r\n";
    response_stream << "\r\n";
    response_stream << res.body;

    std::string res_str = response_stream.str();
    send(client_sock, res_str.data(), res_str.size(), MSG_NOSIGNAL);
    close(client_sock);
}

} // namespace riskdb
