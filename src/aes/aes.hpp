#pragma once

#include <cstdint>
#include <vector>

namespace DepotDL::Crypto {

std::vector<uint8_t> decrypt(const std::vector<uint8_t>& data,
                              const std::vector<uint8_t>& key);

}