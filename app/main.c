/*
 * main.c — VhidLab 测试界面（纯 Win32）。
 * 按钮：左击 / 右击 / 中击 / 滚轮上 / 滚轮下 / 移动(+20,+20)。
 * 键盘：输入框敲一个字符发送；按钮发 Ctrl+A、Win+D 组合键演示。
 * 每个动作打开 \\.\VhidLab 用 DeviceIoControl 下发；底部状态栏显示结果。
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include "..\\include\\vhidlab_protocol.h"

#define ID_LCLICK 1001
#define ID_RCLICK 1002
#define ID_MCLICK 1003
#define ID_WUP    1004
#define ID_WDN    1005
#define ID_MOVE   1006
#define ID_KEYSEND 1007
#define ID_CTRLA  1008
#define ID_WIND   1009
#define ID_EDIT   1100
#define ID_STATUS 1101

static HWND g_edit, g_status;

static void SetStatus(const char *s) { SetWindowTextA(g_status, s); }

static BOOL SendIoctl(DWORD code, void *buf, DWORD len) {
    HANDLE h = CreateFileW(VHIDLAB_WIN32_NAME, GENERIC_READ | GENERIC_WRITE,
                           0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        char m[128]; sprintf_s(m, sizeof(m), "打开设备失败 err=%lu（驱动装了吗？）", GetLastError());
        SetStatus(m); return FALSE;
    }
    DWORD ret = 0;
    BOOL ok = DeviceIoControl(h, code, buf, len, NULL, 0, &ret, NULL);
    DWORD err = GetLastError();
    CloseHandle(h);
    if (!ok) { char m[128]; sprintf_s(m, sizeof(m), "IOCTL 失败 err=%lu", err); SetStatus(m); }
    else SetStatus("OK");
    return ok;
}

static void MouseClick(unsigned char btn) {
    VHIDLAB_MOUSE_IN m = {0}; m.buttons = btn; m.flags = VHIDLAB_FLAG_CLICK;
    SendIoctl(IOCTL_VHIDLAB_MOUSE_BUTTON, &m, sizeof(m));
}
static void MouseWheel(signed char w) {
    VHIDLAB_MOUSE_IN m = {0}; m.wheel = w;
    SendIoctl(IOCTL_VHIDLAB_MOUSE_WHEEL, &m, sizeof(m));
}
static void MouseMove(signed char dx, signed char dy) {
    VHIDLAB_MOUSE_IN m = {0}; m.dx = dx; m.dy = dy;
    SendIoctl(IOCTL_VHIDLAB_MOUSE_MOVE, &m, sizeof(m));
}
/* 简单 ASCII→HID usage：a-z=0x04.., 1-9=0x1E.., 其它返回 0 */
static unsigned char AsciiToUsage(char c) {
    if (c >= 'a' && c <= 'z') return (unsigned char)(0x04 + (c - 'a'));
    if (c >= 'A' && c <= 'Z') return (unsigned char)(0x04 + (c - 'A'));
    if (c >= '1' && c <= '9') return (unsigned char)(0x1E + (c - '1'));
    if (c == '0') return 0x27;
    if (c == ' ') return 0x2C;
    return 0;
}
static void KeyTap(unsigned char modifier, unsigned char usage) {
    VHIDLAB_KEY_IN k = {0}; k.modifier = modifier; k.usage = usage; k.flags = VHIDLAB_FLAG_CLICK;
    SendIoctl(IOCTL_VHIDLAB_KEY_EVENT, &k, sizeof(k));
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        int x = 20, y = 20, w = 120, h = 34, g = 10;
        CreateWindowA("BUTTON", "左击", WS_CHILD|WS_VISIBLE, x, y, w, h, hWnd, (HMENU)ID_LCLICK, NULL, NULL);
        CreateWindowA("BUTTON", "右击", WS_CHILD|WS_VISIBLE, x+w+g, y, w, h, hWnd, (HMENU)ID_RCLICK, NULL, NULL);
        CreateWindowA("BUTTON", "中击", WS_CHILD|WS_VISIBLE, x+2*(w+g), y, w, h, hWnd, (HMENU)ID_MCLICK, NULL, NULL);
        y += h+g;
        CreateWindowA("BUTTON", "滚轮上", WS_CHILD|WS_VISIBLE, x, y, w, h, hWnd, (HMENU)ID_WUP, NULL, NULL);
        CreateWindowA("BUTTON", "滚轮下", WS_CHILD|WS_VISIBLE, x+w+g, y, w, h, hWnd, (HMENU)ID_WDN, NULL, NULL);
        CreateWindowA("BUTTON", "移动+20,+20", WS_CHILD|WS_VISIBLE, x+2*(w+g), y, w, h, hWnd, (HMENU)ID_MOVE, NULL, NULL);
        y += h+g+10;
        CreateWindowA("STATIC", "键盘：", WS_CHILD|WS_VISIBLE, x, y+6, 50, h, hWnd, NULL, NULL, NULL);
        g_edit = CreateWindowA("EDIT", "a", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL, x+55, y, 60, h, hWnd, (HMENU)ID_EDIT, NULL, NULL);
        CreateWindowA("BUTTON", "发送字符", WS_CHILD|WS_VISIBLE, x+125, y, w, h, hWnd, (HMENU)ID_KEYSEND, NULL, NULL);
        y += h+g;
        CreateWindowA("BUTTON", "Ctrl+A", WS_CHILD|WS_VISIBLE, x, y, w, h, hWnd, (HMENU)ID_CTRLA, NULL, NULL);
        CreateWindowA("BUTTON", "Win+D", WS_CHILD|WS_VISIBLE, x+w+g, y, w, h, hWnd, (HMENU)ID_WIND, NULL, NULL);
        y += h+g+6;
        g_status = CreateWindowA("STATIC", "就绪。先装驱动，再点按钮。", WS_CHILD|WS_VISIBLE, x, y, 420, h, hWnd, (HMENU)ID_STATUS, NULL, NULL);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_LCLICK: MouseClick(VHIDLAB_BTN_LEFT); break;
        case ID_RCLICK: MouseClick(VHIDLAB_BTN_RIGHT); break;
        case ID_MCLICK: MouseClick(VHIDLAB_BTN_MIDDLE); break;
        case ID_WUP: MouseWheel(3); break;
        case ID_WDN: MouseWheel(-3); break;
        case ID_MOVE: MouseMove(20, 20); break;
        case ID_KEYSEND: { char b[8]={0}; GetWindowTextA(g_edit, b, sizeof(b));
            unsigned char u = AsciiToUsage(b[0]); if (u) KeyTap(0, u); else SetStatus("无法识别的字符"); break; }
        case ID_CTRLA: KeyTap(0x01, 0x04); break;      /* LCtrl + A */
        case ID_WIND:  KeyTap(0x08, AsciiToUsage('d')); break; /* LGui + D */
        }
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(hWnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    (void)hPrev; (void)lpCmd;
    WNDCLASSA wc = {0}; wc.lpfnWndProc = WndProc; wc.hInstance = hInst;
    wc.lpszClassName = "VhidLabWnd"; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    RegisterClassA(&wc);
    HWND hWnd = CreateWindowA("VhidLabWnd", "VhidLab 虚拟鼠标/键盘 测试",
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 470, 320, NULL, NULL, hInst, NULL);
    ShowWindow(hWnd, nShow); UpdateWindow(hWnd);
    MSG msg; while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return 0;
}
