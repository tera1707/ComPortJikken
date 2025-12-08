#include "MyComPort.h"
#include <vector>

MyComPort::MyComPort() : m_handle(INVALID_HANDLE_VALUE), m_lastError(0) {}

MyComPort::~MyComPort() {
    Close();
}

MyComPort::MyComPort(MyComPort&& other) noexcept
    : m_handle(other.m_handle), m_lastError(other.m_lastError) {
    other.m_handle = INVALID_HANDLE_VALUE;
    other.m_lastError = 0;
}

MyComPort& MyComPort::operator=(MyComPort&& other) noexcept {
    if (this != &other) {
        Close();
        m_handle = other.m_handle;
        m_lastError = other.m_lastError;
        other.m_handle = INVALID_HANDLE_VALUE;
        other.m_lastError = 0;
    }
    return *this;
}

bool MyComPort::Open(const wchar_t* portName, DWORD baudRate, BYTE byteSize, BYTE parity, BYTE stopBits, DWORD readTimeoutMs, DWORD writeTimeoutMs, bool setDtr, bool setRts) {
    Close();
    ResetError();

    // Build full device path: \\\\.\COMx
    std::wstring devicePath = L"\\\\.\\";
    devicePath += portName ? portName : L"";

    m_handle = ::CreateFileW(devicePath.c_str(),
                             GENERIC_READ | GENERIC_WRITE,
                             0,
                             nullptr,
                             OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL,
                             nullptr);

    if (m_handle == INVALID_HANDLE_VALUE) {
        SetErrorFromLastError();
        return false;
    }

    // Configure timeouts
    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = readTimeoutMs;
    timeouts.ReadTotalTimeoutConstant = readTimeoutMs;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = writeTimeoutMs;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    if (!::SetCommTimeouts(m_handle, &timeouts)) {
        SetErrorFromLastError();
        Close();
        return false;
    }

    // Configure DCB
    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (!::GetCommState(m_handle, &dcb)) {
        SetErrorFromLastError();
        Close();
        return false;
    }

    dcb.BaudRate = baudRate;
    dcb.ByteSize = byteSize;
    dcb.Parity = parity;
    dcb.StopBits = stopBits;
    dcb.fBinary = TRUE;
    dcb.fParity = (parity != NOPARITY);

    // Apply DTR/RTS according to parameters
    dcb.fDtrControl = setDtr ? DTR_CONTROL_ENABLE : DTR_CONTROL_DISABLE;
    dcb.fRtsControl = setRts ? RTS_CONTROL_ENABLE : RTS_CONTROL_DISABLE;

    if (!::SetCommState(m_handle, &dcb)) {
        SetErrorFromLastError();
        Close();
        return false;
    }

    // Setup buffers
    if (!::SetupComm(m_handle, 1 << 15, 1 << 15)) {
        SetErrorFromLastError();
        // not fatal; continue
    }

    // Clear junk data
    ::PurgeComm(m_handle, PURGE_RXCLEAR | PURGE_TXCLEAR);

    return true;
}

void MyComPort::Close() {
    if (m_handle != INVALID_HANDLE_VALUE) {
        ::CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
    }
}

bool MyComPort::IsOpen() const noexcept {
    return m_handle != INVALID_HANDLE_VALUE;
}

int MyComPort::Write(const void* buffer, DWORD bytesToWrite) {
    if (!IsOpen()) return -1;
    ResetError();

    DWORD written = 0;
    if (!::WriteFile(m_handle, buffer, bytesToWrite, &written, nullptr)) {
        SetErrorFromLastError();
        return -1;
    }
    return static_cast<int>(written);
}

int MyComPort::Read(void* buffer, DWORD bytesToRead) {
    if (!IsOpen()) return -1;
    ResetError();

    DWORD read = 0;
    if (!::ReadFile(m_handle, buffer, bytesToRead, &read, nullptr)) {
        SetErrorFromLastError();
        return -1;
    }
    return static_cast<int>(read);
}

int MyComPort::ReadAllAvailable(std::vector<unsigned char>& outBuffer) {
    if (!IsOpen()) return -1;
    ResetError();

    outBuffer.clear();

    // Read 1 byte at a time and rely on COMMTIMEOUTS for timeout.
    // Break when Read returns 0 bytes (timeout/no more data within the window).
    for (;;) {
        unsigned char b = 0;
        int r = Read(&b, 1);
        if (r < 0) {
            return -1; // error
        }
        if (r == 0) {
            break; // timeout/no data
        }
        outBuffer.push_back(b);
        // Continue to attempt reading next byte until timeout occurs
    }

    return static_cast<int>(outBuffer.size());
}

bool MyComPort::Purge() {
    if (!IsOpen()) return false;
    if (!::PurgeComm(m_handle, PURGE_RXCLEAR | PURGE_TXCLEAR | PURGE_RXABORT | PURGE_TXABORT)) {
        SetErrorFromLastError();
        return false;
    }
    return true;
}
