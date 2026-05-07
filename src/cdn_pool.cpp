#include "depotdl/cdn.hpp"
#include <curl/curl.h>
#include <json.hpp>

using namespace DepotDL;

namespace {
class CurlHandle {
public:
    CurlHandle() : handle_(curl_easy_init()) {
        if (!handle_) throw std::runtime_error("Failed to init CURL");
    }
    ~CurlHandle() { curl_easy_cleanup(handle_); }

    CurlHandle(const CurlHandle&) = delete;
    CurlHandle& operator=(const CurlHandle&) = delete;

    CURL* get() { return handle_; }

private:
    CURL* handle_;
};

size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    static_cast<std::string*>(userp)->append(static_cast<char*>(contents), total);
    return total;
}

std::string fetch_url(const std::string& url) {
    CurlHandle curl;
    std::string response;

    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl.get());
    if (res != CURLE_OK) {
        throw std::runtime_error(std::string("CURL error: ") + curl_easy_strerror(res));
    }

    return response;
}}

CDNPool::CDNPool(RetryPolicy policy)
    : policy_(policy) {}

bool CDNPool::discover(uint32_t cell_id) {
    const std::string url =
        "https://api.steampowered.com/IContentServerDirectoryService/GetServersForSteamPipe/v1/"
        "?cell_id=" + std::to_string(cell_id);

    std::string response;
    try {
        response = fetch_url(url);
    } catch (const std::exception& e) {
        return false;
    }

    auto root = nlohmann::json::parse(response, nullptr, false);
    if (root.is_discarded()) return false;

    auto& servers = root["response"]["servers"];
    if (!servers.is_array() || servers.empty()) return false;

    std::vector<CDNServer> discovered;
    for (auto& s : servers) {
        if (!s.contains("type") || s["type"] != "CDN") continue;
        if (!s.contains("host")) continue;

        CDNServer server;
        server.host = s["host"].get<std::string>();
        server.port = s.value("port", 443);
        server.score = 0.0f;
        discovered.push_back(std::move(server));
    }

    if (discovered.empty()) return false;

    add_servers(std::move(discovered));
    return true;
}

void CDNPool::add_servers(std::vector<CDNServer> servers) {
    std::lock_guard lock(mutex_);
    for (auto& s : servers) {
        pool_.push(std::move(s));
    }
}

float CDNPool::compute_score(const CDNServer& server) const {
    constexpr float FAILURE_PENALTY = 1000.0f;
    return server.score + (server.failures * FAILURE_PENALTY);
}

CDNServer CDNPool::best() {
    std::lock_guard lock(mutex_);
    if (pool_.empty()) throw std::runtime_error("No CDN servers available");

    auto candidate = pool_.top();
    pool_.pop();
    pool_.push(candidate);
    return candidate;
}

void CDNPool::report_success(const CDNServer& server, std::chrono::milliseconds latency) {
    std::lock_guard lock(mutex_);

    std::vector<CDNServer> temp;
    while (!pool_.empty()) {
        temp.push_back(pool_.top());
        pool_.pop();
    }

    for (auto& s : temp) {
        if (s.host == server.host) {
            constexpr float alpha = 0.2f;
            s.score = (alpha * latency.count()) + ((1.0f - alpha) * s.score);
            s.failures = s.failures > 0 ? s.failures - 1 : 0;
            s.last_used = std::chrono::steady_clock::now();
        }
        pool_.push(s);
    }
}

void CDNPool::report_failure(const CDNServer& server) {
    std::lock_guard lock(mutex_);

    std::vector<CDNServer> temp;
    while (!pool_.empty()) {
        temp.push_back(pool_.top());
        pool_.pop();
    }

    for (auto& s : temp) {
        if (s.host == server.host) {
            s.failures++;
            s.last_used = std::chrono::steady_clock::now();
        }
        pool_.push(s);
    }
}