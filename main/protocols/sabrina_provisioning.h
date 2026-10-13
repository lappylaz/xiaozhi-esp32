#pragma once

#include <string>

// Only active while the AiPi right button is held at startup, before networking.
void RunSabrinaProvisioningIfRequested();
bool LoadSabrinaProvisionedConfig(std::string& url, std::string& stt_url, std::string& key);
