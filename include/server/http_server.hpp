#pragma once

#include <string>
#include <functional>
#include <map>
#include <memory>
#include <atomic>
#include <thread>
#include <vector>

namespace riskdb {

struct HttpRequest {
    std::string method;
    std::string path;
    std::string query_string;
    std::map<std::string, std::string> query_params;
    std::map<std::string, std::string> headers;
    std::string body;

    std::string get_param(const std::string& key, const std::string& default_val = "") const {
        auto it = query_params.find(key);
        return (it != query_params.end()) ? it->second : default_val;
    }
};

struct HttpResponse {
    int status_code{200};
    std::string status_text{"OK"};
    std::string content_type{"application/json"};
    std::map<std::string, std::string> headers;
    std::string body;

    static HttpResponse json(const std::string& json_body, int code = 200) {
        HttpResponse res;
        res.status_code = code;
        res.status_text = (code == 200) ? "OK" : "Error";
        res.content_type = "application/json";
        res.body = json_body;
        return res;
    }

    static HttpResponse html(const std::string& html_body, int code = 200) {
        HttpResponse res;
        res.status_code = code;
        res.status_text = (code == 200) ? "OK" : "Error";
        res.content_type = "text/html; charset=utf-8";
        res.body = html_body;
        return res;
    }

    static HttpResponse css(const std::string& css_body) {
        HttpResponse res;
        res.status_code = 200;
        res.status_text = "OK";
        res.content_type = "text/css; charset=utf-8";
        res.body = css_body;
        return res;
    }

    static HttpResponse js(const std::string& js_body) {
        HttpResponse res;
        res.status_code = 200;
        res.status_text = "OK";
        res.content_type = "application/javascript; charset=utf-8";
        res.body = js_body;
        return res;
    }

    static HttpResponse not_found(const std::string& msg = "Not Found") {
        HttpResponse res;
        res.status_code = 404;
        res.status_text = "Not Found";
        res.content_type = "application/json";
        res.body = "{\"error\": \"" + msg + "\"}";
        return res;
    }
};

using HttpHandler = std::function<HttpResponse(const HttpRequest&)>;

class HttpServer {
public:
    explicit HttpServer(int port = 8080);
    ~HttpServer();

    void route(const std::string& method, const std::string& path, HttpHandler handler);
    void set_static_dir(const std::string& dir_path) { static_dir_ = dir_path; }

    bool start();
    void stop();
    bool is_running() const { return running_; }

private:
    void accept_loop();
    void handle_client(int client_socket);
    static HttpRequest parse_request(const std::string& raw_request);
    std::string read_static_file(const std::string& relative_path, std::string& out_content_type);

    int port_{8080};
    int server_socket_{-1};
    std::atomic<bool> running_{false};
    std::thread accept_thread_;
    std::map<std::pair<std::string, std::string>, HttpHandler> routes_;
    std::string static_dir_{"./web"};
};

} // namespace riskdb
