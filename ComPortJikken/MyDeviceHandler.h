#pragma once
#include <Windows.h>
#include <string>
#include <vector>

// MyDeviceHandler
// - Enumerate devices matching a Hardware ID substring (e.g., "BTHENUM\\Dev_0CA694033D59")
// - Enable/Disable a specific device by exact Hardware ID
//
// Requires: setupapi.lib, cfgmgr32.lib
class MyDeviceHandler {
public:
    // Find devices whose Hardware ID contains the given substring (case-insensitive)
    // Returns list of matching hardware IDs.
    static std::vector<std::wstring> FindDevicesByHardwareId(const std::wstring& hwidSubstring);

    // Disable device with exact matching Hardware ID
    // Returns true on success
    static bool DisableDeviceByHardwareId(const std::wstring& hardwareId);

    // Enable device with exact matching Hardware ID
    // Returns true on success
    static bool EnableDeviceByHardwareId(const std::wstring& hardwareId);

private:
    static bool ChangeDeviceStateByHardwareId(const std::wstring& hardwareId, bool enable);
};
