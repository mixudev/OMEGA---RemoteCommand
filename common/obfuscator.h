#ifndef OBFUSCATOR_H
#define OBFUSCATOR_H

#include <string>
#include <array>
#include <cstdint>

/**
 * Obfuscator C++17 - Sederhana & Ringan
 * Mengenkripsi string literal saat proses kompilasi (compile-time).
 * Tujuannya agar string sensitif tidak muncul sebagai plain text di file binary (.exe).
 */

namespace Obf {
    // Generate a pseudo-random key based on compile time and index
    inline constexpr uint8_t key(uint32_t index) {
        uint32_t s = 0x12345678; // Seed tetap untuk ODR consistency 
        uint32_t k = s + index;
        k ^= k >> 16;
        k *= 0x85ebca6b;
        k ^= k >> 13;
        k *= 0xc2b2ae35;
        k ^= k >> 16;
        return static_cast<uint8_t>(k & 0xFF);
    }

    template<size_t N>
    struct XorStr {
        std::array<uint8_t, N> data;

        constexpr XorStr(const char* str) : data{} {
            for (size_t i = 0; i < N; ++i) {
                data[i] = static_cast<uint8_t>(str[i]) ^ key(static_cast<uint32_t>(i));
            }
        }

        std::string decrypt() const {
            std::string result;
            result.reserve(N);
            for (size_t i = 0; i < N; ++i) {
                uint8_t d = data[i] ^ key(static_cast<uint32_t>(i));
                if (d == 0) break; // Stop at null terminator if present
                result += static_cast<char>(d);
            }
            return result;
        }
    };
}

// Macro to create an obfuscated string at compile time and decrypt it on use
#define OB_STR(str) ([]() { \
    static constexpr Obf::XorStr<sizeof(str)> obfuscated(str); \
    return obfuscated.decrypt(); \
}())

#endif // OBFUSCATOR_H
