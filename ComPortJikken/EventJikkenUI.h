#pragma once
#include <Windows.h>

// Declarations for UI-related functions split from EventJikken.cpp

// Log append
void AppendLog(HWND hDlg, const wchar_t* text);

// Dialog helpers
BOOL OnInitDialog(HWND hDlg);

// Actions
bool OnReceiveResponse(HWND hDlg);
bool OnPortOpen(HWND hDlg);
void OnPortClose(HWND hDlg);
void OnDeviceStop(HWND hDlg);
void OnDeviceStart(HWND hDlg);
void OnPortCommandSend(HWND hDlg);

// Dialog procedure
BOOL CALLBACK MyDlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp);
