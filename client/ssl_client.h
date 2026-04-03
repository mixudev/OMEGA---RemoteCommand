#ifndef SSL_CLIENT_H
#define SSL_CLIENT_H

#include <string>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "../common/utils.h"

class SSLClient {
public:
    SSLClient(const std::string& host, int port);
    ~SSLClient();

    bool Connect();
    SSL* GetSSL() const;
    void Disconnect();

private:
    std::string host;
    int port;
    SOCKET clientSocket;
    SSL_CTX* ctx;
    SSL* ssl;

    SSL_CTX* CreateContext();
};

#endif // SSL_CLIENT_H
