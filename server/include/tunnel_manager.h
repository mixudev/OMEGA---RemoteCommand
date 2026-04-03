#ifndef TUNNEL_MANAGER_H
#define TUNNEL_MANAGER_H

#include <string>

// Prototipe Tunnel
void StartTunnelAndGetUrl(int port, std::string& public_url);
void UpdateGitHubGist(const std::string& public_url);
bool IsProcessRunning(const std::string& processName);
bool StartDetachedProcess(const std::string& cmdLine);
void CleanupSystem();
std::string GetDetailedTunnelLogs();

#endif
