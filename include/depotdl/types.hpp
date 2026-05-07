#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace DepotDL {

struct ChunkDescriptor {
    std::string chunk_id;
    uint64_t offset;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    std::string file_path;
};

struct DepotManifest {
    uint32_t depot_id;
    uint64_t manifest_id;
    std::vector<ChunkDescriptor> chunks;
};

struct DownloadConfig {
    std::vector<uint8_t> depot_key;
    std::string output_dir;

    std::optional<uint64_t> app_token;
    std::optional<std::vector<std::string>> cdn_servers;

    uint32_t max_concurrent_chunks = 32;
    uint32_t retry_attempts = 5;
    uint32_t retry_base_delay_ms = 250;
    bool verify_checksums = false;
    bool resume = false;
};

struct DownloadStats {
    uint64_t bytes_downloaded = 0;
    uint64_t bytes_written = 0;
    uint32_t chunks_completed = 0;
    uint32_t chunks_failed = 0;
    uint32_t chunks_total = 0;
};

} // namespace DepotDL