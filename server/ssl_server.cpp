#include "ssl_server.h"
#include <iostream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#endif

SSLServer::SSLServer(int port, const std::string& certFile, const std::string& keyFile)
    : port(port), certFile(certFile), keyFile(keyFile), serverSocket(INVALID_SOCKET), ctx(nullptr) {
}

SSLServer::~SSLServer() {
    Stop();
}

SSL_CTX* SSLServer::CreateContext() {
    const SSL_METHOD *method = TLS_server_method();
    SSL_CTX *ctx = SSL_CTX_new(method);

    if (!ctx) {
        perror("Unable to create SSL context");
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
    return ctx;
}

void SSLServer::ConfigureContext(SSL_CTX *ctx) {
    if (SSL_CTX_use_certificate_file(ctx, certFile.c_str(), SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    if (SSL_CTX_use_PrivateKey_file(ctx, keyFile.c_str(), SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
}

bool SSLServer::Start() {
    ctx = CreateContext();
    ConfigureContext(ctx);

    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "[-] Error creating socket" << std::endl;
        return false;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverSocket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[-] Error binding to port " << port << std::endl;
        return false;
    }

    if (listen(serverSocket, 1) < 0) {
        std::cerr << "[-] Error listening" << std::endl;
        return false;
    }

    std::cout << "[+] Server listening on port " << port << std::endl;
    return true;
}

SSL* SSLServer::AcceptClient() {
    struct sockaddr_in addr;
#ifdef _WIN32
    int len = sizeof(addr);
#else
    socklen_t len = sizeof(addr);
#endif

    SOCKET clientSocket = accept(serverSocket, (struct sockaddr*)&addr, &len);
    if (clientSocket == INVALID_SOCKET) {
        return nullptr;
    }

    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, clientSocket);

    if (SSL_accept(ssl) <= 0) {
        SSL_free(ssl);
        closesocket(clientSocket);
        return nullptr;
    }

    return ssl;
}

void SSLServer::Stop() {
    if (serverSocket != INVALID_SOCKET) {
        closesocket(serverSocket);
        serverSocket = INVALID_SOCKET;
    }
    if (ctx) {
        SSL_CTX_free(ctx);
        ctx = nullptr;
    }
}
