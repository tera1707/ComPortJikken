#pragma once
#include <Windows.h>
#include <string>
#include <vector>
#include <functional>

// Event-driven serial COM port wrapper
// Uses WaitCommEvent to wait for RXCHAR before performing ReadFile.
class MyComPort2
{
public:
    using ReceiveCallback = std::function<void(const unsigned char* data, size_t len)>;

    MyComPort2();
    ~MyComPort2();

    MyComPort2(const MyComPort2&) = delete;
    MyComPort2& operator=(const MyComPort2&) = delete;

    MyComPort2(MyComPort2&& other) noexcept;
    MyComPort2& operator=(MyComPort2&& other) noexcept;

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

    void Close();
    bool IsOpen() const noexcept { return m_handle != INVALID_HANDLE_VALUE && m_handle != nullptr; }

    // Write raw bytes synchronously. Returns number of bytes written, or -1 on error.
    int Write(const void* buffer, DWORD bytesToWrite);

    // Write using OVERLAPPED. Returns true on success (completed or after wait), false on error.
    bool WriteAsync(const void* buffer, DWORD bytesToWrite);

    // Fixed-length read triggered by RXCHAR event. Returns number of bytes read, or -1 on error.
    int ReadFixedOnRxEvent(void* buffer, DWORD bytesToRead);

    // Read as much as available in input queue when RXCHAR event occurs.
    int ReadAvailableOnRxEvent(std::vector<unsigned char>& outBuffer);

    // Purge in/out buffers
    bool Purge();

    // Background receive handling
    bool StartReceiveThread(ReceiveCallback cb);
    void StopReceiveThread();

    DWORD LastError() const noexcept { return m_lastError; }

    void ResetError() noexcept { m_lastError = 0; }
    void SetErrorFromLastError() noexcept { m_lastError = ::GetLastError(); }

    bool ConfigPort(DWORD baudRate, BYTE byteSize, BYTE parity, BYTE stopBits, DWORD readTimeoutMs, DWORD writeTimeoutMs, bool setDtr, bool setRts);

    static DWORD WINAPI RecvThreadProcStatic(LPVOID lpParam);
    DWORD RecvThreadProc();

    HANDLE m_handle = INVALID_HANDLE_VALUE;
    DWORD  m_lastError = 0;

    // receive thread state
    HANDLE m_hRecvThread = nullptr;
    volatile LONG m_recvRunFlag = 0; // 0=stop,1=run
    ReceiveCallback m_callback;
};
