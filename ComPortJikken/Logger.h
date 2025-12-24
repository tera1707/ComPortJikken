#pragma once
#include <Windows.h>
#include <string>

// Simple UI + file logger
class Logger {
public:
    // hDlg can be null for file-only logging; logFilePath can be null to disable file logging
    explicit Logger(HWND hDlg = nullptr, const wchar_t* logFilePath = nullptr);
    ~Logger();

    // Set target dialog handle at runtime
    void SetDialog(HWND hDlg) noexcept { m_hDlg = hDlg; }

    // Set/Change log file path (reopen file)
    bool SetLogFile(const wchar_t* logFilePath);

    // Append a log entry (timestamped). If text is null, no-op.
    void Append(const wchar_t* text) noexcept;

private:
    void AppendToListBox(const std::wstring& line) noexcept;
    void AppendToFile(const std::wstring& line) noexcept;
    static std::wstring MakeTimestampedLine(const wchar_t* text) noexcept;

    HWND m_hDlg = nullptr;
    HANDLE m_file = INVALID_HANDLE_VALUE;
};
