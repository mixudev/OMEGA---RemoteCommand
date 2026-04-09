#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include <string>
#include <iostream>
#include "common/config.h"
#include "common/colors.h"

#include "tunnel_manager.h"

// Prototipe Fungsi UI
void PrintBanner();
void PrintServerStatus();
void CommandCenter();
void InteractLoop(int session_id);
void EnableVTMode();
void AsyncLog(const std::string& msg);
std::string SaveAgentResult(int session_id, const std::string& type, const std::string& data, bool force_json = false);
TunnelType SelectTunnelType();

#endif // UI_MANAGER_H
