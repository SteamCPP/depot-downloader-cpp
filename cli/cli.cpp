#include <CLI11.hpp>

int main(int argc, char** argv) {
    CLI::App depot_dl{"Depot downloader tool written in C++"};
    argv = depot_dl.ensure_utf8(argv);
}