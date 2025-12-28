#pragma once
#include <Windows.h>
#include <string>
#include <vector>

// Simple serial COM port wrapper
// Minimal, header-only interface for opening, configuring, reading, and writing.
class MyComPort {
public:
    MyComPort();
    ~MyComPort();

    // Disable copy
    MyComPort(const MyComPort&) = delete;
    MyComPort& operator=(const MyComPort&) = delete;

    // Enable move
    MyComPort(MyComPort&& other) noexcept;
    MyComPort& operator=(MyComPort&& other) noexcept;

    // Open a COM port. Example: L"COM3". Returns true on success.
    bool Open(const wchar_t* portName,
              DWORD baudRate = CBR_9600,
              BYTE byteSize = 8,
              BYTE parity = NOPARITY,
              BYTE stopBits = ONESTOPBIT,
              DWORD readTimeoutMs = 1000,
              DWORD writeTimeoutMs = 1000,
              bool setDtr = true,
              bool setRts = true);

    // Close if open
    void Close();

    // Returns true if handle is valid
    bool IsOpen() const noexcept;

    // Write raw bytes. Returns number of bytes written, or -1 on error.
    int Write(const void* buffer, DWORD bytesToWrite);

    // Read raw bytes. Returns number of bytes read, or -1 on error.
    int Read(void* buffer, DWORD bytesToRead);

    // Read all available bytes until buffer empties. Returns total bytes read, or -1 on error.
    int ReadAllAvailable(std::vector<unsigned char>& outBuffer);

    // Flush in/out buffers
    bool Purge();

    // Get last error code (GetLastError compatible). 0 if no error.
    DWORD LastError() const noexcept { return m_lastError; }

private:
    void ResetError() noexcept { m_lastError = 0; }
    void SetErrorFromLastError() noexcept { m_lastError = ::GetLastError(); }

    HANDLE m_handle;
    DWORD  m_lastError;
};
