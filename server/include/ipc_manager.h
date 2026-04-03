#ifndef IPC_MANAGER_H
#define IPC_MANAGER_H

#include <string>
#include <mutex>
#include <iostream>

// Shared mutex protecting std::cout across all threads
extern std::mutex g_cout_mutex;

// Thread-safe print helper (does NOT reprint prompt)
// Use AsyncLog() from ui_manager.h for log messages that need a prompt.
inline void SafePrint(const std::string& msg) {
    std::lock_guard<std::mutex> lk(g_cout_mutex);
    std::cout << msg;
    std::cout.flush();
}

// Prototipe IPC
void IPCServerThread();
std::string SendIPCRequest(const std::string& request);

#endif
