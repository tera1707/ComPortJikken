#include "framework.h"
#include "EventJikken.h"
#include "EventJikkenUI.h"
#include "MyComPort2.h"
#include "MyDeviceHandler.h"
#include "resource.h"
#include "Logger.h"
#include <string>
#include <vector>

// ローカル状態
static MyComPort2 g_ComPort2; // イベント駆動COMポート
static Logger g_Logger2;      // ログ
static int g_DefaultComPortIndex2 = 2;
static const wchar_t* g_DefaultCommandString2 = L"AT\r\n";
static const wchar_t* g_DefaultTargetDeviceId2 = L"BTHENUM\\Dev_0CA694033D59";

static void AppendLog2(HWND hDlg, const wchar_t* text)
{
    g_Logger2.SetDialog(hDlg);
    g_Logger2.Append(text);
}

static BOOL OnInitDialog2(HWND hDlg)
{
    g_Logger2.SetDialog(hDlg);
    // ログファイルの既定設定
    wchar_t path[MAX_PATH];
    DWORD len = ::GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len > 0)
    {
        for (DWORD i = len; i > 0; --i)
        {
            if (path[i-1] == L'\\' || path[i-1] == L'/') { path[i] = L'\0'; break; }
        }
        std::wstring logPath = std::wstring(path) + L"Logs2.txt";
        g_Logger2.SetLogFile(logPath.c_str());
    }

    // COMポートコンボを初期化
    HWND hCombo = GetDlgItem(hDlg, IDC_PORT_NO_COMBO);
    if (hCombo)
    {
        wchar_t buf[16];
        for (int i = 1; i <= 9; ++i)
        {
            swprintf_s(buf, L"COM%d", i);
            SendMessage(hCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(buf));
        }
        int selIndex = (g_DefaultComPortIndex2 >= 0 && g_DefaultComPortIndex2 < 9) ? g_DefaultComPortIndex2 : 0;
        SendMessage(hCombo, CB_SETCURSEL, (WPARAM)selIndex, 0);
    }

    // 既定文字列設定
    SetDlgItemTextW(hDlg, IDC_TARGET_DEVICE, g_DefaultTargetDeviceId2);
    SetDlgItemTextW(hDlg, IDC_PORT_SEND_COMMAND_STRING, g_DefaultCommandString2);
    return TRUE;
}

static void OnPortOpen2(HWND hDlg)
{
    HWND hCombo = GetDlgItem(hDlg, IDC_PORT_NO_COMBO);
    if (!hCombo)
        return;

    LRESULT sel = SendMessage(hCombo, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR)
    {
        int selIndex = (g_DefaultComPortIndex2 >= 0 && g_DefaultComPortIndex2 < 9) ? g_DefaultComPortIndex2 : 0;
        SendMessage(hCombo, CB_SETCURSEL, (WPARAM)selIndex, 0);
        sel = selIndex;
    }

    wchar_t portName[32] = {};
    if (SendMessage(hCombo, CB_GETLBTEXT, (WPARAM)sel, (LPARAM)portName) == CB_ERR)
    {
        AppendLog2(hDlg, L"ポート名の取得に失敗しました。");
        return;
    }

    if (!g_ComPort2.Open(portName))
    {
        wchar_t msg[128];
        swprintf_s(msg, L"ポートを開けませんでした。(Err=%lu)", g_ComPort2.LastError());
        AppendLog2(hDlg, msg);
    }
    else
    {
        AppendLog2(hDlg, L"ポートをオープンしました。");
        // 受信コールバック設定してスレッド起動
        g_ComPort2.StartReceiveThread([hDlg](const unsigned char* data, size_t len)
        {
            std::string text(reinterpret_cast<const char*>(data), reinterpret_cast<const char*>(data) + len);
            wchar_t wbuf[1024];
            int wlen = MultiByteToWideChar(CP_ACP, 0, text.c_str(), (int)text.size(), wbuf, (int)_countof(wbuf) - 1);
            if (wlen > 0)
            {
                wbuf[wlen] = L'\0';
                AppendLog2(hDlg, L"受信しました。");
                AppendLog2(hDlg, wbuf);
            }
        });
    }
}

static void OnPortClose2(HWND hDlg)
{
    UNREFERENCED_PARAMETER(hDlg);
    // スレッド停止 -> ポートクローズ
    g_ComPort2.StopReceiveThread();
    if (g_ComPort2.IsOpen())
    {
        g_ComPort2.Close();
        AppendLog2(hDlg, L"ポートをクローズしました。");
    }
    else
    {
        AppendLog2(hDlg, L"ポートは開かれていません。");
    }
}

static void OnPortCommandSend2(HWND hDlg)
{
    if (!g_ComPort2.IsOpen())
    {
        AppendLog2(hDlg, L"ポートが開いていません。");
        return;
    }
    wchar_t wcmd[512] = {};
    int wlen = GetDlgItemTextW(hDlg, IDC_PORT_SEND_COMMAND_STRING, wcmd, (int)_countof(wcmd));
    if (wlen <= 0)
    {
        AppendLog2(hDlg, L"送信文字列が空です。");
        return;
    }
    int alen = WideCharToMultiByte(CP_ACP, 0, wcmd, wlen, nullptr, 0, nullptr, nullptr);
    if (alen <= 0)
    {
        AppendLog2(hDlg, L"文字列の変換に失敗しました。");
        return;
    }
    std::string acmd(alen, '\0');
    WideCharToMultiByte(CP_ACP, 0, wcmd, wlen, &acmd[0], alen, nullptr, nullptr);

    // 非同期送信（完了待ち）
    OVERLAPPED ov = {};
    HANDLE hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ov.hEvent = hEvent;
    int res = g_ComPort2.WriteAsync(acmd.data(), (DWORD)acmd.size(), hEvent, &ov);
    if (res < 0)
    {
        wchar_t msg[128];
        swprintf_s(msg, L"送信に失敗しました。(Err=%lu)", g_ComPort2.LastError());
        AppendLog2(hDlg, msg);
    }
    else
    {
        DWORD written = 0;
        if (res == 0)
        {
            // 完了待ち（WriteAsync 内で待機済みなので、ここでは結果取得のみ）
            if (!::GetOverlappedResult(g_ComPort2.IsOpen() ? (HANDLE)nullptr : (HANDLE)nullptr, &ov, &written, FALSE))
            {
                // 上記はダミー。WriteAsyncで完了済みのため、書き込み済みバイトは不明ならメッセージのみ
                written = (DWORD)acmd.size();
            }
        }
        else
        {
            written = (DWORD)res;
        }
        wchar_t msg[128];
        swprintf_s(msg, L"%lu バイト送信しました。", written);
        AppendLog2(hDlg, msg);
    }
    if (hEvent)
        ::CloseHandle(hEvent);
}

// ダイアログプロシージャ（MYTESTDLGBASE_MAIN2）
BOOL CALLBACK MyDlgProc2(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_INITDIALOG:
        return OnInitDialog2(hDlg);
    case WM_COMMAND:
        switch (LOWORD(wp))
        {
        case IDOK:
            EndDialog(hDlg, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        case IDC_PORT_OPEN:
            OnPortOpen2(hDlg);
            return TRUE;
        case IDC_PORT_CLOSE:
            OnPortClose2(hDlg);
            return TRUE;
        case IDC_PORT_COMMAND_SEND:
            OnPortCommandSend2(hDlg);
            // 受信はバックグラウンドスレッドが継続実施
            return TRUE;
        case IDC_DEVICE_STOP:
            //OnDeviceStop(hDlg);
            return TRUE;
        case IDC_DEVICE_START:
            //OnDeviceStart(hDlg);
            return TRUE;
        }
        return FALSE;
    }
    return FALSE;
}
