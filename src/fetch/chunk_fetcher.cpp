#include "depotdl/chunk_fetcher.hpp"
#include "depotdl/cdn.hpp"

#include <cassert>
#include <stdexcept>
#include <string>

using namespace DepotDL;

ChunkFetcher::ChunkFetcher(CDNPool& pool, FetcherConfig config)
    : config_(config), pool_(pool) {
    multi_handle_ = curl_multi_init();
    if (!multi_handle_) throw std::runtime_error("Failed to init curl multi handle");
    in_flight_.reserve(config_.max_concurrent);
    
    curl_multi_setopt(multi_handle_, CURLMOPT_MAXCONNECTS, config_.max_concurrent);
}

ChunkFetcher::~ChunkFetcher() {
    for (auto& chunk : in_flight_) {
        curl_multi_remove_handle(multi_handle_, chunk.easy_handle);
        curl_easy_cleanup(chunk.easy_handle);
    }
    curl_multi_cleanup(multi_handle_);
}

void ChunkFetcher::set_on_fetched(ChunkFetchedCallback cb) { on_fetched_ = std::move(cb); }
void ChunkFetcher::set_on_failed(ChunkFailedCallback cb) { on_failed_ = std::move(cb); }

void ChunkFetcher::enqueue(std::vector<ChunkDescriptor> chunks, CDNServer cdn_host, uint32_t depot_id) {
    cdn_host_ = std::move(cdn_host);
    depot_id_ = depot_id;
    for (auto& c : chunks) {
        pending_.push(std::move(c));
    }
}

void ChunkFetcher::cancel() {
    cancelled_ = true;
}

size_t ChunkFetcher::write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    auto* buffer = static_cast<std::vector<uint8_t>*>(userp);
    auto* data = static_cast<uint8_t*>(contents);
    buffer->insert(buffer->end(), data, data + total);
    return total;
}

CURL* ChunkFetcher::make_easy_handle(InFlightChunk& chunk) {
    CURL* easy = curl_easy_init();
    if (!easy) throw std::runtime_error("Failed to init curl easy handle");

    std::string url = "https://" + chunk.cdn_host.host +
                      "/depot/" + std::to_string(depot_id_) +
                      "/chunk/" + chunk.descriptor.chunk_id;

    curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, &chunk.buffer);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, config_.timeout_ms);
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy, CURLOPT_PRIVATE, &chunk);

    if (config_.use_http2) {
        curl_easy_setopt(easy, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
    }

    return easy;
}

void ChunkFetcher::fill_slots() {
    while (!pending_.empty() && in_flight_.size() < config_.max_concurrent) {
        InFlightChunk chunk;
        chunk.descriptor = pending_.front();
        chunk.cdn_host = cdn_host_;
        chunk.start_time = std::chrono::steady_clock::now();
        chunk.attempts = 0;
        pending_.pop();

        chunk.buffer.reserve(chunk.descriptor.compressed_size);

        try {
            chunk.easy_handle = make_easy_handle(chunk);
        } catch (...) {
            if (on_failed_) on_failed_(chunk.descriptor, "Failed to create easy handle");
            continue;
        }

        curl_multi_add_handle(multi_handle_, chunk.easy_handle);
        in_flight_.push_back(std::move(chunk));
    }
}

void ChunkFetcher::handle_completed() {
    CURLMsg* msg;
    int msgs_left;

    while ((msg = curl_multi_info_read(multi_handle_, &msgs_left)) != nullptr) {
        if (msg->msg != CURLMSG_DONE) continue;

        CURL* easy = msg->easy_handle;
        CURLcode result = msg->data.result;

        InFlightChunk* chunk_ptr;
        curl_easy_getinfo(easy, CURLINFO_PRIVATE, &chunk_ptr);

        curl_multi_remove_handle(multi_handle_, easy);
        curl_easy_cleanup(easy);
        easy = nullptr;

        auto it = std::ranges::find_if(in_flight_,
            [chunk_ptr](const InFlightChunk& c) { return &c == chunk_ptr; });

        assert(it != in_flight_.end() && "in_flight_ desync — chunk pointer mismatch");

        if (result == CURLE_OK) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - chunk_ptr->start_time
            );
            pool_.report_success(chunk_ptr->cdn_host, elapsed); 
            if (on_fetched_) on_fetched_(it->descriptor, std::move(it->buffer));
            in_flight_.erase(it);
        } else {
            pool_.report_failure(chunk_ptr->cdn_host);
            it->attempts++;
            if (it->attempts >= config_.max_retries) {
                if (on_failed_) on_failed_(it->descriptor, curl_easy_strerror(result));
                in_flight_.erase(it);
            } else {
                it->cdn_host = pool_.best();
                it->buffer.clear();
                it->easy_handle = make_easy_handle(*it);
                curl_multi_add_handle(multi_handle_, it->easy_handle);
            }
        }
    }
}

void ChunkFetcher::run() {
    fill_slots();

    while (!cancelled_ && (!in_flight_.empty() || !pending_.empty())) {
        int running;
        curl_multi_perform(multi_handle_, &running);

        curl_multi_poll(multi_handle_, nullptr, 0, config_.poll_timeout_ms, nullptr);

        handle_completed();
        fill_slots();
    }
}