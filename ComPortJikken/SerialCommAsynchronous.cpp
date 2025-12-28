#include "SerialCommAsynchronous.h"

SerialCommAsynchronous::SerialCommAsynchronous() {}
SerialCommAsynchronous::~SerialCommAsynchronous() { Close(); }

SerialCommAsynchronous::SerialCommAsynchronous(SerialCommAsynchronous&& other) noexcept
{
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

SerialCommAsynchronous& SerialCommAsynchronous::operator=(SerialCommAsynchronous&& other) noexcept
{
    if (this != &other)
    {
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

bool SerialCommAsynchronous::Open(const wchar_t* portName,
              DWORD baudRate,
              BYTE byteSize,
              BYTE parity,
              BYTE stopBits,
              DWORD readTimeoutMs,
              DWORD writeTimeoutMs,
              bool setDtr,
              bool setRts)
{
    ResetError();
    Close();

    // CreateFile to open COM port
    m_handle = ::CreateFileW(portName,
                             GENERIC_READ | GENERIC_WRITE,
                             0,
                             nullptr,
                             OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                             nullptr);

    if (m_handle == INVALID_HANDLE_VALUE)
    {
        SetErrorFromLastError();
        return false;
    }

    if (!ConfigPort(baudRate, byteSize, parity, stopBits, readTimeoutMs, writeTimeoutMs, setDtr, setRts))
    {
        Close();
        return false;
    }

    // Set to receive character event
    DWORD mask = EV_RXCHAR; // can include EV_ERR, EV_RXFLAG if needed
    if (!::SetCommMask(m_handle, mask))
    {
        SetErrorFromLastError();
        Close();
        return false;
    }

    return true;
}

void SerialCommAsynchronous::Close()
{
    // stop receive thread first
    StopReceiveThread();

    if (m_handle != INVALID_HANDLE_VALUE && m_handle != nullptr)
    {
        // m_handleがCloseされると、非同期のWrite/Readの待ちイベントlocalOv.hEvent/ovEvt.hEvent もキャンセルされる
        ::CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
    }
}

int SerialCommAsynchronous::Write(const void* buffer, DWORD bytesToWrite)
{
    if (!IsOpen())
        return -1;

    if (buffer == nullptr || bytesToWrite == 0) return 0;
    ResetError();

    DWORD written = 0;
    if (!::WriteFile(m_handle, buffer, bytesToWrite, &written, nullptr))
    {
        SetErrorFromLastError();
        return -1;
    }
    return static_cast<int>(written);
}

bool SerialCommAsynchronous::WriteAsync(const void* buffer, DWORD bytesToWrite)
{
    if (!IsOpen())
        return false;

    if (buffer == nullptr || bytesToWrite == 0)
        return false;

    ResetError();

    OVERLAPPED localOv = {};
    localOv.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);

    DWORD written = 0;
    BOOL ok = ::WriteFile(m_handle, buffer, bytesToWrite, &written, &localOv);

    if (!ok)
    {
        DWORD err = ::GetLastError();
        if (err != ERROR_IO_PENDING)
        {
            m_lastError = err;
            return false;
        }

        // 書き込み完了待ち
        DWORD wr = ::WaitForSingleObject(localOv.hEvent, INFINITE);
        if (wr == WAIT_OBJECT_0)
        {
            // Retrieve result
            BOOL res = ::GetOverlappedResult(m_handle, &localOv, &written, FALSE);
            if (!res)
            {
                m_lastError = ::GetLastError();
                return false;
            }
            return true;
        }
        else if (wr == WAIT_TIMEOUT)
        {
            // Cancel I/O on timeout
            m_lastError = WAIT_TIMEOUT;
            ::CancelIoEx(m_handle, &localOv);
            return false;
        }
        else
        {
            // WAIT_FAILED or abandoned
            m_lastError = ::GetLastError();
            ::CancelIoEx(m_handle, &localOv);
            return false;
        }
    }
    // completed immediately
    return true;
}

// 受信イベントが来たら、固定の長さを読み込む方式
int SerialCommAsynchronous::ReadFixedOnRxEvent(void* buffer, DWORD bytesToRead)
{
    if (!IsOpen())
        return -1;

    if (buffer == nullptr || bytesToRead == 0)
        return 0;

    ResetError();

    DWORD evtMask = 0;
    // WaitCommEvent waits until an event in the mask occurs (EV_RXCHAR set by SetCommMask)
    OVERLAPPED ovEvt = {};
    ovEvt.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    BOOL waitOk = ::WaitCommEvent(m_handle, &evtMask, &ovEvt);
    if (!waitOk)
    {
        DWORD err = ::GetLastError();
        if (err == ERROR_IO_PENDING)
        {
            DWORD wr = ::WaitForSingleObject(ovEvt.hEvent, INFINITE);
            if (wr != WAIT_OBJECT_0)
            {
                ::CloseHandle(ovEvt.hEvent);
                m_lastError = (wr == WAIT_TIMEOUT) ? WAIT_TIMEOUT : ::GetLastError();
                return -1;
            }
        }
        else
        {
            ::CloseHandle(ovEvt.hEvent);
            SetErrorFromLastError();
            return -1;
        }
    }

    ::CloseHandle(ovEvt.hEvent);

    // Check for RXCHAR event before proceeding
    if ((evtMask & EV_RXCHAR) == 0)
    {
        // Not a receive event; nothing to read now.
        return 0;
    }

    DWORD bytes = 0;
    OVERLAPPED ovRead = {};
    ovRead.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    BOOL readOk = ::ReadFile(m_handle, buffer, bytesToRead, &bytes, &ovRead);
    if (!readOk)
    {
        DWORD err = ::GetLastError();
        if (err == ERROR_IO_PENDING)
        {
            DWORD wr = ::WaitForSingleObject(ovRead.hEvent, INFINITE);
            if (wr == WAIT_OBJECT_0)
            {
                BOOL res = ::GetOverlappedResult(m_handle, &ovRead, &bytes, FALSE);
                ::CloseHandle(ovRead.hEvent);
                if (!res)
                {
                    m_lastError = ::GetLastError();
                    return -1;
                }
                return static_cast<int>(bytes);
            }
            else
            {
                ::CancelIoEx(m_handle, &ovRead);
                ::CloseHandle(ovRead.hEvent);
                m_lastError = (wr == WAIT_TIMEOUT) ? WAIT_TIMEOUT : ::GetLastError();
                return -1;
            }
        }
        else
        {
            ::CloseHandle(ovRead.hEvent);
            m_lastError = err;
            return -1;
        }
    }

    ::CloseHandle(ovRead.hEvent);
    return static_cast<int>(bytes);
}

// 受信イベントが来たら、受信バッファに入っている分だけReadする方式
int SerialCommAsynchronous::ReadAvailableOnRxEvent(std::vector<unsigned char>& outBuffer)
{
    if (!IsOpen())
        return -1;

    ResetError();

    outBuffer.clear();

    DWORD evtMask = 0;
    while (true)
    {
        OVERLAPPED ovEvt = {};
        ovEvt.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        BOOL waitOk = ::WaitCommEvent(m_handle, &evtMask, &ovEvt);
        if (!waitOk)
        {
            DWORD err = ::GetLastError();
            if (err == ERROR_IO_PENDING)
            {
                DWORD wr = ::WaitForSingleObject(ovEvt.hEvent, INFINITE);
                if (wr != WAIT_OBJECT_0)
                {
                    ::CloseHandle(ovEvt.hEvent);
                    m_lastError = (wr == WAIT_TIMEOUT) ? WAIT_TIMEOUT : ::GetLastError();
                    return -1;
                }
            }
            else
            {
                ::CloseHandle(ovEvt.hEvent);
                SetErrorFromLastError();
                return -1;
            }
        }
        ::CloseHandle(ovEvt.hEvent);

        if ((evtMask & EV_RXCHAR) == 0)
        {
            // Ignore other events
            continue;
        }

        // Query how many bytes are in input queue
        COMSTAT comStat = {};
        DWORD errors = 0;
        if (!::ClearCommError(m_handle, &errors, &comStat))
        {
            SetErrorFromLastError();
            return -1;
        }

        if (comStat.cbInQue == 0)
        {
            // No bytes queued; break to avoid tight loop
            break;
        }

        size_t toRead = comStat.cbInQue;
        std::vector<unsigned char> temp;
        temp.resize(toRead);
        DWORD bytesRead = 0;
        OVERLAPPED ovRead = {};
        ovRead.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        BOOL readOk = ::ReadFile(m_handle, temp.data(), static_cast<DWORD>(toRead), &bytesRead, &ovRead);
        if (!readOk)
        {
            DWORD err = ::GetLastError();
            if (err == ERROR_IO_PENDING)
            {
                DWORD wr = ::WaitForSingleObject(ovRead.hEvent, INFINITE);
                if (wr == WAIT_OBJECT_0)
                {
                    BOOL res = ::GetOverlappedResult(m_handle, &ovRead, &bytesRead, FALSE);
                    ::CloseHandle(ovRead.hEvent);
                    if (!res)
                    {
                        m_lastError = ::GetLastError();
                        return -1;
                    }
                }
                else
                {
                    ::CancelIoEx(m_handle, &ovRead);
                    ::CloseHandle(ovRead.hEvent);
                    m_lastError = (wr == WAIT_TIMEOUT) ? WAIT_TIMEOUT : ::GetLastError();
                    return -1;
                }
            }
            else
            {
                ::CloseHandle(ovRead.hEvent);
                m_lastError = err;
                return -1;
            }
        }
        else
        {
            ::CloseHandle(ovRead.hEvent);
        }
        outBuffer.insert(outBuffer.end(), temp.begin(), temp.begin() + bytesRead);

        if (bytesRead == 0) break;
        if (bytesRead < toRead)
        {
            continue;
        }

        if (!::ClearCommError(m_handle, &errors, &comStat))
        {
            SetErrorFromLastError();
            return -1;
        }
        if (comStat.cbInQue == 0)
        {
            break;
        }
    }

    return static_cast<int>(outBuffer.size());
}

bool SerialCommAsynchronous::Purge()
{
    if (!IsOpen())
        return false;

    ResetError();
    if (!::PurgeComm(m_handle, PURGE_RXCLEAR | PURGE_TXCLEAR))
    {
        SetErrorFromLastError();
        return false;
    }
    return true;
}

bool SerialCommAsynchronous::ConfigPort(DWORD baudRate, BYTE byteSize, BYTE parity, BYTE stopBits, DWORD readTimeoutMs, DWORD writeTimeoutMs, bool setDtr, bool setRts)
{
    DCB dcb = {};
    dcb.DCBlength = sizeof(DCB);
    if (!::GetCommState(m_handle, &dcb))
    {
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
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;

    if (!::SetCommState(m_handle, &dcb))
    {
        SetErrorFromLastError();
        return false;
    }

    COMMTIMEOUTS timeouts = {};
    timeouts.ReadIntervalTimeout = readTimeoutMs; // non-zero to allow timeout
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = readTimeoutMs;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = writeTimeoutMs;

    if (!::SetCommTimeouts(m_handle, &timeouts))
    {
        SetErrorFromLastError();
        return false;
    }

    // Set buffer sizes (optional reasonable defaults)
    if (!::SetupComm(m_handle, 4096, 4096))
    {
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

bool SerialCommAsynchronous::StartReceiveThread(ReceiveCallback cb)
{
    if (!IsOpen())
        return false;

    if (m_hRecvThread) 
        return true; // already running

    m_callback = std::move(cb);
    if (!m_callback)
        return false;

    if (InterlockedCompareExchange(&m_recvRunFlag, 1, 0) != 0)
        return true;

    m_hRecvThread = ::CreateThread(nullptr, 0, RecvThreadProcStatic, this, 0, nullptr);

    return m_hRecvThread != nullptr;
}

void SerialCommAsynchronous::StopReceiveThread()
{
    if (InterlockedCompareExchange(&m_recvRunFlag, 0, 1) == 1)
    {
        if (m_hRecvThread)
        {
            ::WaitForSingleObject(m_hRecvThread, 1000);
            ::CloseHandle(m_hRecvThread);
            m_hRecvThread = nullptr;
        }
    }
}

DWORD WINAPI SerialCommAsynchronous::RecvThreadProcStatic(LPVOID lpParam)
{
    return reinterpret_cast<SerialCommAsynchronous*>(lpParam)->RecvThreadProc();
}

DWORD SerialCommAsynchronous::RecvThreadProc()
{
    unsigned char buf[512];         // 固定の長さを読み込む方式用
    std::vector<unsigned char> rx;  // バッファに入った分だけ読み込む方式用

    while (InterlockedCompareExchange(&m_recvRunFlag, 1, 1) == 1)
    {
        if (!IsOpen())
        {
            ::Sleep(50);
            continue;
        }

        // 固定の長さを読み込む方式
        //int n = ReadFixedOnRxEvent(buf, (DWORD)sizeof(buf));

        // バッファに入った分だけ読み込む方式
        rx.clear();
        int n = ReadAvailableOnRxEvent(rx);

        if (InterlockedCompareExchange(&m_recvRunFlag, 1, 1) != 1)
            break;

        if (n <= 0)
        {
            ::Sleep(5);
            continue;
        }

        // 固定の長さを読み込む方式
        //if (m_callback)
        //    m_callback(buf, (size_t)n);

        // バッファに入った分だけ読み込む方式
        m_callback(rx.data(), (size_t)rx.size());
    }
    return 0;
}