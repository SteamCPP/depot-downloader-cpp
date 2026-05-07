#pragma once

#include "types.hpp"
#include "cdn.hpp"
#include "chunk_fetcher.hpp"

#include <boost/asio.hpp>
#include <atomic>
#include <filesystem>
#include <functional>
#include <future>
#include <mutex>
#include <unordered_map>

namespace DepotDL {

using ProgressCallback = std::function<void(const DownloadStats&)>;

class Downloader {
public:
    explicit Downloader(DownloadConfig config);
    ~Downloader() = default;

    Downloader(const Downloader&) = delete;
    Downloader& operator=(const Downloader&) = delete;
    Downloader(Downloader&&) = default;
    Downloader& operator=(Downloader&&) = default;

    std::future<DownloadStats> download(const DepotManifest& manifest);
    void cancel();
    void set_progress_callback(ProgressCallback cb);

private:
    void preallocate_files(const DepotManifest& manifest);

    void process_chunk(const ChunkDescriptor& chunk, std::vector<uint8_t> data);
    std::vector<uint8_t> decrypt_chunk(const std::vector<uint8_t>& data);
    std::vector<uint8_t> decompress_chunk(const ChunkDescriptor& chunk, const std::vector<uint8_t>& data);
    void write_chunk(const ChunkDescriptor& chunk, const std::vector<uint8_t>& data);

    void update_stats(const ChunkDescriptor& chunk);

    DownloadConfig config_;
    DownloadStats stats_;
    ProgressCallback progress_cb_;

    CDNPool cdn_pool_;
    ChunkFetcher fetcher_;

    std::unordered_map<std::string, int> file_descriptors_;

    boost::asio::thread_pool thread_pool_;

    std::atomic<bool> cancelled_{false};
    std::atomic<uint32_t> chunks_in_flight_{0};
    std::mutex stats_mutex_;
    std::mutex files_mutex_;
};

} // namespace DepotDL