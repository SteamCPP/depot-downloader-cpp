#include "aes.hpp"

#include <cryptopp/aes.h>
#include <cryptopp/filters.h>
#include <cryptopp/modes.h>
#include <stdexcept>

using namespace DepotDL;

std::vector<uint8_t> Crypto::decrypt(const std::vector<uint8_t>& data,
                              const std::vector<uint8_t>& key) {
    if (key.size() != CryptoPP::AES::MAX_KEYLENGTH) {
        throw std::invalid_argument("Depot key must be 32 bytes");
    }

    if (data.size() < CryptoPP::AES::BLOCKSIZE) {
        throw std::invalid_argument("Data too short to contain IV");
    }

    const uint8_t* iv = data.data();
    const uint8_t* ciphertext = data.data() + CryptoPP::AES::BLOCKSIZE;
    size_t ciphertext_len = data.size() - CryptoPP::AES::BLOCKSIZE;

    CryptoPP::CBC_Mode<CryptoPP::AES>::Decryption dec;
    dec.SetKeyWithIV(key.data(), key.size(), iv);

    std::vector<uint8_t> plaintext(ciphertext_len);
    CryptoPP::ArraySource source(
        ciphertext, ciphertext_len, true,
        new CryptoPP::StreamTransformationFilter(
            dec,
            new CryptoPP::ArraySink(plaintext.data(), plaintext.size())
        )
    );

    return plaintext;
}