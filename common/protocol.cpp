#include "protocol.h"
#include <vector>
#include <cstdint>
#include <cstring>
#include <iostream>

#ifdef _WIN32
#include <winsock2.h> // For htonl, ntohl
#else
#include <arpa/inet.h>
#endif

bool SendSecureMessage(SSL* ssl, const std::string& message) {
    if (!ssl) return false;

    // A protocol message has [4-bytes length][payload]
    uint32_t length = static_cast<uint32_t>(message.length());
    uint32_t network_length = htonl(length);

    // Send the length header (ensure full write)
    uint32_t header_written = 0;
    while (header_written < sizeof(network_length)) {
        int bytes_written = SSL_write(ssl, (char*)&network_length + header_written, sizeof(network_length) - header_written);
        if (bytes_written <= 0) return false;
        header_written += bytes_written;
    }

    // Send the actual payload (ensure full write)
    if (length > 0) {
        uint32_t payload_written = 0;
        while (payload_written < length) {
            int bytes_written = SSL_write(ssl, message.c_str() + payload_written, length - payload_written);
            if (bytes_written <= 0) return false;
            payload_written += bytes_written;
        }
    }

    return true;
}

bool ReceiveSecureMessage(SSL* ssl, std::string& message) {
    if (!ssl) return false;

    message.clear();

    // Read the 4-byte length header in a loop to handle partial reads
    uint32_t network_length = 0;
    char* header_ptr = reinterpret_cast<char*>(&network_length);
    uint32_t total_header_read = 0;
    
    while (total_header_read < sizeof(network_length)) {
        int bytes_read = SSL_read(ssl, header_ptr + total_header_read, sizeof(network_length) - total_header_read);
        if (bytes_read <= 0) {
            return false;
        }
        total_header_read += bytes_read;
    }

    uint32_t length = ntohl(network_length);

    // Check for large or empty payloads
    if (length == 0) {
        return true; 
    }

    if (length > 1024 * 1024 * 50) { // Limit to 50MB
        std::cerr << "[-] Error: Payload too large" << std::endl;
        return false;
    }

    // Read payload
    std::vector<char> buffer(length);
    uint32_t total_read = 0;

    while (total_read < length) {
        int bytes_read = SSL_read(ssl, buffer.data() + total_read, length - total_read);
        if (bytes_read <= 0) {
            return false;
        }
        total_read += bytes_read;
    }

    message.assign(buffer.data(), length);
    return true;
}
