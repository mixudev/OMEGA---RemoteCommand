#include "ssl_client.h"
#include <iostream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#endif

SSLClient::SSLClient(const std::string& host, int port)
    : host(host), port(port), clientSocket(INVALID_SOCKET), ctx(nullptr), ssl(nullptr) {
}

SSLClient::~SSLClient() {
    Disconnect();
}

SSL_CTX* SSLClient::CreateContext() {
    const SSL_METHOD *method = TLS_client_method();
    SSL_CTX *ctx = SSL_CTX_new(method);

    if (!ctx) {
        perror("Unable to create SSL context");
        ERR_print_errors_fp(stderr);
        return nullptr;
    }
    return ctx;
}

bool SSLClient::Connect() {
    ctx = CreateContext();
    if (!ctx) return false;

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;       // Restrict to IPv4 to prevent resolution complexity
    hints.ai_socktype = SOCK_STREAM; // TCP Stream
    hints.ai_protocol = IPPROTO_TCP;

    std::string portStr = std::to_string(port);

    // Resolve DNS (Hostnames, DDNS, Ngrok, or raw IP)
    int result = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res);
    if (result != 0) {
        std::cerr << "[-] Error resolving hostname or IP: " << host << std::endl;
        return false;
    }

    // Create custom socket based on resolved DNS struct
    clientSocket = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (clientSocket == INVALID_SOCKET) {
        std::cerr << "[-] Error creating socket" << std::endl;
        freeaddrinfo(res);
        return false;
    }

    // Enable TCP Keep-Alive BEFORE connecting
    int optval = 1;
    setsockopt(clientSocket, SOL_SOCKET, SO_KEEPALIVE, (const char*)&optval, sizeof(optval));

#ifdef _WIN32
    struct tcp_keepalive keepalive;
    keepalive.onoff = 1;
    keepalive.keepalivetime = 15000;    // Wait 15 seconds before probing
    keepalive.keepaliveinterval = 2000; // 2 seconds between probes
    DWORD bytesReturned = 0;
    WSAIoctl(clientSocket, SIO_KEEPALIVE_VALS, &keepalive, sizeof(keepalive), NULL, 0, &bytesReturned, NULL, NULL);
#endif

    // Connect to resolved address
    if (connect(clientSocket, res->ai_addr, res->ai_addrlen) < 0) {
        std::cerr << "[-] Connection failed to " << host << ":" << port << std::endl;
        freeaddrinfo(res);
        closesocket(clientSocket);
        clientSocket = INVALID_SOCKET;
        return false;
    }

    freeaddrinfo(res);

    // Create SSL instance
    ssl = SSL_new(ctx);
    if (!ssl) {
        std::cerr << "[-] Failed to create SSL object" << std::endl;
        return false;
    }

    SSL_set_fd(ssl, clientSocket);

    // Perform handshake
    if (SSL_connect(ssl) <= 0) {
        std::cerr << "[-] SSL handshake failed" << std::endl;
        ERR_print_errors_fp(stderr);
        return false;
    }

    std::cout << "[+] Connected to server " << host << ":" << port << std::endl;
    return true;
}

SSL* SSLClient::GetSSL() const {
    return ssl;
}

void SSLClient::Disconnect() {
    if (ssl) {
        // Properly shutdown SSL connection (might block!)
        int shutdown_result = SSL_shutdown(ssl);
        if (shutdown_result == 0) {
            SSL_shutdown(ssl);
        }
        SSL_free(ssl);
        ssl = nullptr;
    }
    if (clientSocket != INVALID_SOCKET) {
        closesocket(clientSocket);
        clientSocket = INVALID_SOCKET;
    }
    if (ctx) {
        SSL_CTX_free(ctx);
        ctx = nullptr;
    }
}

void SSLClient::ForceDisconnect() {
    // ABORTIVE DISCONNECT: No handshake, just kill handles
    if (ssl) {
        SSL_free(ssl); // Free without shutdown handshake
        ssl = nullptr;
    }
    if (clientSocket != INVALID_SOCKET) {
        // Force reset (Linger 0)
        struct linger lin;
        lin.l_onoff = 1;
        lin.l_linger = 0;
        setsockopt(clientSocket, SOL_SOCKET, SO_LINGER, (const char*)&lin, sizeof(lin));
        closesocket(clientSocket);
        clientSocket = INVALID_SOCKET;
    }
    if (ctx) {
        SSL_CTX_free(ctx);
        ctx = nullptr;
    }
}
