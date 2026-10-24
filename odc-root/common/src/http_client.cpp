#include "odc/http_client.hpp"
#include "odc/logging.hpp"

#include <sstream>

namespace odc {

HttpClient::HttpClient(const HttpClientConfig& cfg)
    : cfg_(cfg) {
}

// ── Public API ──────────────────────────────────────────────────────────────

HttpResponse HttpClient::post(
    const std::string& url,
    const std::string& body,
    const std::unordered_map<std::string, std::string>& extra_headers) {
    return do_request("POST", url, body, extra_headers);
}

HttpResponse HttpClient::get(
    const std::string& url,
    const std::unordered_map<std::string, std::string>& extra_headers) {
    return do_request("GET", url, "", extra_headers);
}

// ── Private ─────────────────────────────────────────────────────────────────

HttpClient::UrlParts HttpClient::parse_url(const std::string& url) {
    UrlParts parts;
    std::string remainder = url;

    if (remainder.find("https://") == 0) {
        parts.scheme = "https";
        parts.port = 443;
        remainder = remainder.substr(8);
    } else if (remainder.find("http://") == 0) {
        parts.scheme = "http";
        parts.port = 80;
        remainder = remainder.substr(7);
    }

    size_t slash = remainder.find('/');
    if (slash != std::string::npos) {
        parts.host = remainder.substr(0, slash);
        parts.path = remainder.substr(slash);
    } else {
        parts.host = remainder;
        parts.path = "/";
    }

    size_t colon = parts.host.rfind(':');
    // Check for IPv6 brackets
    if (colon != std::string::npos && parts.host.find(']') == std::string::npos) {
        // Regular host:port
        parts.port = static_cast<uint16_t>(std::stoi(parts.host.substr(colon + 1)));
        parts.host = parts.host.substr(0, colon);
    }
    // Remove IPv6 brackets
    if (parts.host.front() == '[' && parts.host.back() == ']') {
        parts.host = parts.host.substr(1, parts.host.size() - 2);
    }

    return parts;
}

std::string HttpClient::url_encode(const std::string& value) {
    (void)value;
    return value;
}

HttpResponse HttpClient::do_request(
    const std::string& method,
    const std::string& url,
    const std::string& body,
    const std::unordered_map<std::string, std::string>& extra_headers) {

    HttpResponse response;
    auto logger = get_logger("http_client");

    UrlParts parts = parse_url(url);
    bool use_tls = (parts.scheme == "https");

    asio::io_context io_ctx;

    try {
        asio::ip::tcp::resolver resolver(io_ctx);
        auto endpoints = resolver.resolve(parts.host, std::to_string(parts.port));

        if (use_tls) {
            asio::ssl::context ssl_ctx(asio::ssl::context::tls_client);
            setup_ssl(ssl_ctx);

            asio::ssl::stream<asio::ip::tcp::socket> stream(io_ctx, ssl_ctx);

            // Connect
            asio::connect(stream.lowest_layer(), endpoints);

            // Set timeouts
            asio::steady_timer timer(io_ctx);
            bool timed_out = false;
            timer.expires_after(cfg_.connect_timeout);
            timer.async_wait([&](const std::error_code&) {
                timed_out = true;
                stream.lowest_layer().close();
            });

            // SSL handshake
            stream.handshake(asio::ssl::stream_base::client);

            if (timed_out) {
                response.status_code = 0;
                response.status_message = "Connection timeout";
                return response;
            }

            // Build HTTP request
            std::ostringstream req;
            req << method << " " << parts.path << " HTTP/1.1\r\n";
            req << "Host: " << parts.host << "\r\n";
            req << "Content-Type: application/json\r\n";
            req << "Content-Length: " << body.size() << "\r\n";
            req << "User-Agent: odc-root/0.1.0\r\n";
            req << "Connection: close\r\n";

            for (const auto& [key, value] : extra_headers) {
                req << key << ": " << value << "\r\n";
            }
            req << "\r\n";
            req << body;

            std::string request_str = req.str();

            // Send
            asio::write(stream, asio::buffer(request_str));

            // Read response
            asio::streambuf response_buf;
            asio::error_code ec;
            asio::read_until(stream, response_buf, "\r\n\r\n", ec);

            if (ec && ec != asio::error::eof) {
                response.status_code = 0;
                response.status_message = ec.message();
                return response;
            }

            // Parse status line
            std::istream resp_stream(&response_buf);
            std::string http_ver, status_msg;
            resp_stream >> http_ver >> response.status_code;
            std::getline(resp_stream, status_msg);

            // Remove leading/trailing whitespace from status message
            size_t start = status_msg.find_first_not_of(" \t\r\n");
            if (start != std::string::npos) {
                status_msg = status_msg.substr(start);
                size_t end = status_msg.find_last_not_of(" \t\r\n");
                if (end != std::string::npos)
                    status_msg = status_msg.substr(0, end + 1);
            }
            response.status_message = status_msg;

            // Parse headers
            std::string header_line;
            size_t content_length = 0;
            bool chunked = false;

            while (std::getline(resp_stream, header_line) && header_line != "\r") {
                size_t colon_pos = header_line.find(':');
                if (colon_pos != std::string::npos) {
                    std::string key = header_line.substr(0, colon_pos);
                    std::string value = header_line.substr(colon_pos + 1);

                    size_t vstart = value.find_first_not_of(" \t");
                    if (vstart != std::string::npos)
                        value = value.substr(vstart);
                    size_t vend = value.find_last_not_of(" \t\r\n");
                    if (vend != std::string::npos)
                        value = value.substr(0, vend + 1);

                    response.headers[key] = value;

                    if (key == "Content-Length") {
                        content_length = std::stoul(value);
                    } else if (key == "Transfer-Encoding" &&
                               value.find("chunked") != std::string::npos) {
                        chunked = true;
                    }
                }
            }

            // Read body
            std::ostringstream body_stream;
            if (content_length > 0) {
                std::vector<char> body_buf(content_length);
                asio::error_code body_ec;
                asio::read(stream, asio::buffer(body_buf), body_ec);
                if (!body_ec || body_ec == asio::error::eof) {
                    body_stream.write(body_buf.data(),
                                      static_cast<std::streamsize>(body_buf.size()));
                }
            } else if (chunked) {
                // Simplified chunked decoding — read until connection close
                asio::error_code chunk_ec;
                while (!chunk_ec) {
                    std::vector<char> buf(4096);
                    size_t n = stream.read_some(asio::buffer(buf), chunk_ec);
                    body_stream.write(buf.data(), static_cast<std::streamsize>(n));
                }
            }

            // Consume any remaining data
            try {
                while (true) {
                    std::vector<char> buf(4096);
                    size_t n = stream.read_some(asio::buffer(buf), ec);
                    if (n > 0) {
                        body_stream.write(buf.data(),
                                          static_cast<std::streamsize>(n));
                    }
                    if (ec) break;
                }
            } catch (...) {}

            response.body = body_stream.str();

        } else {
            // Plain HTTP
            asio::ip::tcp::socket socket(io_ctx);

            asio::connect(socket, endpoints);

            std::ostringstream req;
            req << method << " " << parts.path << " HTTP/1.1\r\n";
            req << "Host: " << parts.host << "\r\n";
            req << "Content-Type: application/json\r\n";
            req << "Content-Length: " << body.size() << "\r\n";
            req << "User-Agent: odc-root/0.1.0\r\n";
            req << "Connection: close\r\n";

            for (const auto& [key, value] : extra_headers) {
                req << key << ": " << value << "\r\n";
            }
            req << "\r\n";
            req << body;

            std::string request_str = req.str();
            asio::write(socket, asio::buffer(request_str));

            asio::streambuf response_buf;
            asio::error_code ec;
            asio::read_until(socket, response_buf, "\r\n\r\n", ec);

            std::istream resp_stream(&response_buf);
            std::string http_ver, status_msg;
            resp_stream >> http_ver >> response.status_code;
            std::getline(resp_stream, status_msg);
            response.status_message = status_msg;

            // Read headers + body (same as TLS path above)
            std::string header_line;
            size_t content_length = 0;
            while (std::getline(resp_stream, header_line) && header_line != "\r") {
                size_t colon_pos = header_line.find(':');
                if (colon_pos != std::string::npos) {
                    std::string key = header_line.substr(0, colon_pos);
                    std::string value = header_line.substr(colon_pos + 1);
                    size_t vstart = value.find_first_not_of(" \t\r\n");
                    if (vstart != std::string::npos) value = value.substr(vstart);
                    response.headers[key] = value;
                    if (key == "Content-Length")
                        content_length = std::stoul(value);
                }
            }

            if (content_length > 0) {
                std::vector<char> body_buf(content_length);
                asio::error_code body_ec;
                asio::read(socket, asio::buffer(body_buf), body_ec);
                if (!body_ec || body_ec == asio::error::eof)
                    response.body.assign(body_buf.data(), body_buf.size());
            }

            socket.close();
        }
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger, "HTTP request failed: {}", e.what());
        response.status_code = 0;
        response.status_message = e.what();
    }

    return response;
}

void HttpClient::setup_ssl(asio::ssl::context& ctx) {
    ctx.set_options(
        asio::ssl::context::default_workarounds |
        asio::ssl::context::no_sslv2 |
        asio::ssl::context::no_sslv3 |
        asio::ssl::context::no_tlsv1 |
        asio::ssl::context::no_tlsv1_1);

    // Server certificate verification
    if (cfg_.verify_peer && !cfg_.ca_cert_path.empty()) {
        ctx.load_verify_file(cfg_.ca_cert_path);
        ctx.set_verify_mode(asio::ssl::verify_peer);
    } else if (!cfg_.verify_peer) {
        ctx.set_verify_mode(asio::ssl::verify_none);
    }

    // Client certificate for mTLS
    if (!cfg_.client_cert_path.empty() && !cfg_.client_key_path.empty()) {
        ctx.use_certificate_chain_file(cfg_.client_cert_path);
        ctx.use_private_key_file(cfg_.client_key_path, asio::ssl::context::pem);
    }
}

} // namespace odc
