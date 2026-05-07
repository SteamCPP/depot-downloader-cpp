#include "depotdl/downloader.hpp"
#include "aes/aes.hpp"
#include "depotdl/cdn.hpp"

#include <zlib.h>
#include <lzma.h>
#include <fcntl.h>

#include <stdexcept>
#include <filesystem>
#include "macros.hpp"

using namespace DepotDL;

Downloader::Downloader(DownloadConfig config)
    : config_(std::move(config)), cdn_pool_()
    , fetcher_(cdn_pool_, FetcherConfig{
        .max_concurrent = config_.max_concurrent_chunks,
    })
    , thread_pool_(std::thread::hardware_concurrency()) {

    fetcher_.set_on_fetched([this](const ChunkDescriptor& chunk, std::vector<uint8_t> data) {
        chunks_in_flight_++;
        boost::asio::post(thread_pool_, [this, chunk, data = std::move(data)]() mutable {
            process_chunk(chunk, std::move(data));
            chunks_in_flight_--;
        });
    });

    fetcher_.set_on_failed([this](const ChunkDescriptor& chunk, std::string reason) {
        std::lock_guard lock(stats_mutex_);
        stats_.chunks_failed++;
        update_stats(chunk);
    });
}

std::future<DownloadStats> Downloader::download(const DepotManifest& manifest) {
    return std::async(std::launch::async, [this, &manifest]() -> DownloadStats {
        if (config_.cdn_servers) {
            std::vector<CDNServer> servers;
            for (auto& host : *config_.cdn_servers) {
                servers.push_back(CDNServer{.host = host});
            }
            cdn_pool_.add_servers(std::move(servers));
        } else {
            if (!cdn_pool_.discover()) {
                throw std::runtime_error("Failed to discover CDN servers");
            }
        }

        preallocate_files(manifest);

        fetcher_.enqueue(manifest.chunks, cdn_pool_.best(), manifest.depot_id);
        fetcher_.run();

        thread_pool_.join();

        return stats_;
    });
}

void Downloader::preallocate_files(const DepotManifest& manifest) {
    namespace fs = std::filesystem;
    fs::create_directories(config_.output_dir);

    std::unordered_map<std::string, uint64_t> file_sizes;
    for (auto& chunk : manifest.chunks) {
        file_sizes[chunk.file_path] += chunk.uncompressed_size;
    }

    std::lock_guard lock(files_mutex_);
    for (auto& [path, size] : file_sizes) {
        auto full_path = fs::path(config_.output_dir) / path;
        fs::create_directories(full_path.parent_path());

        std::string path_str = full_path.string();
        int fd = open(path_str.c_str(), O_WRONLY | O_CREAT, 0644);
        if (fd < 0) throw std::runtime_error("Failed to open file: " + full_path.string());

#ifdef __linux__
        fallocate(fd, 0, 0, static_cast<off_t>(size));
#else
        ftruncate(fd, static_cast<off_t>(size));
#endif
        file_descriptors_[path] = fd;
    }
}

void Downloader::process_chunk(const ChunkDescriptor& chunk, std::vector<uint8_t> data) {
    if (cancelled_) return;

    try {
        auto decrypted = decrypt_chunk(data);
        auto decompressed = decompress_chunk(chunk,decrypted);
        write_chunk(chunk, decompressed);

        std::lock_guard lock(stats_mutex_);
        stats_.chunks_completed++;
        stats_.bytes_downloaded += chunk.compressed_size;
        stats_.bytes_written += chunk.uncompressed_size;
        update_stats(chunk);
    } catch (const std::exception& e) {
        std::lock_guard lock(stats_mutex_);
        stats_.chunks_failed++;
    }
}

std::vector<uint8_t> Downloader::decrypt_chunk(const std::vector<uint8_t>& data) {
    return Crypto::decrypt(data, config_.depot_key);
}

std::vector<uint8_t> Downloader::decompress_chunk(const ChunkDescriptor& chunk, const std::vector<uint8_t>& data) {
    if (data.size() >= 2 && data[0] == 'V' && data[1] == 'Z') {
        // Steam VZ header layout:
        // [0-1]  = 'VZ' magic
        // [2]    = version ('a')
        // [3-6]  = CRC32
        // [7-11] = LZMA properties
        // [12..] = compressed data
        // [-10..-2] = checksum + decompressed size
        // [-2..-1] = 'zv' footer

        if (data.back() != 'v' || data[data.size() - 2] != 'z') {
            throw std::runtime_error("VZ: invalid footer");
        }

        uint32_t decompressed_size;
        std::memcpy(&decompressed_size, data.data() + data.size() - 6, sizeof(uint32_t));

        lzma_filter filters[2];
        lzma_options_lzma options;

        filters[0] = {LZMA_FILTER_LZMA1, &options};
        filters[1] = {LZMA_VLI_UNKNOWN, nullptr};

        lzma_properties_decode(filters, nullptr, data.data() + 7, 5);
        filters[0] = {LZMA_FILTER_LZMA1, &options};
        filters[1] = {LZMA_VLI_UNKNOWN, nullptr};

        std::vector<uint8_t> out(decompressed_size);
        size_t out_pos = 0;
        uint64_t memlimit = UINT64_MAX;

        lzma_ret ret = lzma_raw_buffer_decode(
            filters, nullptr,
            data.data() + 12, nullptr, data.size() - 12 - 10,
            out.data(), &out_pos, decompressed_size
        );

        if (ret != LZMA_OK && ret != LZMA_STREAM_END) {
            throw std::runtime_error("LZMA decompression failed");
        }

        out.resize(out_pos);
        return out;
    }

    z_stream zs{};
    if (inflateInit(&zs) != Z_OK) {
        throw std::runtime_error("zlib init failed");
    }

    zs.next_in = const_cast<Bytef*>(data.data());
    zs.avail_in = static_cast<uInt>(data.size());

    std::vector<uint8_t> out;
    out.resize(chunk.uncompressed_size);
    zs.next_out = out.data();
    zs.avail_out = static_cast<uInt>(out.size());

    int ret = inflate(&zs, Z_FINISH);
    while (ret == Z_BUF_ERROR) {
        size_t current = out.size();
        out.resize(current * 2);
        zs.next_out = out.data() + current;
        zs.avail_out = static_cast<uInt>(current);
        ret = inflate(&zs, Z_FINISH);
    }

    inflateEnd(&zs);

    if (ret != Z_STREAM_END) {
        throw std::runtime_error("zlib decompression failed");
    }

    out.resize(zs.total_out);
    return out;
}

void Downloader::write_chunk(const ChunkDescriptor& chunk, const std::vector<uint8_t>& data) {
    std::lock_guard lock(files_mutex_);
    auto it = file_descriptors_.find(chunk.file_path);
    if (it == file_descriptors_.end()) {
        throw std::runtime_error("No file descriptor for: " + chunk.file_path);
    }

    int written = pwrite(it->second, data.data(), data.size(), static_cast<off_t>(chunk.offset));
    if (written < 0 || static_cast<size_t>(written) != data.size()) {
        throw std::runtime_error("pwrite failed for chunk: " + chunk.chunk_id);
    }
}

void Downloader::update_stats(const ChunkDescriptor& chunk) {
    if (progress_cb_) progress_cb_(stats_);
}

void Downloader::cancel() {
    cancelled_ = true;
    fetcher_.cancel();
}

void Downloader::set_progress_callback(ProgressCallback cb) {
    progress_cb_ = std::move(cb);
}