#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#include <string>
#include <openssl/ssl.h>
#include "daemon_core.h"

// Prototipe Sesi
void HandleAgentConnection(SSL* client_ssl, int session_id, std::string peer_ip);
void AcceptorThread(void* server_ptr);
void InteractWithSession(int session_id);

#endif
