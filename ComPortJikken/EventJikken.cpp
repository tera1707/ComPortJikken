#include "framework.h"
#include "EventJikken.h"
#include "resource.h"
#include "EventJikkenUI.h"

// グローバル変数:
HINSTANCE hInst;

// このコード モジュールに含まれる関数の宣言を転送します:

BOOL CALLBACK MyDlgProc2(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    hInst = hInstance;

    //DialogBox(hInst, L"MyTestDlgBase_Main", NULL, (DLGPROC)MyDlgProc);    // 同期版
    DialogBox(hInst, L"MyTestDlgBase_Main2", NULL, (DLGPROC)MyDlgProc2);    // 非同期版

    return (int)0;
}