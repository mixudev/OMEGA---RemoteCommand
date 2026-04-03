#ifndef SSL_SERVER_H
#define SSL_SERVER_H

#include <string>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "../common/utils.h"

class SSLServer {
public:
    SSLServer(int port, const std::string& certFile, const std::string& keyFile);
    ~SSLServer();

    bool Start();
    SSL* AcceptClient();
    void Stop();

private:
    int port;
    std::string certFile;
    std::string keyFile;
    SOCKET serverSocket;
    SSL_CTX* ctx;

    SSL_CTX* CreateContext();
    void ConfigureContext(SSL_CTX* ctx);
};

#endif // SSL_SERVER_H
