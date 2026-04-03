#ifndef EXECUTOR_H
#define EXECUTOR_H

#include <string>

#ifdef _WIN32
#include <windows.h>
struct WindowInfo {
    unsigned long pid;
    char title[MAX_PATH];
};
#endif

class Executor {
public:
    static std::string ExecuteCommand(const std::string& cmd);
    static std::string GetCurrentDir();
};

#endif // EXECUTOR_H
