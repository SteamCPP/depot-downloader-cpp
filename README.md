# C++ Steam Depot Downloader
This tool is an effecient yet concise downloader for Steam depots. It connects to a Steam CDN server and downloads a depot given the proper authentication keys.

## Requirements

* Boost
* Libcurl
* Crypto++
* ZLib

## Building

> [!TIP] Ensure you have the proper dependencies installed!

### 1. Clone the repository
```bash
git clone https://github.com/lammmab/depot-downloader-cpp
```

### 2. CD into the repository
```bash
cd depot-downloader-cpp
```

### 3. Configure CMake
```bash
cmake -B build
```
> [!IMPORTANT] You MUST attach `-DBUILD_BINARY=ON` in this command to build the CLI tool! 

### 4. Build the project
```bash
cmake --build build
```