#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <string>
#include <openssl/ssl.h>

#include "obfuscator.h"

// Token autentikasi rahasia untuk komunikasi Agent-Server
// Dienkripsi agar tidak terbongkar saat binary dibongkar dengan tool 'strings'.
inline std::string AUTH_TOKEN = OB_STR("Secr3tToken123!");

// Send a length-prefixed secure message over SSL
bool SendSecureMessage(SSL* ssl, const std::string& message);

// Receive a length-prefixed secure message over SSL
bool ReceiveSecureMessage(SSL* ssl, std::string& message);

#endif // PROTOCOL_H
