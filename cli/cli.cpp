#include "depotdl/types.hpp"
#include <CLI11.hpp>
#include <depotdl/downloader.hpp>

using namespace DepotDL;

int main(int argc, char** argv) {
    CLI::App app{"Blazing fast depot downloader tool written in C++"};
    argv = app.ensure_utf8(argv);

    DepotDL::DownloadConfig config{};
    DepotDL::DepotManifest manifest{};
    std::string depot_key_hex;
    std::vector<std::string> cdn_servers;

    /* REQUIRED */
    app.add_option("depot_id", manifest.depot_id, "The depot ID to download")->required();
    app.add_option("manifest_id", manifest.manifest_id, "The manifest ID of the depot")->required();
    app.add_option("depot_key", depot_key_hex, "The hex string of the depot key")->required();
    app.add_option("output_dir", config.output_dir, "The directory to output downloaded content to")->required();
    
    /* OPTIONAL */
    app.add_option("--max-chunks", config.max_concurrent_chunks, "Maximum concurrent chunks (DEFAULT 32)");
    app.add_flag("--verify", config.verify_checksums);
    auto* cdn_opt = app.add_option("--cdn-servers", cdn_servers);
    CLI11_PARSE(app, argc, argv);

    if (*cdn_opt) {
        config.cdn_servers = cdn_servers;
    }

    // NOTE: Downloader will NOT work until I add a way to get manifest chunks... Coming SOON™
    DepotDL::Downloader downloader(config);
    downloader.download(manifest);
}