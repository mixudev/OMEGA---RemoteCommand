#ifndef TUNNEL_MANAGER_H
#define TUNNEL_MANAGER_H

#include <string>

enum class TunnelType {
    LOCALTONET = 1,
    PINGGY = 2,
    SERVEO = 3,
    LOCALHOST_RUN = 4
};

// Prototipe Tunnel
void StartTunnelAndGetUrl(int port, std::string& public_url, TunnelType type = TunnelType::LOCALTONET);
void EnsureSSHKey();
void UpdateGitHubGist(const std::string& public_url);
bool IsProcessRunning(const std::string& processName);
bool StartDetachedProcess(const std::string& cmdLine);
void CleanupSystem();
std::string GetDetailedTunnelLogs();

#endif
