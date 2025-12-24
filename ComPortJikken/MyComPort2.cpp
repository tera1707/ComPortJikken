#include "MyComPort2.h"

MyComPort2::MyComPort2() {}
MyComPort2::~MyComPort2() { Close(); }

MyComPort2::MyComPort2(MyComPort2&& other) noexcept {
    m_handle = other.m_handle;
    m_lastError = other.m_lastError;
    m_hRecvThread = other.m_hRecvThread;
    m_recvRunFlag = other.m_recvRunFlag;
    m_callback = std::move(other.m_callback);
    other.m_handle = INVALID_HANDLE_VALUE;
    other.m_lastError = 0;
    other.m_hRecvThread = nullptr;
    other.m_recvRunFlag = 0;
}

MyComPort2& MyComPort2::operator=(MyComPort2&& other) noexcept {
    if (this != &other) {
        Close();
        m_handle = other.m_handle;
        m_lastError = other.m_lastError;
        m_hRecvThread = other.m_hRecvThread;
        m_recvRunFlag = other.m_recvRunFlag;
        m_callback = std::move(other.m_callback);
        other.m_handle = INVALID_HANDLE_VALUE;
        other.m_lastError = 0;
        other.m_hRecvThread = nullptr;
        other.m_recvRunFlag = 0;
    }
    return *this;
}

bool MyComPort2::Open(const wchar_t* portName,
              DWORD baudRate,
              BYTE byteSize,
              BYTE parity,
              BYTE stopBits,
              DWORD readTimeoutMs,
              DWORD writeTimeoutMs,
              bool setDtr,
              bool setRts) {
    ResetError();
    Close();

    // CreateFile to open COM port
    m_handle = ::CreateFileW(portName,
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

    if (!ConfigurePort(baudRate, byteSize, parity, stopBits, readTimeoutMs, writeTimeoutMs, setDtr, setRts)) {
        Close();
        return false;
    }

    // Set to receive character event
    DWORD mask = EV_RXCHAR; // can include EV_ERR, EV_RXFLAG if needed
    if (!::SetCommMask(m_handle, mask)) {
        SetErrorFromLastError();
        Close();
        return false;
    }

    return true;
}

void MyComPort2::Close() {
    // stop receive thread first
    StopReceiveThread();

    if (m_handle != INVALID_HANDLE_VALUE && m_handle != nullptr) {
        ::CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
    }
}

int MyComPort2::Write(const void* buffer, DWORD bytesToWrite) {
    if (!IsOpen()) return -1;
    ResetError();
    DWORD written = 0;
    if (!::WriteFile(m_handle, buffer, bytesToWrite, &written, nullptr)) {
        SetErrorFromLastError();
        return -1;
    }
    return static_cast<int>(written);
}

int MyComPort2::ReadOnRxEvent(void* buffer, DWORD bytesToRead) {
    if (!IsOpen()) return -1;
    ResetError();

    DWORD evtMask = 0;
    // WaitCommEvent waits until an event in the mask occurs (EV_RXCHAR set by SetCommMask)
    if (!::WaitCommEvent(m_handle, &evtMask, nullptr)) {
        SetErrorFromLastError();
        return -1;
    }

    // Check for RXCHAR event before proceeding
    if ((evtMask & EV_RXCHAR) == 0) {
        // Not a receive event; nothing to read now.
        return 0;
    }

    DWORD bytes = 0;
    if (!::ReadFile(m_handle, buffer, bytesToRead, &bytes, nullptr)) {
        SetErrorFromLastError();
        return -1;
    }
    return static_cast<int>(bytes);
}

int MyComPort2::ReadAllAvailable(std::vector<unsigned char>& outBuffer) {
    if (!IsOpen()) return -1;
    ResetError();

    outBuffer.clear();

    DWORD evtMask = 0;
    while (true) {
        if (!::WaitCommEvent(m_handle, &evtMask, nullptr)) {
            SetErrorFromLastError();
            return -1;
        }
        if ((evtMask & EV_RXCHAR) == 0) {
            // Ignore other events
            continue;
        }

        // Query how many bytes are in input queue
        COMSTAT comStat = {};
        DWORD errors = 0;
        if (!::ClearCommError(m_handle, &errors, &comStat)) {
            SetErrorFromLastError();
            return -1;
        }

        if (comStat.cbInQue == 0) {
            // No bytes queued; break to avoid tight loop
            break;
        }

        size_t toRead = comStat.cbInQue;
        std::vector<unsigned char> temp;
        temp.resize(toRead);
        DWORD bytesRead = 0;
        if (!::ReadFile(m_handle, temp.data(), static_cast<DWORD>(toRead), &bytesRead, nullptr)) {
            SetErrorFromLastError();
            return -1;
        }
        outBuffer.insert(outBuffer.end(), temp.begin(), temp.begin() + bytesRead);

        // If less than queued read, continue; otherwise break if queue drained
        if (bytesRead == 0) break;
        // Loop again to wait for next RXCHAR or drain remaining
        if (bytesRead < toRead) {
            // There might still be bytes; continue without an extra event
            continue;
        }

        // After draining, check if more queued
        if (!::ClearCommError(m_handle, &errors, &comStat)) {
            SetErrorFromLastError();
            return -1;
        }
        if (comStat.cbInQue == 0) {
            break;
        }
    }

    return static_cast<int>(outBuffer.size());
}

bool MyComPort2::Purge() {
    if (!IsOpen()) return false;
    ResetError();
    if (!::PurgeComm(m_handle, PURGE_RXCLEAR | PURGE_TXCLEAR)) {
        SetErrorFromLastError();
        return false;
    }
    return true;
}

bool MyComPort2::ConfigurePort(DWORD baudRate, BYTE byteSize, BYTE parity, BYTE stopBits, DWORD readTimeoutMs, DWORD writeTimeoutMs, bool setDtr, bool setRts) {
    DCB dcb = {};
    dcb.DCBlength = sizeof(DCB);
    if (!::GetCommState(m_handle, &dcb)) {
        SetErrorFromLastError();
        return false;
    }
    dcb.BaudRate = baudRate;
    dcb.ByteSize = byteSize;
    dcb.Parity   = parity;
    dcb.StopBits = stopBits;
    dcb.fBinary = TRUE;
    dcb.fParity = (parity != NOPARITY);
    dcb.fDtrControl = setDtr ? DTR_CONTROL_ENABLE : DTR_CONTROL_DISABLE;
    dcb.fRtsControl = setRts ? RTS_CONTROL_ENABLE : RTS_CONTROL_DISABLE;

    if (!::SetCommState(m_handle, &dcb)) {
        SetErrorFromLastError();
        return false;
    }

    COMMTIMEOUTS timeouts = {};
    timeouts.ReadIntervalTimeout = readTimeoutMs; // non-zero to allow timeout
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = readTimeoutMs;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = writeTimeoutMs;

    if (!::SetCommTimeouts(m_handle, &timeouts)) {
        SetErrorFromLastError();
        return false;
    }

    // Set buffer sizes (optional reasonable defaults)
    if (!::SetupComm(m_handle, 4096, 4096)) {
        SetErrorFromLastError();
        return false;
    }

    // Clear any stale data/errors
    DWORD errors = 0;
    COMSTAT stat = {};
    ::ClearCommError(m_handle, &errors, &stat);

    return true;
}

// ---- Receive thread management ----

bool MyComPort2::StartReceiveThread(ReceiveCallback cb) {
    if (!IsOpen()) return false;
    if (m_hRecvThread) return true; // already running
    m_callback = std::move(cb);
    if (!m_callback) return false;
    if (InterlockedCompareExchange(&m_recvRunFlag, 1, 0) != 0) return true;
    m_hRecvThread = ::CreateThread(nullptr, 0, RecvThreadProcStatic, this, 0, nullptr);
    return m_hRecvThread != nullptr;
}

void MyComPort2::StopReceiveThread() {
    if (InterlockedCompareExchange(&m_recvRunFlag, 0, 1) == 1) {
        if (m_hRecvThread) {
            ::WaitForSingleObject(m_hRecvThread, 3000);
            ::CloseHandle(m_hRecvThread);
            m_hRecvThread = nullptr;
        }
    }
}

DWORD WINAPI MyComPort2::RecvThreadProcStatic(LPVOID lpParam) {
    return reinterpret_cast<MyComPort2*>(lpParam)->RecvThreadProc();
}

DWORD MyComPort2::RecvThreadProc() {
    unsigned char buf[512];
    while (InterlockedCompareExchange(&m_recvRunFlag, 1, 1) == 1) {
        if (!IsOpen()) { ::Sleep(50); continue; }
        int n = ReadOnRxEvent(buf, (DWORD)sizeof(buf));
        if (InterlockedCompareExchange(&m_recvRunFlag, 1, 1) != 1) break;
        if (n <= 0) { ::Sleep(5); continue; }
        if (m_callback) m_callback(buf, (size_t)n);
    }
    return 0;
}
