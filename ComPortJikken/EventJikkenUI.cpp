#include "framework.h"
#include "EventJikken.h"
#include "EventJikkenUI.h"
#include "SerialCommSynchronous.h"
#include "MyDeviceHandler.h"
#include "resource.h"
#include "Logger.h"
#include <string>
#include <vector>
#include <thread>

// ローカル状態（このモジュール内に閉じる）
static SerialCommSynchronous g_ComPort; // COMポート管理用
static int g_DefaultComPortIndex = 3; // アプリ起動時のCOMポートコンボ初期選択インデックス
static const wchar_t* g_DefaultCommandString = L"AT"; // 既定送信文字列
static const wchar_t* g_DefaultTargetDeviceId = L"BTHENUM\\Dev_0CA694033D59"; // 既定ターゲットID
static Logger g_Logger; // UI + file logger (file path can be set later)

// ログ出力（IDC_LOG_LISTへ追加）
void AppendLog(HWND hDlg, const wchar_t* text)
{
    g_Logger.SetDialog(hDlg);
    g_Logger.Append(text);
}

// 初期化処理
BOOL OnInitDialog(HWND hDlg)
{
    g_Logger.SetDialog(hDlg);
    // Optional: set default log file in executable directory
    wchar_t path[MAX_PATH];
    DWORD len = ::GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len > 0)
    {
        // Replace filename with Logs.txt
        for (DWORD i = len; i > 0; --i)
        {
            if (path[i-1] == L'\\' || path[i-1] == L'/') { path[i] = L'\0'; break; }
        }
        std::wstring logPath = std::wstring(path) + L"Logs.txt";
        g_Logger.SetLogFile(logPath.c_str());
    }

    HWND hCombo = GetDlgItem(hDlg, IDC_PORT_NO_COMBO);
    if (hCombo)
    {
        wchar_t buf[16];
        for (int i = 1; i <= 9; ++i)
        {
            swprintf_s(buf, L"COM%d", i);
            SendMessage(hCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(buf));
        }
        int selIndex = (g_DefaultComPortIndex >= 0 && g_DefaultComPortIndex < 9) ? g_DefaultComPortIndex : 0;
        SendMessage(hCombo, CB_SETCURSEL, (WPARAM)selIndex, 0);
    }
    SetDlgItemTextW(hDlg, IDC_TARGET_DEVICE, g_DefaultTargetDeviceId);
    SetDlgItemTextW(hDlg, IDC_PORT_SEND_COMMAND_STRING, g_DefaultCommandString);
    return TRUE;
}

// ポートを開く
void OnPortOpen(HWND hDlg)
{
    HWND hCombo = GetDlgItem(hDlg, IDC_PORT_NO_COMBO);
    if (!hCombo) return;

    LRESULT sel = SendMessage(hCombo, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR)
    {
        int selIndex = (g_DefaultComPortIndex >= 0 && g_DefaultComPortIndex < 9) ? g_DefaultComPortIndex : 0;
        SendMessage(hCombo, CB_SETCURSEL, (WPARAM)selIndex, 0);
        sel = selIndex;
    }

    wchar_t portName[32] = {};
    if (SendMessage(hCombo, CB_GETLBTEXT, (WPARAM)sel, (LPARAM)portName) == CB_ERR)
    {
        AppendLog(hDlg, L"ポート名の取得に失敗しました。");
        return;
    }

    if (!g_ComPort.Open(portName))
    {
        wchar_t msg[128];
        swprintf_s(msg, L"ポートを開けませんでした。(Err=%lu)", g_ComPort.LastError());
        AppendLog(hDlg, msg);
    }
    else
    {
        AppendLog(hDlg, L"ポートをオープンしました。");
    }
}

static void OnPortClose(HWND hDlg)
{
    if (g_ComPort.IsOpen())
    {
        g_ComPort.Close();
        AppendLog(hDlg, L"ポートをクローズしました。");
    }
    else
    {
        AppendLog(hDlg, L"ポートは開かれていません。");
    }
}

static void OnDeviceStop(HWND hDlg)
{
    wchar_t hwid[256] = {};
    if (GetDlgItemTextW(hDlg, IDC_TARGET_DEVICE, hwid, (int)_countof(hwid)) <= 0)
    {
        AppendLog(hDlg, L"停止対象のハードウェアIDを入力してください。");
        return;
    }

    if (MyDeviceHandler::DisableDeviceByHardwareId(hwid))
    {
        AppendLog(hDlg, L"デバイスを停止しました。");
    }
    else
    {
        AppendLog(hDlg, L"デバイスの停止に失敗しました。");
    }
}

static void OnDeviceStart(HWND hDlg)
{
    wchar_t hwid[256] = {};
    if (GetDlgItemTextW(hDlg, IDC_TARGET_DEVICE, hwid, (int)_countof(hwid)) <= 0)
    {
        AppendLog(hDlg, L"有効化対象のハードウェアIDを入力してください。");
        return;
    }

    if (MyDeviceHandler::EnableDeviceByHardwareId(hwid))
    {
        AppendLog(hDlg, L"デバイスを有効にしました。");
    }
    else
    {
        AppendLog(hDlg, L"デバイスの有効化に失敗しました。");
    }
}

static void OnPortCommandSend(HWND hDlg)
{
    if (!g_ComPort.IsOpen())
    {
        AppendLog(hDlg, L"ポートが開いていません。");
        return;
    }
    wchar_t wcmd[512] = {};
    int wlen = GetDlgItemTextW(hDlg, IDC_PORT_SEND_COMMAND_STRING, wcmd, (int)_countof(wcmd));
    if (wlen <= 0)
    {
        AppendLog(hDlg, L"送信文字列が空です。");
        return;
    }
    
    std::wstring wcmd2 = std::wstring(wcmd) + L"\r";

    int written = g_ComPort.Write(wcmd2.data(), (DWORD)wcmd2.size());
    if (written < 0)
    {
        wchar_t msg[128];
        swprintf_s(msg, L"送信に失敗しました。(Err=%lu)", g_ComPort.LastError());
        AppendLog(hDlg, msg);
    }
    else
    {
        AppendLog(hDlg, (std::wstring(L"[send]") + wcmd2).c_str());
    }
}

// 受信処理（受信バッファが空になるまで）
void OnReceiveResponse(HWND hDlg)
{
    if (!g_ComPort.IsOpen())
    {
        AppendLog(hDlg, L"受信できません。ポートが開いていません。");
        return;
    }

    //AppendLog(hDlg, L"受信処理を開始します。");

    std::vector<unsigned char> rx;
    int total = g_ComPort.ReadAllAvailable(rx);
    if (total < 0)
    {
        wchar_t msg[128];
        swprintf_s(msg, L"受信に失敗しました。(Err=%lu)", g_ComPort.LastError());
        AppendLog(hDlg, msg);
        return;
    }
    if (total == 0)
    {
        //AppendLog(hDlg, L"受信データはありません。");
        return;
    }

    std::wstring text(reinterpret_cast<const char*>(rx.data()), reinterpret_cast<const char*>(rx.data()) + rx.size());

    AppendLog(hDlg, (std::wstring(L"[recv]") + text).c_str());
}

// ダイアログプロシージャ
BOOL CALLBACK MyDlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp)
{
    static std::thread recieveThread;
    static bool isRecieving = false;
    static bool isContinuousSending = false;

    switch (msg)
    {
    case WM_INITDIALOG:
        return OnInitDialog(hDlg);
    case WM_CLOSE:
        isRecieving = false;
        isContinuousSending = false;
        if (recieveThread.joinable())
            recieveThread.join();
        return FALSE;
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
            OnPortOpen(hDlg);

            // 受信スレッド開始
            isRecieving = true;

            recieveThread = std::thread([hDlg]()
            {
                while (isRecieving)
                {
                    OnReceiveResponse(hDlg);
                }
            });

            return TRUE;
        case IDC_PORT_CLOSE:
            isRecieving = false;

            if (recieveThread.joinable())
                recieveThread.join();

            OnPortClose(hDlg);
            return TRUE;
        case IDC_PORT_COMMAND_SEND:
            // コマンドを送信した後に
            OnPortCommandSend(hDlg);
            // 応答を期待する
            OnReceiveResponse(hDlg);
            return TRUE;
        case IDC_PORT_CONTINUOUS_SEND:

            if (isContinuousSending)
                return TRUE;

            isContinuousSending = true;
            AppendLog(hDlg, L"連続送信開始");

            std::thread([hDlg]()
            {
                while (isContinuousSending)
                {
                    OnPortOpen(hDlg);
                    //::Sleep(1000);
                    OnPortCommandSend(hDlg);
                    //::Sleep(1000);
                    OnReceiveResponse(hDlg);
                    //::Sleep(1000);
                    OnPortClose(hDlg);
                    //::Sleep(1000);
                }
                AppendLog(hDlg, L"連続送信終了");
            }).detach();

            return TRUE;
        case IDC_PORT_CONTINUOUS_STOP:
            isContinuousSending = false;
            return TRUE;
        case IDC_DEVICE_STOP:
            OnDeviceStop(hDlg);
            return TRUE;
        case IDC_DEVICE_START:
            OnDeviceStart(hDlg);
            return TRUE;
        }
        return FALSE;
    }
    return FALSE;
}
