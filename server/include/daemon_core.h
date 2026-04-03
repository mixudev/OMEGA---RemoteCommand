#ifndef DAEMON_CORE_H
#define DAEMON_CORE_H

#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

// Prototipe Daemon
void LaunchDaemon();
#ifdef _WIN32
BOOL WINAPI ConsoleHandler(DWORD ctrlType);
#endif

#endif
