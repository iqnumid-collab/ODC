#pragma once

#include <asio.hpp>
#include <asio/ssl.hpp>

#include <chrono>
#include <functional>
#include <string>
#include <unordered_map>

namespace odc {

struct HttpClientConfig {
    std::chrono::seconds connect_timeout{10};
    std::chrono::seconds request_timeout{30};
    bool verify_peer{true};
    std::string ca_cert_path;
    std::string client_cert_path;
    std::string client_key_path;
};

struct HttpResponse {
    int status_code{0};
    std::string status_message;
    std::string body;
    std::unordered_map<std::string, std::string> headers;
};

class HttpClient {
public:
    explicit HttpClient(const HttpClientConfig& cfg = {});
    ~HttpClient() = default;

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    HttpResponse post(const std::string& url,
                      const std::string& body,
                      const std::unordered_map<std::string, std::string>& extra_headers = {});

    HttpResponse get(const std::string& url,
                     const std::unordered_map<std::string, std::string>& extra_headers = {});

private:
    struct UrlParts {
        std::string scheme;
        std::string host;
        uint16_t port{0};
        std::string path;
    };

    static UrlParts parse_url(const std::string& url);
    static std::string url_encode(const std::string& value);

    HttpResponse do_request(const std::string& method, const std::string& url,
                            const std::string& body,
                            const std::unordered_map<std::string, std::string>& extra_headers);

    void setup_ssl(asio::ssl::context& ctx);

    HttpClientConfig cfg_;
};

} // namespace odc
