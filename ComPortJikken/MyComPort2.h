#pragma once
#include <Windows.h>
#include <string>
#include <vector>

// Event-driven serial COM port wrapper
// Uses WaitCommEvent to wait for RXCHAR before performing ReadFile.
class MyComPort2 {
public:
    MyComPort2();
    ~MyComPort2();

    MyComPort2(const MyComPort2&) = delete;
    MyComPort2& operator=(const MyComPort2&) = delete;

    MyComPort2(MyComPort2&& other) noexcept;
    MyComPort2& operator=(MyComPort2&& other) noexcept;

    // Open a COM port. Example: L"COM3". Returns true on success.
    bool Open(const wchar_t* portName,
              DWORD baudRate = CBR_115200,
              BYTE byteSize = 8,
              BYTE parity = NOPARITY,
              BYTE stopBits = ONESTOPBIT,
              DWORD readTimeoutMs = 5000,
              DWORD writeTimeoutMs = 5000,
              bool setDtr = true,
              bool setRts = true);

    void Close();
    bool IsOpen() const noexcept { return m_handle != INVALID_HANDLE_VALUE && m_handle != nullptr; }

    // Write raw bytes. Returns number of bytes written, or -1 on error.
    int Write(const void* buffer, DWORD bytesToWrite);

    // Event-driven read: waits for RXCHAR then reads up to bytesToRead. Returns number of bytes read, or -1 on error.
    int ReadOnRxEvent(void* buffer, DWORD bytesToRead);

    // Read all currently available bytes using event-driven loop (RXCHAR) until buffer empties.
    int ReadAllAvailable(std::vector<unsigned char>& outBuffer);

    // Purge in/out buffers
    bool Purge();

    DWORD LastError() const noexcept { return m_lastError; }

private:
    void ResetError() noexcept { m_lastError = 0; }
    void SetErrorFromLastError() noexcept { m_lastError = ::GetLastError(); }

    bool ConfigurePort(DWORD baudRate, BYTE byteSize, BYTE parity, BYTE stopBits, DWORD readTimeoutMs, DWORD writeTimeoutMs, bool setDtr, bool setRts);

    HANDLE m_handle = INVALID_HANDLE_VALUE;
    DWORD  m_lastError = 0;
};
