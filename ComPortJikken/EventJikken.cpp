#include "framework.h"
#include "EventJikken.h"
#include "MyComPort.h"
#include "MyDeviceHandler.h"
#include "resource.h"

// グローバル変数:
HINSTANCE hInst;
static MyComPort g_ComPort; // COMポート管理用
// アプリ起動時のCOMポートコンボ初期選択インデックス（0=先頭）（COM3なら「2」を入れる）
static int g_DefaultComPortIndex = 2;
// アプリ起動時にコマンド入力欄へ設定する既定文字列
static const wchar_t* g_DefaultCommandString = L"AT\r\n";
// アプリ起動時にターゲットデバイスID欄へ設定する既定文字列
static const wchar_t* g_DefaultTargetDeviceId = L"BTHENUM\\Dev_0CA694033D59";

// ログ出力（IDC_LOG_LISTへ追加）
static void AppendLog(HWND hDlg, const wchar_t* text)
{
    if (!text) return;

    // 時刻(ミリ秒)付きで行を組み立てる
    SYSTEMTIME st{};
    ::GetLocalTime(&st);
    wchar_t line[512];
    swprintf_s(line, L"[%02u:%02u:%02u.%03u] %s",
        (unsigned)st.wHour,
        (unsigned)st.wMinute,
        (unsigned)st.wSecond,
        (unsigned)st.wMilliseconds,
        text);

    HWND hList = GetDlgItem(hDlg, IDC_LOG_LIST);
    if (!hList) return;
    SendMessage(hList, LB_INSERTSTRING, 0, reinterpret_cast<LPARAM>(line));
    LRESULT count = SendMessage(hList, LB_GETCOUNT, 0, 0);
    if (count > 0) {
        SendMessage(hList, LB_SETTOPINDEX, (WPARAM)(count - 1), 0);
    }
}

// 初期化処理を関数化
static BOOL OnInitDialog(HWND hDlg)
{
    // IDC_PORT_NO_COMBO に "COM1" ~ "COM9" を挿入
    HWND hCombo = GetDlgItem(hDlg, IDC_PORT_NO_COMBO);
    if (hCombo) {
        wchar_t buf[16];
        for (int i = 1; i <= 9; ++i) {
            swprintf_s(buf, L"COM%d", i);
            SendMessage(hCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(buf));
        }
        // グローバルで指定されたインデックスを選択（範囲外なら0）
        int selIndex = (g_DefaultComPortIndex >= 0 && g_DefaultComPortIndex < 9) ? g_DefaultComPortIndex : 0;
        SendMessage(hCombo, CB_SETCURSEL, (WPARAM)selIndex, 0);
    }
    // ターゲットデバイスIDをセット（グローバル既定値）
    SetDlgItemTextW(hDlg, IDC_TARGET_DEVICE, g_DefaultTargetDeviceId);
    // コマンドエディットに既定文字列をセット
    SetDlgItemTextW(hDlg, IDC_PORT_SEND_COMMAND_STRING, g_DefaultCommandString);
    return TRUE;
}

// 受信処理を関数化（受信バッファが空になるまで）
static void OnReceiveResponse(HWND hDlg)
{
    if (!g_ComPort.IsOpen()) {
        AppendLog(hDlg, L"受信できません。ポートが開いていません。");
        return;
    }

    AppendLog(hDlg, L"受信処理を開始します。");

    std::vector<unsigned char> rx;
    int total = g_ComPort.ReadAllAvailable(rx);
    if (total < 0) {
        wchar_t msg[128];
        swprintf_s(msg, L"受信に失敗しました。(Err=%lu)", g_ComPort.LastError());
        AppendLog(hDlg, msg);
        return;
    }
    if (total == 0) {
        AppendLog(hDlg, L"受信データはありません。");
        return;
    }
    // ログ用に可読変換（ANSI想定）
    std::string text(rx.begin(), rx.end());
    wchar_t wbuf[1024];
    int wlen = MultiByteToWideChar(CP_ACP, 0, text.c_str(), (int)text.size(), wbuf, (int)_countof(wbuf) - 1);
    if (wlen <= 0) {
        AppendLog(hDlg, L"受信データの文字コード変換に失敗しました。");
        return;
    }
    wbuf[wlen] = L'\0';

    AppendLog(hDlg, L"応答を受信しました。");
    AppendLog(hDlg, wbuf);
}

// ボタン押下時の処理を関数化
static void OnPortOpen(HWND hDlg)
{
    // コンボから選択中のCOMポート名を取得してオープン
    HWND hCombo = GetDlgItem(hDlg, IDC_PORT_NO_COMBO);
    if (!hCombo) return;

    LRESULT sel = SendMessage(hCombo, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR) {
        // 未選択ならグローバル指定に合わせる
        int selIndex = (g_DefaultComPortIndex >= 0 && g_DefaultComPortIndex < 9) ? g_DefaultComPortIndex : 0;
        SendMessage(hCombo, CB_SETCURSEL, (WPARAM)selIndex, 0);
        sel = selIndex;
    }

    wchar_t portName[32] = {};
    if (SendMessage(hCombo, CB_GETLBTEXT, (WPARAM)sel, (LPARAM)portName) == CB_ERR) {
        AppendLog(hDlg, L"ポート名の取得に失敗しました。");
        return;
    }

    if (!g_ComPort.Open(portName)) {
        wchar_t msg[128];
        swprintf_s(msg, L"ポートを開けませんでした。(Err=%lu)", g_ComPort.LastError());
        AppendLog(hDlg, msg);
    } else {
        AppendLog(hDlg, L"ポートをオープンしました。");
    }
}

static void OnPortClose(HWND hDlg)
{
    if (g_ComPort.IsOpen()) {
        g_ComPort.Close();
        AppendLog(hDlg, L"ポートをクローズしました。");
    } else {
        AppendLog(hDlg, L"ポートは開かれていません。");
    }
}

static void OnDeviceStop(HWND hDlg)
{
    wchar_t hwid[256] = {};
    if (GetDlgItemTextW(hDlg, IDC_TARGET_DEVICE, hwid, (int)_countof(hwid)) <= 0) {
        AppendLog(hDlg, L"停止対象のハードウェアIDを入力してください。");
        return;
    }

    if (MyDeviceHandler::DisableDeviceByHardwareId(hwid)) {
        AppendLog(hDlg, L"デバイスを停止しました。");
    } else {
        AppendLog(hDlg, L"デバイスの停止に失敗しました。");
    }
}

static void OnDeviceStart(HWND hDlg)
{
    wchar_t hwid[256] = {};
    if (GetDlgItemTextW(hDlg, IDC_TARGET_DEVICE, hwid, (int)_countof(hwid)) <= 0) {
        AppendLog(hDlg, L"有効化対象のハードウェアIDを入力してください。");
        return;
    }

    if (MyDeviceHandler::EnableDeviceByHardwareId(hwid)) {
        AppendLog(hDlg, L"デバイスを有効にしました。");
    } else {
        AppendLog(hDlg, L"デバイスの有効化に失敗しました。");
    }
}

static void OnPortCommandSend(HWND hDlg)
{
    if (!g_ComPort.IsOpen()) {
        AppendLog(hDlg, L"ポートが開いていません。");
        return;
    }
    wchar_t wcmd[512] = {};
    int wlen = GetDlgItemTextW(hDlg, IDC_PORT_SEND_COMMAND_STRING, wcmd, (int)_countof(wcmd));
    if (wlen <= 0) {
        AppendLog(hDlg, L"送信文字列が空です。");
        return;
    }
    // Wide -> ANSI (system code page)
    int alen = WideCharToMultiByte(CP_ACP, 0, wcmd, wlen, nullptr, 0, nullptr, nullptr);
    if (alen <= 0) {
        AppendLog(hDlg, L"文字列の変換に失敗しました。");
        return;
    }
    std::string acmd(alen, '\0');
    WideCharToMultiByte(CP_ACP, 0, wcmd, wlen, &acmd[0], alen, nullptr, nullptr);

    int written = g_ComPort.Write(acmd.data(), (DWORD)acmd.size());
    if (written < 0) {
        wchar_t msg[128];
        swprintf_s(msg, L"送信に失敗しました。(Err=%lu)", g_ComPort.LastError());
        AppendLog(hDlg, msg);
    } else {
        wchar_t msg[128];
        swprintf_s(msg, L"%d バイト送信しました。", written);
        AppendLog(hDlg, msg);
    }
}

// このコード モジュールに含まれる関数の宣言を転送します:
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
BOOL CALLBACK MyDlgProc(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    hInst = hInstance;

    DialogBox(hInst, L"MyTestDlgBase_Main", NULL, (DLGPROC)MyDlgProc);

    return (int)0;
}

// ダイアログプロシージャ
BOOL CALLBACK MyDlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG:
        return OnInitDialog(hDlg);
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDOK:
            EndDialog(hDlg, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        case IDC_PORT_OPEN:
            OnPortOpen(hDlg);
            return TRUE;
        case IDC_PORT_CLOSE:
            OnPortClose(hDlg);
            return TRUE;
        case IDC_PORT_COMMAND_SEND:
            OnPortCommandSend(hDlg);
            OnReceiveResponse(hDlg);
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