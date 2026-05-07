#pragma once

#include <chrono>
#include <mutex>
#include <queue>
#include <random>
#include <string>
#include <vector>

namespace DepotDL {

struct CDNServer {
    std::string host;
    uint16_t port{443};
    float score{0.0f};
    uint32_t failures{0};
    std::chrono::steady_clock::time_point last_used{};

    bool operator>(const CDNServer& other) const { return score > other.score; }
};

struct RetryPolicy {
    uint32_t max_attempts{5};
    uint32_t base_delay_ms{250};
    uint32_t max_delay_ms{30000};
    float backoff_multiplier{2.0f};
    float jitter_factor{0.25f};
};

class CDNPool {
public:
    explicit CDNPool(RetryPolicy policy = {});

    void add_servers(std::vector<CDNServer> servers);
    bool discover(uint32_t cell_id = 0); 

    CDNServer best();

    void report_success(const CDNServer& server, std::chrono::milliseconds latency);
    void report_failure(const CDNServer& server);

    template<typename Fn>
    auto with_retry(Fn&& fn) -> decltype(fn(std::declval<CDNServer>()));

    bool empty() const;
    size_t size() const;

private:
    float compute_score(const CDNServer& server) const;
    std::chrono::milliseconds next_delay(uint32_t attempt) const;

    std::priority_queue<CDNServer, std::vector<CDNServer>, std::greater<CDNServer>> pool_;
    RetryPolicy policy_;

    mutable std::mutex mutex_;
    std::mt19937 rng_{std::random_device{}()};
};

} // namespace DepotDL