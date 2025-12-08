#include "MyDeviceHandler.h"
#include <SetupAPI.h>
#include <Cfgmgr32.h>
#include <devguid.h>
#include <initguid.h>
#include <algorithm>

#pragma comment(lib, "SetupAPI.lib")
#pragma comment(lib, "Cfgmgr32.lib")

namespace {
    // case-insensitive find
    bool contains_nocase(const std::wstring& haystack, const std::wstring& needle) {
        if (needle.empty()) return true;
        auto it = std::search(
            haystack.begin(), haystack.end(),
            needle.begin(), needle.end(),
            [](wchar_t ch1, wchar_t ch2) {
                return ::CharUpperW((LPWSTR)MAKELONG(ch1, 0)) == ::CharUpperW((LPWSTR)MAKELONG(ch2, 0));
            });
        return it != haystack.end();
    }
}

static std::vector<std::wstring> GetAllHardwareIds() {
    std::vector<std::wstring> result;

    HDEVINFO hDevInfo = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        return result;
    }

    SP_DEVINFO_DATA devInfo{};
    devInfo.cbSize = sizeof(devInfo);

    for (DWORD index = 0; SetupDiEnumDeviceInfo(hDevInfo, index, &devInfo); ++index) {
        // Query required size
        DWORD regType = 0;
        DWORD requiredSize = 0;
        SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devInfo, SPDRP_HARDWAREID, &regType, nullptr, 0, &requiredSize);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || requiredSize == 0)
            continue;

        std::vector<wchar_t> buffer(requiredSize / sizeof(wchar_t) + 2, 0);
        if (!SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devInfo, SPDRP_HARDWAREID, &regType,
            reinterpret_cast<PBYTE>(buffer.data()), requiredSize, nullptr))
            continue;

        // MULTI_SZ: collect all strings
        const wchar_t* p = buffer.data();
        while (*p) {
            result.emplace_back(p);
            p += wcslen(p) + 1;
        }
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
    return result;
}

std::vector<std::wstring> MyDeviceHandler::FindDevicesByHardwareId(const std::wstring& hwidSubstring) {
    std::vector<std::wstring> matches;
    auto all = GetAllHardwareIds();
    for (const auto& id : all) {
        if (contains_nocase(id, hwidSubstring)) {
            matches.push_back(id);
        }
    }
    return matches;
}

static bool GetDevInstForHardwareIdExact(const std::wstring& hardwareId, DEVINST& outInst) {
    HDEVINFO hDevInfo = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        return false;
    }

    SP_DEVINFO_DATA devInfo{};
    devInfo.cbSize = sizeof(devInfo);

    bool found = false;
    for (DWORD index = 0; SetupDiEnumDeviceInfo(hDevInfo, index, &devInfo); ++index) {
        DWORD regType = 0; DWORD requiredSize = 0;
        SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devInfo, SPDRP_HARDWAREID, &regType, nullptr, 0, &requiredSize);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || requiredSize == 0) continue;

        std::vector<wchar_t> buffer(requiredSize / sizeof(wchar_t) + 2, 0);
        if (!SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devInfo, SPDRP_HARDWAREID, &regType,
            reinterpret_cast<PBYTE>(buffer.data()), requiredSize, nullptr)) continue;

        const wchar_t* p = buffer.data();
        while (*p) {
            if (_wcsicmp(p, hardwareId.c_str()) == 0) {
                outInst = devInfo.DevInst;
                found = true;
                break;
            }
            p += wcslen(p) + 1;
        }
        if (found) break;
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
    return found;
}

bool MyDeviceHandler::ChangeDeviceStateByHardwareId(const std::wstring& hardwareId, bool enable) {
    DEVINST devInst;
    if (!GetDevInstForHardwareIdExact(hardwareId, devInst)) {
        return false;
    }

    // Use CM APIs to enable/disable
    ULONG problem = 0;
    CONFIGRET cr;
    if (enable) {
        cr = CM_Enable_DevNode(devInst, 0);
    } else {
        cr = CM_Disable_DevNode(devInst, 0);
    }

    if (cr != CR_SUCCESS) {
        return false;
    }

    // Request re-enumeration
    CM_Reenumerate_DevNode(devInst, 0);

    return true;
}

bool MyDeviceHandler::DisableDeviceByHardwareId(const std::wstring& hardwareId) {
    return ChangeDeviceStateByHardwareId(hardwareId, false);
}

bool MyDeviceHandler::EnableDeviceByHardwareId(const std::wstring& hardwareId) {
    return ChangeDeviceStateByHardwareId(hardwareId, true);
}
