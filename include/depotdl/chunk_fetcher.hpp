#pragma once

#include "types.hpp"
#include "cdn.hpp"

#include <atomic>

#include <curl/curl.h>

#include <functional>
#include <queue>
#include <string>
#include <vector>

namespace DepotDL {

using ChunkFetchedCallback = std::function<void(const ChunkDescriptor&, std::vector<uint8_t>)>;
using ChunkFailedCallback = std::function<void(const ChunkDescriptor&, std::string reason)>;

struct FetcherConfig {
    uint32_t max_concurrent{32};
    uint32_t max_retries{5};
    uint32_t timeout_ms{30000};
    uint32_t poll_timeout_ms{1000};
    bool use_http2{true};
};

class ChunkFetcher {
public:
    explicit ChunkFetcher(CDNPool& pool, FetcherConfig config = {});
    ~ChunkFetcher();

    ChunkFetcher(const ChunkFetcher&) = delete;
    ChunkFetcher& operator=(const ChunkFetcher&) = delete;

    void set_on_fetched(ChunkFetchedCallback cb);
    void set_on_failed(ChunkFailedCallback cb);

    void enqueue(std::vector<ChunkDescriptor> chunks, CDNServer cdn_host, uint32_t depot_id);

    void run();
    void cancel();

private:
    struct InFlightChunk {
        ChunkDescriptor descriptor;
        CURL* easy_handle{nullptr};
        std::vector<uint8_t> buffer;
        CDNServer cdn_host;
        uint32_t attempts{0};
        std::chrono::steady_clock::time_point start_time;
    };

    CURL* make_easy_handle(InFlightChunk& chunk);
    void fill_slots();
    void handle_completed();
    static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp);

    CURLM* multi_handle_{nullptr};
    FetcherConfig config_;

    std::queue<ChunkDescriptor> pending_;
    std::vector<InFlightChunk> in_flight_;
    CDNServer cdn_host_;
    uint32_t depot_id_;

    ChunkFetchedCallback on_fetched_;
    ChunkFailedCallback on_failed_;
    CDNPool& pool_;

    std::atomic<bool> cancelled_{false};
};

} // namespace DepotDL