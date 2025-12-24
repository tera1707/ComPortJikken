#include "Logger.h"
#include "resource.h"
#include <vector>

Logger::Logger(HWND hDlg, const wchar_t* logFilePath)
    : m_hDlg(hDlg) {
    if (logFilePath) {
        SetLogFile(logFilePath);
    }
}

Logger::~Logger() {
    if (m_file != INVALID_HANDLE_VALUE && m_file != nullptr) {
        ::CloseHandle(m_file);
        m_file = INVALID_HANDLE_VALUE;
    }
}

bool Logger::SetLogFile(const wchar_t* logFilePath) {
    if (!logFilePath || !*logFilePath) {
        // Close existing file if any
        if (m_file != INVALID_HANDLE_VALUE && m_file != nullptr) {
            ::CloseHandle(m_file);
            m_file = INVALID_HANDLE_VALUE;
        }
        return true;
    }

    // Ensure previous handle is closed
    if (m_file != INVALID_HANDLE_VALUE && m_file != nullptr) {
        ::CloseHandle(m_file);
        m_file = INVALID_HANDLE_VALUE;
    }

    m_file = ::CreateFileW(logFilePath,
                           FILE_APPEND_DATA,
                           FILE_SHARE_READ,
                           nullptr,
                           OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    return m_file != INVALID_HANDLE_VALUE;
}

void Logger::Append(const wchar_t* text) noexcept {
    if (!text) return;
    std::wstring line = MakeTimestampedLine(text);
    AppendToListBox(line);
    AppendToFile(line);
}

void Logger::AppendToListBox(const std::wstring& line) noexcept {
    if (!m_hDlg) return;
    HWND hList = GetDlgItem(m_hDlg, IDC_LOG_LIST);
    if (!hList) return;
    SendMessage(hList, LB_INSERTSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
    LRESULT count = SendMessage(hList, LB_GETCOUNT, 0, 0);
    if (count > 0) {
        SendMessage(hList, LB_SETTOPINDEX, (WPARAM)(count - 1), 0);
    }
}

void Logger::AppendToFile(const std::wstring& line) noexcept {
    if (m_file == INVALID_HANDLE_VALUE || m_file == nullptr) return;
    // Add CRLF
    std::wstring withCrLf = line;
    withCrLf += L"\r\n";

    // Convert to UTF-8 for file
    int bytesNeeded = ::WideCharToMultiByte(CP_UTF8, 0, withCrLf.c_str(), (int)withCrLf.size(), nullptr, 0, nullptr, nullptr);
    if (bytesNeeded <= 0) return;
    std::vector<char> buf(bytesNeeded);
    ::WideCharToMultiByte(CP_UTF8, 0, withCrLf.c_str(), (int)withCrLf.size(), buf.data(), bytesNeeded, nullptr, nullptr);

    DWORD written = 0;
    ::SetFilePointer(m_file, 0, nullptr, FILE_END);
    ::WriteFile(m_file, buf.data(), (DWORD)buf.size(), &written, nullptr);
}

std::wstring Logger::MakeTimestampedLine(const wchar_t* text) noexcept {
    SYSTEMTIME st{};
    ::GetLocalTime(&st);
    wchar_t header[128];
    swprintf_s(header, L"[%02u:%02u:%02u.%03u] ",
        (unsigned)st.wHour,
        (unsigned)st.wMinute,
        (unsigned)st.wSecond,
        (unsigned)st.wMilliseconds);
    return std::wstring(header) + text;
}
