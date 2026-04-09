#ifndef UTILS_H
#define UTILS_H

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int SOCKET;
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <chrono>
#include <ctime>
#include <sys/stat.h>
#include <filesystem>
#include <cstring>
#include "config.h"
#include "colors.h"

#ifdef _WIN32
// Helper untuk membersihkan layar terminal (opsional)
inline void ClearConsole() {
    COORD topLeft = { 0, 0 };
    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO screen;
    DWORD written;
    GetConsoleScreenBufferInfo(console, &screen);
    FillConsoleOutputCharacterA(console, ' ', screen.dwSize.X * screen.dwSize.Y, topLeft, &written);
    FillConsoleOutputAttribute(console, screen.wAttributes, screen.dwSize.X * screen.dwSize.Y, topLeft, &written);
    SetConsoleCursorPosition(console, topLeft);
}
#endif

inline void InitializeSockets() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[-] WSAStartup failed" << std::endl;
        exit(EXIT_FAILURE);
    }
#endif
}

inline void CleanupSockets() {
#ifdef _WIN32
    WSACleanup();
#endif
}

// Menjamin direktori tersedia (Log folder)
inline void EnsureDirectoryExists(const std::string& path) {
#ifdef _WIN32
    struct _stat st;
    if (_stat(path.c_str(), &st) != 0) {
        CreateDirectoryA(path.c_str(), NULL);
    }
#else
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        mkdir(path.c_str(), 0755);
    }
#endif
}

// Function to recursively create folders on Windows (Unicode version)
inline bool CreateDirectoryRecursiveW(const std::wstring& path) {
#ifdef _WIN32
    std::wstring current;
    size_t pos = 0;
    while ((pos = path.find_first_of(L"\\/", pos)) != std::wstring::npos) {
        current = path.substr(0, pos++);
        if (!current.empty() && current.back() != L':') {
            CreateDirectoryW(current.c_str(), NULL);
        }
    }
    return CreateDirectoryW(path.c_str(), NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
#else
    return true; 
#endif
}

// Function to set file/directory as hidden on Windows (Unicode version)
inline void SetFileHiddenW(const std::wstring& path) {
#ifdef _WIN32
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
#endif
}

// Function to recursively create folders on Windows
inline bool CreateDirectoryRecursive(const std::string& path) {
    // Legacy support (optional, can be removed)
    return CreateDirectoryA(path.c_str(), NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

// Function to set file/directory as hidden on Windows
inline void SetFileHidden(const std::string& path) {
    SetFileAttributesA(path.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
}

// Function to wipe a directory recursively
inline void WipeDirectory(const std::string& path) {
    try {
        if (std::filesystem::exists(path)) {
            std::filesystem::remove_all(path);
        }
    } catch (...) {}
}

// Fungsi Logging Terpusat yang menghormati Config
inline void LogToFile(const std::string& filename, const std::string& msg) {
    if (!LOGGING_ENABLED) return;

    EnsureDirectoryExists(LOG_DIR);
    
    std::string fullPath = std::string(LOG_DIR) + "/" + filename;
    std::ofstream logFile(fullPath, std::ios::app);
    if (logFile.is_open()) {
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        char timeStr[20];
        struct tm timeinfo;
#ifdef _WIN32
        localtime_s(&timeinfo, &now);
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
#else
        localtime_r(&now, &timeinfo);
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
#endif

        logFile << "[" << timeStr << "] " << msg << std::endl;
        logFile.close();
    }
}

// Fungsi pembantu untuk mengecek apakah port lokal tersedia
inline bool IsPortAvailable(int port) {
#ifdef _WIN32
    SOCKET testSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (testSock == INVALID_SOCKET) return false;

    int opt = 1;
    setsockopt(testSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    int result = bind(testSock, (struct sockaddr*)&addr, sizeof(addr));
    closesocket(testSock);
    return (result != SOCKET_ERROR);
#else
    int testSock = socket(AF_INET, SOCK_STREAM, 0);
    if (testSock < 0) return false;

    int opt = 1;
    setsockopt(testSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    int result = bind(testSock, (struct sockaddr*)&addr, sizeof(addr));
    close(testSock);
    return (result == 0);
#endif
}

// --- Base64 Utilities for File Transfer ---
static const std::string base64_chars = 
             "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
             "abcdefghijklmnopqrstuvwxyz"
             "0123456789+/";

inline std::string Base64Encode(const std::string& in) {
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(base64_chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

inline std::string Base64Decode(const std::string& in) {
    std::string out;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[base64_chars[i]] = i;

    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

// Escaping string for simple JSON output
inline std::string JsonEscape(const std::string& input) {
    std::string output;
    for (char c : input) {
        switch (c) {
            case '\"': output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b"; break;
            case '\f': output += "\\f"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) <= 0x1f) {
                    char buf[8];
                    sprintf(buf, "\\u%04x", (int)(unsigned char)c);
                    output += buf;
                } else {
                    output += c;
                }
        }
    }
    return output;
}

// Function to self-delete a file and optionally its parent directory using a robust looping batch file
inline void SelfDelete(const std::string& path, bool deleteDir = false) {
#ifdef _WIN32
    char tempPath[MAX_PATH];
    if (GetTempPathA(MAX_PATH, tempPath) == 0) return;

    // Get current PID and EXE name for aggressive killing
    DWORD pid = GetCurrentProcessId();
    size_t lastSlash = path.find_last_of("\\/");
    std::string exeName = (lastSlash != std::string::npos) ? path.substr(lastSlash + 1) : path;

    // Use a unique name for the cleanup script to avoid collisions
    std::string batchFile = std::string(tempPath) + "omega_wipe_" + std::to_string(GetTickCount()) + ".bat";
    
    std::ofstream bat(batchFile);
    if (bat.is_open()) {
        bat << "@echo off" << std::endl;
        bat << "setlocal enabledelayedexpansion" << std::endl;
        
        // Wait for the main process to exit gracefully first
        bat << "ping 127.0.0.1 -n 3 > nul" << std::endl;
        
        // Kill the parent process tree aggressively (multiple attempts)
        bat << ":kill_loop" << std::endl;
        bat << "taskkill /F /PID " << pid << " /T > nul 2>&1" << std::endl;
        bat << "taskkill /F /IM \"" << exeName << "\" /T > nul 2>&1" << std::endl;
        
        // Move file out of the original directory to break directory lock
        bat << "set \"TMP_EXE=%TEMP%\\tmp_wipe_" << std::to_string(GetTickCount()) << ".exe\"" << std::endl;
        bat << "move /Y \"" << path << "\" \"!TMP_EXE!\" > nul 2>&1" << std::endl;

        // Force delete log artifacts specifically (with wildcards for safety)
        bat << "del /F /Q /A \"client.log*\" > nul 2>&1" << std::endl;
        bat << "del /F /Q /A \"logs\\client.log*\" > nul 2>&1" << std::endl;
        bat << "rd /s /q \"logs\" > nul 2>&1" << std::endl;
        
        bat << ":loop_file" << std::endl;
        // Delete the binary from either original or new location
        bat << "del /F /Q /A \"" << path << "\" > nul 2>&1" << std::endl;
        bat << "del /F /Q /A \"!TMP_EXE!\" > nul 2>&1" << std::endl;
        
        bat << "if exist \"" << path << "\" (" << std::endl;
        bat << "    taskkill /F /IM \"" << exeName << "\" /T > nul 2>&1" << std::endl;
        bat << "    ping 127.0.0.1 -n 2 > nul" << std::endl;
        bat << "    goto loop_file" << std::endl;
        bat << ")" << std::endl;
        
        if (lastSlash != std::string::npos) {
            std::string parentDir = path.substr(0, lastSlash);
            if (deleteDir) {
                bat << ":loop_dir" << std::endl;
                bat << "rd /s /q \"" << parentDir << "\" > nul 2>&1" << std::endl;
                bat << "if exist \"" << parentDir << "\" (ping 127.0.0.1 -n 2 > nul & goto loop_dir)" << std::endl;
            }
        }
        
        // Self-destruction of the batch file (delayed)
        bat << "start /b \"\" cmd /c \"ping 127.0.0.1 -n 2 > nul & del \"%~f0\"\" & exit" << std::endl;
        bat.close();
        
        // Run the batch file invisibly with highest priority
        ShellExecuteA(NULL, "open", "cmd.exe", ("/c \"" + batchFile + "\"").c_str(), NULL, SW_HIDE);
    }
#endif
}

#endif // UTILS_H
