/*
 * main.c — VhidLab 测试台（纯 Win32）。
 *
 * 设计要点（解决"注入看不见效果"）：
 *  1) 注入前倒计时 3 秒：点注入按钮后不立刻下发，留时间把光标 / 焦点挪到靶上，
 *     避免"光标和焦点停在按钮上 → 注入全打在自己身上"。
 *  2) 事件回显区（靶）：自绘子窗口，记录所有鼠标 / 键盘消息 + 事件计数，
 *     带自己的垂直滚动条，支持滚轮与拖动，新日志自动跟随到底。
 *  3) 命中判定：光标在回显区内=命中；在控制面板其他位置=取消注入（防自激递归）；
 *     在面板外=照发，但状态栏红字提示"未命中靶"。
 *  4) 点击类动作在 app 侧拆成 down / Sleep(25) / up 两次 IOCTL，
 *     规避"按下与抬起同帧下发被吞"的隐患（不改驱动）。
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "..\\include\\vhidlab_protocol.h"

/* ---------- 控件 ID ---------- */
#define ID_LCLICK  1001
#define ID_RCLICK  1002
#define ID_MCLICK  1003
#define ID_WUP     1004
#define ID_WDN     1005
#define ID_MOVE    1006
#define ID_LDOWN   1007
#define ID_LUP     1008
#define ID_CTRLA   1009
#define ID_WIND    1010
#define ID_KEYSEND 1011
#define ID_EDIT    1100
#define ID_STATUS  1101
#define ID_CURSOR  1102
#define ID_ECHO    1200

/* 回显区右键菜单 */
#define IDM_CLEAR  1301
#define IDM_COPY   1302

/* 定时器 */
#define IDT_COUNTDOWN 1
#define IDT_CURSOR    2

/* 倒计时秒数（用户定：3s） */
#define COUNTDOWN_SEC 3
#define ECHO_CLASS    "VhidLabEcho"

/* 日志缓冲 */
#define LOG_MAX   512
#define LOG_LINE  140
#define LINE_H    16

/* ---------- 全局状态 ---------- */
static HWND g_hwnd, g_edit, g_status, g_cursor, g_echo;
static BOOL g_statusErr = FALSE;

/* 回显区 */
static char g_lines[LOG_MAX][LOG_LINE];
static int  g_lineCount  = 0;
static int  g_topLine    = 0;
static BOOL g_followTail = TRUE;
static int  g_eventCount = 0;
static HFONT g_mono = NULL;

/* ---------- 待注入动作 ---------- */
typedef enum { ACT_NONE, ACT_MOUSE_ONE, ACT_MOUSE_CLICK, ACT_KEY_ONE, ACT_KEY_TAP } ACT_KIND;

typedef struct {
    BOOL     active;
    int      remain;
    ACT_KIND kind;
    UINT     code;
    char     label[40];
    VHIDLAB_MOUSE_IN m;
    VHIDLAB_KEY_IN   k;
} PENDING_STATE;

static PENDING_STATE g_pend;

/* ---------- 工具 ---------- */
static void SetStatusText(BOOL isErr, const char *text)
{
    g_statusErr = isErr;
    SetWindowTextA(g_status, text);
    InvalidateRect(g_status, NULL, TRUE);
}

static void SetStatusF(BOOL isErr, const char *fmt, ...)
{
    char buf[220];
    va_list ap; va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    SetStatusText(isErr, buf);
}

/* ---------- 回显区：日志与滚动 ---------- */
static int EchoVisibleLines(void)
{
    RECT rc;
    if (!g_echo) return 1;
    GetClientRect(g_echo, &rc);
    int v = rc.bottom / LINE_H;
    return v > 0 ? v : 1;
}

static void EchoUpdateScroll(void)
{
    if (!g_echo) return;
    int vis = EchoVisibleLines();
    int maxTop = g_lineCount > vis ? g_lineCount - vis : 0;

    if (g_followTail) g_topLine = maxTop;
    if (g_topLine > maxTop) g_topLine = maxTop;
    if (g_topLine < 0) g_topLine = 0;

    SCROLLINFO si;
    ZeroMemory(&si, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin   = 0;
    si.nMax   = g_lineCount > 0 ? g_lineCount - 1 : 0;
    si.nPage  = (UINT)vis;
    si.nPos   = g_topLine;
    SetScrollInfo(g_echo, SB_VERT, &si, TRUE);
}

static void EchoLog(const char *fmt, ...)
{
    if (g_lineCount >= LOG_MAX) {
        int drop = LOG_MAX / 4;
        memmove(g_lines, g_lines + drop, (size_t)(LOG_MAX - drop) * LOG_LINE);
        g_lineCount -= drop;
        g_topLine = g_topLine > drop ? g_topLine - drop : 0;
    }

    SYSTEMTIME st; GetLocalTime(&st);
    char body[100];
    va_list ap; va_start(ap, fmt);
    _vsnprintf_s(body, sizeof(body), _TRUNCATE, fmt, ap);
    va_end(ap);

    sprintf_s(g_lines[g_lineCount], LOG_LINE, "%02d:%02d:%02d.%03d  %s",
              st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, body);
    g_lineCount++;
    g_eventCount++;

    EchoUpdateScroll();
    if (g_echo) InvalidateRect(g_echo, NULL, TRUE);
}

/* ---------- 回显区窗口过程 ---------- */
static void LogMouse(const char *tag)
{
    POINT pt; GetCursorPos(&pt);
    EchoLog("%-9s (%ld,%ld)", tag, pt.x, pt.y);
}

static LRESULT CALLBACK EchoProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_mono = CreateFontA(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             FIXED_PITCH | FF_MODERN, "Consolas");
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        FillRect(dc, &rc, GetSysColorBrush(COLOR_WINDOW));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(30, 30, 30));
        HFONT old = (HFONT)SelectObject(dc, g_mono);

        int vis = rc.bottom / LINE_H;
        int i;
        for (i = 0; i < vis; i++) {
            int idx = g_topLine + i;
            if (idx >= g_lineCount) break;
            RECT r; r.left = 6; r.top = i * LINE_H; r.right = rc.right - 4; r.bottom = (i + 1) * LINE_H;
            DrawTextA(dc, g_lines[idx], -1, &r, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        }
        SelectObject(dc, old);
        EndPaint(h, &ps);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        int notches = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
        int vis = EchoVisibleLines();
        int maxTop = g_lineCount > vis ? g_lineCount - vis : 0;

        g_topLine -= notches * 3;
        if (g_topLine < 0) g_topLine = 0;
        if (g_topLine > maxTop) g_topLine = maxTop;
        g_followTail = (g_topLine >= maxTop);

        EchoLog("WHEEL     delta=%d", GET_WHEEL_DELTA_WPARAM(wp));
        EchoUpdateScroll();
        InvalidateRect(h, NULL, TRUE);
        return 0;
    }

    case WM_VSCROLL: {
        int vis = EchoVisibleLines();
        int maxTop = g_lineCount > vis ? g_lineCount - vis : 0;
        SCROLLINFO si;
        ZeroMemory(&si, sizeof(si));
        si.cbSize = sizeof(si); si.fMask = SIF_ALL;
        GetScrollInfo(h, SB_VERT, &si);

        int pos = si.nPos;
        switch (LOWORD(wp)) {
        case SB_LINEUP:    pos -= 1; break;
        case SB_LINEDOWN:  pos += 1; break;
        case SB_PAGEUP:    pos -= (int)si.nPage; break;
        case SB_PAGEDOWN:  pos += (int)si.nPage; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: pos = si.nTrackPos; break;
        case SB_TOP:       pos = 0; break;
        case SB_BOTTOM:    pos = maxTop; break;
        }
        if (pos < 0) pos = 0;
        if (pos > maxTop) pos = maxTop;
        g_topLine = pos;
        g_followTail = (pos >= maxTop);

        EchoUpdateScroll();
        InvalidateRect(h, NULL, TRUE);
        return 0;
    }

    /* 鼠标：DOWN 记日志后走 DefWindowProc —— 它会 SetCapture / SetFocus，
       并在右键抬起后自动生成 WM_CONTEXTMENU，正好用来验证"右击弹窗" */
    case WM_LBUTTONDOWN: LogMouse("L-DOWN"); SetFocus(h); break;
    case WM_LBUTTONUP:   LogMouse("L-UP");   break;
    case WM_RBUTTONDOWN: LogMouse("R-DOWN"); SetFocus(h); break;
    case WM_RBUTTONUP:   LogMouse("R-UP");   break;
    case WM_MBUTTONDOWN: LogMouse("M-DOWN"); SetFocus(h); break;
    case WM_MBUTTONUP:   LogMouse("M-UP");   break;
    case WM_LBUTTONDBLCLK: LogMouse("L-DBL"); SetFocus(h); return 0;
    case WM_RBUTTONDBLCLK: LogMouse("R-DBL"); return 0;

    case WM_MOUSEMOVE: {
        static POINT last = { -9999, -9999 };
        POINT pt; GetCursorPos(&pt);
        if (abs(pt.x - last.x) >= 20 || abs(pt.y - last.y) >= 20) {
            last = pt;
            EchoLog("%-9s (%ld,%ld)", "MOVE", pt.x, pt.y);
        }
        break;   /* 交给 DefWindowProc 去设置光标形状 */
    }

    case WM_CONTEXTMENU: {
        EchoLog("--- 右键菜单弹出（靶命中，说明注入生效）");
        HMENU m = CreatePopupMenu();
        AppendMenuA(m, MF_STRING, IDM_CLEAR, "清空日志(&C)");
        AppendMenuA(m, MF_STRING, IDM_COPY,  "复制最后一行(&L)");
        POINT pt;
        if ((INT_PTR)lp == -1) { GetCursorPos(&pt); }
        else { pt.x = (short)LOWORD(lp); pt.y = (short)HIWORD(lp); }
        SetForegroundWindow(g_hwnd);
        TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, h, NULL);
        DestroyMenu(m);
        PostMessage(g_hwnd, WM_NULL, 0, 0);
        return 0;
    }

    case WM_KEYDOWN:
        EchoLog("%-9s vk=0x%02X", "KEYDOWN", (unsigned)wp);
        return 0;
    case WM_KEYUP:
        EchoLog("%-9s vk=0x%02X", "KEYUP", (unsigned)wp);
        return 0;
    case WM_CHAR: {
        if (wp >= 32 && wp < 127) EchoLog("%-9s '%c' (0x%02X)", "CHAR", (int)wp, (unsigned)wp);
        else                      EchoLog("%-9s 0x%02X", "CHAR", (unsigned)wp);
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_CLEAR:
            g_lineCount = 0; g_topLine = 0; g_followTail = TRUE;
            EchoLog("--- 日志已清空");
            break;
        case IDM_COPY:
            if (g_lineCount > 0 && OpenClipboard(h)) {
                EmptyClipboard();
                size_t n = strlen(g_lines[g_lineCount - 1]) + 1;
                HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, n);
                if (hg) {
                    memcpy(GlobalLock(hg), g_lines[g_lineCount - 1], n);
                    GlobalUnlock(hg);
                    SetClipboardData(CF_TEXT, hg);
                }
                CloseClipboard();
                EchoLog("--- 最后一行已复制到剪贴板");
            }
            break;
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;   /* WM_PAINT 里自己填背景，避免闪烁 */
    }
    return DefWindowProcA(h, msg, wp, lp);
}

/* ---------- 注入 ---------- */
static BOOL SendIoctlRaw(UINT code, const void *buf, DWORD len, char *err, size_t cap)
{
    HANDLE h = CreateFileW(VHIDLAB_WIN32_NAME, GENERIC_READ | GENERIC_WRITE,
                           0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        sprintf_s(err, cap, "打开设备失败 err=%lu（驱动装了吗？）", GetLastError());
        return FALSE;
    }
    DWORD ret = 0;
    BOOL ok = DeviceIoControl(h, code, (LPVOID)buf, len, NULL, 0, &ret, NULL);
    DWORD e = GetLastError();
    CloseHandle(h);
    if (!ok) sprintf_s(err, cap, "IOCTL 失败 err=%lu", e);
    return ok;
}

/* 光标是不是落在回显区内 */
static BOOL CursorOnEcho(void)
{
    POINT pt; GetCursorPos(&pt);
    HWND w = WindowFromPoint(pt);
    while (w) {
        if (w == g_echo) return TRUE;
        w = GetParent(w);
    }
    return FALSE;
}

/* 光标是不是还在控制面板上（任何一块），用于拦截自激递归 */
static BOOL CursorOnPanel(void)
{
    POINT pt; GetCursorPos(&pt);
    HWND w = WindowFromPoint(pt);
    while (w) {
        if (w == g_hwnd) return TRUE;
        w = GetParent(w);
    }
    return FALSE;
}

static void RestoreTitle(void)
{
    SetWindowTextA(g_hwnd, "VhidLab 测试台（虚拟鼠标 / 键盘）");
}

static void ExecPending(void)
{
    char err[160] = {0};
    BOOL ok = TRUE;
    VHIDLAB_MOUSE_IN m;
    VHIDLAB_KEY_IN   k;

    switch (g_pend.kind) {
    case ACT_MOUSE_ONE:
        ok = SendIoctlRaw(g_pend.code, &g_pend.m, sizeof(g_pend.m), err, sizeof(err));
        break;

    case ACT_MOUSE_CLICK:            /* 按下 → 停 25ms → 抬起，分两条下发 */
        m = g_pend.m; m.flags = 0; m.is_down = 1;
        ok = SendIoctlRaw(g_pend.code, &m, sizeof(m), err, sizeof(err));
        Sleep(25);
        m = g_pend.m; m.flags = 0; m.is_down = 0; m.buttons = 0;
        if (ok) ok = SendIoctlRaw(g_pend.code, &m, sizeof(m), err, sizeof(err));
        break;

    case ACT_KEY_ONE:
        ok = SendIoctlRaw(g_pend.code, &g_pend.k, sizeof(g_pend.k), err, sizeof(err));
        break;

    case ACT_KEY_TAP:
        k = g_pend.k; k.flags = 0; k.is_down = 1;
        ok = SendIoctlRaw(g_pend.code, &k, sizeof(k), err, sizeof(err));
        Sleep(25);
        k = g_pend.k; k.flags = 0; k.is_down = 0; k.usage = 0;
        if (ok) ok = SendIoctlRaw(g_pend.code, &k, sizeof(k), err, sizeof(err));
        break;

    default:
        return;
    }

    if (!ok) {
        SetStatusF(TRUE, "%s 注入失败：%s", g_pend.label, err);
    } else if (CursorOnEcho()) {
        SetStatusF(FALSE, "已注入 %s · 命中靶（回显区已收到）", g_pend.label);
    } else {
        SetStatusF(TRUE, "已注入 %s · 未命中靶（光标不在回显区，请到目标窗口查看效果）", g_pend.label);
    }
    g_pend.active = FALSE;
}

static void StartInject(ACT_KIND kind, UINT code, const char *label,
                        const VHIDLAB_MOUSE_IN *m, const VHIDLAB_KEY_IN *k)
{
    ZeroMemory(&g_pend, sizeof(g_pend));
    g_pend.active = TRUE;
    g_pend.kind   = kind;
    g_pend.code   = code;
    g_pend.remain = COUNTDOWN_SEC;
    strcpy_s(g_pend.label, sizeof(g_pend.label), label);
    if (m) g_pend.m = *m;
    if (k) g_pend.k = *k;

    SetStatusF(FALSE, "%s：%d 秒后注入 —— 请把光标 / 焦点挪到靶上", label, g_pend.remain);
    SetTimer(g_hwnd, IDT_COUNTDOWN, 1000, NULL);
}

/* ---------- 面板 ---------- */
typedef struct { int id; const char *text; } BTN_DEF;

static const BTN_DEF g_buttons[] = {
    { ID_LCLICK, "左击" },   { ID_RCLICK, "右击" },
    { ID_MCLICK, "中击" },   { ID_WUP,    "滚轮上" },
    { ID_WDN,    "滚轮下" }, { ID_MOVE,   "移动+20" },
    { ID_LDOWN,  "左键按下" },{ ID_LUP,   "左键抬起" },
    { ID_CTRLA,  "Ctrl+A" }, { ID_WIND,   "Win+D" }
};
#define NBUTTONS (int)(sizeof(g_buttons) / sizeof(g_buttons[0]))

/* ASCII → HID usage（需要 Shift 的字符通过 mods 带出） */
static unsigned char AsciiToUsage(char c, unsigned char *mods)
{
    *mods = 0;
    if (c >= 'a' && c <= 'z') return (unsigned char)(0x04 + (c - 'a'));
    if (c >= 'A' && c <= 'Z') { *mods = 0x02; return (unsigned char)(0x04 + (c - 'A')); }
    if (c >= '1' && c <= '9') return (unsigned char)(0x1E + (c - '1'));
    switch (c) {
    case '0': return 0x27;
    case ' ': return 0x2C;
    case '\r': case '\n': return 0x28;
    case '\t': return 0x2B;
    case '!': *mods = 0x02; return 0x1E;
    case '@': *mods = 0x02; return 0x1F;
    case '#': *mods = 0x02; return 0x20;
    case '$': *mods = 0x02; return 0x21;
    case '%': *mods = 0x02; return 0x22;
    case '^': *mods = 0x02; return 0x23;
    case '&': *mods = 0x02; return 0x24;
    case '*': *mods = 0x02; return 0x25;
    case '(': *mods = 0x02; return 0x26;
    case ')': *mods = 0x02; return 0x27;
    case '_': *mods = 0x02; return 0x2D;
    case '+': *mods = 0x02; return 0x2E;
    case '{': *mods = 0x02; return 0x2F;
    case '}': *mods = 0x02; return 0x30;
    case '|': *mods = 0x02; return 0x31;
    case ':': *mods = 0x02; return 0x33;
    case '"': *mods = 0x02; return 0x34;
    case '~': *mods = 0x02; return 0x35;
    case '<': *mods = 0x02; return 0x36;
    case '>': *mods = 0x02; return 0x37;
    case '?': *mods = 0x02; return 0x38;
    case '-': return 0x2D;
    case '=': return 0x2E;
    case '[': return 0x2F;
    case ']': return 0x30;
    case '\\': return 0x31;
    case ';': return 0x33;
    case '\'': return 0x34;
    case '`': return 0x35;
    case ',': return 0x36;
    case '.': return 0x37;
    case '/': return 0x38;
    }
    return 0;
}

static void DoMouseClick(unsigned char btn, const char *label)
{
    VHIDLAB_MOUSE_IN m; ZeroMemory(&m, sizeof(m));
    m.buttons = btn;
    StartInject(ACT_MOUSE_CLICK, IOCTL_VHIDLAB_MOUSE_BUTTON, label, &m, NULL);
}

static void DoMouseOne(UINT code, signed char dx, signed char dy, signed char wheel,
                       unsigned char buttons, unsigned char isDown, const char *label)
{
    VHIDLAB_MOUSE_IN m; ZeroMemory(&m, sizeof(m));
    m.dx = dx; m.dy = dy; m.wheel = wheel; m.buttons = buttons; m.is_down = isDown;
    StartInject(ACT_MOUSE_ONE, code, label, &m, NULL);
}

static void DoKeyTap(unsigned char modifier, unsigned char usage, const char *label)
{
    VHIDLAB_KEY_IN k; ZeroMemory(&k, sizeof(k));
    k.modifier = modifier; k.usage = usage;
    StartInject(ACT_KEY_TAP, IOCTL_VHIDLAB_KEY_EVENT, label, NULL, &k);
}

/* 控件布局（窗口不可缩放，按客户区尺寸算一次） */
static void LayoutControls(HWND hWnd)
{
    RECT rc; GetClientRect(hWnd, &rc);
    const int M = 12, BW = 92, BH = 32, G = 6;
    int i;

    for (i = 0; i < NBUTTONS; i++) {
        int row = i / 2, col = i % 2;
        HWND b = GetDlgItem(hWnd, g_buttons[i].id);
        MoveWindow(b, M + col * (BW + G), M + row * (BH + G), BW, BH, TRUE);
    }

    int y = M + 5 * (BH + G);                       /* 字符行 */
    MoveWindow(g_edit, M + 36, y + 4, 54, BH - 8, TRUE);
    MoveWindow(GetDlgItem(hWnd, ID_KEYSEND), M + BW + G, y, BW, BH, TRUE);

    int echoX = M + 2 * BW + G + M;
    int panelBottom = y + BH;
    MoveWindow(g_echo, echoX, M, rc.right - echoX - M, panelBottom - M, TRUE);

    int sy = panelBottom + M;
    MoveWindow(g_status, M, sy + 3, rc.right - 2 * M - 200, 20, TRUE);
    MoveWindow(g_cursor, rc.right - M - 190, sy + 3, 190, 20, TRUE);
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        int i;
        for (i = 0; i < NBUTTONS; i++) {
            CreateWindowA("BUTTON", g_buttons[i].text, WS_CHILD | WS_VISIBLE,
                          0, 0, 10, 10, hWnd, (HMENU)(INT_PTR)g_buttons[i].id, NULL, NULL);
        }
        CreateWindowA("STATIC", "字符", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, hWnd, NULL, NULL, NULL);
        g_edit = CreateWindowA("EDIT", "a",
                               WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                               0, 0, 10, 10, hWnd, (HMENU)(INT_PTR)ID_EDIT, NULL, NULL);
        CreateWindowA("BUTTON", "发送", WS_CHILD | WS_VISIBLE,
                      0, 0, 10, 10, hWnd, (HMENU)(INT_PTR)ID_KEYSEND, NULL, NULL);

        g_echo = CreateWindowExA(0, ECHO_CLASS, "",
                                 WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_BORDER,
                                 0, 0, 10, 10, hWnd, (HMENU)(INT_PTR)ID_ECHO, NULL, NULL);

        g_status = CreateWindowA("STATIC", "就绪：点注入按钮后有 3 秒，把光标 / 焦点挪到靶上",
                                 WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, hWnd, (HMENU)(INT_PTR)ID_STATUS, NULL, NULL);
        g_cursor = CreateWindowA("STATIC", "光标 -, -",
                                 WS_CHILD | WS_VISIBLE | SS_RIGHT, 0, 0, 10, 10, hWnd, (HMENU)(INT_PTR)ID_CURSOR, NULL, NULL);

        LayoutControls(hWnd);

        EchoLog("回显区就绪 —— 光标落在本区内时，鼠标事件才会到达这里");
        EchoLog("要让键盘事件（字符）进本区，先点一下这里取得焦点");
        EchoLog("右击本区可弹出菜单；滚轮可滚动本日志");

        SetTimer(hWnd, IDT_CURSOR, 150, NULL);
        return 0;
    }

    case WM_SIZE:
        LayoutControls(hWnd);
        return 0;

    case WM_TIMER:
        if (wp == IDT_CURSOR) {
            POINT pt; GetCursorPos(&pt);
            char b[80];
            sprintf_s(b, sizeof(b), "光标 %ld, %ld  ·  事件 %d", pt.x, pt.y, g_eventCount);
            SetWindowTextA(g_cursor, b);
        } else if (wp == IDT_COUNTDOWN) {
            g_pend.remain--;
            if (g_pend.remain > 0) {
                SetStatusF(FALSE, "%s：%d 秒后注入 —— 请把光标 / 焦点挪到靶上", g_pend.label, g_pend.remain);
                char t[120];
                sprintf_s(t, sizeof(t), "VhidLab 测试台 —— %d 秒后注入：%s", g_pend.remain, g_pend.label);
                SetWindowTextA(hWnd, t);
            } else {
                KillTimer(hWnd, IDT_COUNTDOWN);
                RestoreTitle();
                if (CursorOnPanel() && !CursorOnEcho()) {
                    SetStatusF(TRUE, "已取消 %s：光标仍停在控制面板上（避免注入打到自己）", g_pend.label);
                    g_pend.active = FALSE;
                } else {
                    ExecPending();
                }
            }
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_LCLICK: DoMouseClick(VHIDLAB_BTN_LEFT,   "左击"); break;
        case ID_RCLICK: DoMouseClick(VHIDLAB_BTN_RIGHT,  "右击"); break;
        case ID_MCLICK: DoMouseClick(VHIDLAB_BTN_MIDDLE, "中击"); break;
        case ID_WUP:    DoMouseOne(IOCTL_VHIDLAB_MOUSE_WHEEL, 0, 0,  3, 0, 0, "滚轮上"); break;
        case ID_WDN:    DoMouseOne(IOCTL_VHIDLAB_MOUSE_WHEEL, 0, 0, -3, 0, 0, "滚轮下"); break;
        case ID_MOVE:   DoMouseOne(IOCTL_VHIDLAB_MOUSE_MOVE, 20, 20, 0, 0, 0, "移动+20"); break;
        case ID_LDOWN:  DoMouseOne(IOCTL_VHIDLAB_MOUSE_BUTTON, 0, 0, 0, VHIDLAB_BTN_LEFT, 1, "左键按下"); break;
        case ID_LUP:    DoMouseOne(IOCTL_VHIDLAB_MOUSE_BUTTON, 0, 0, 0, 0, 0, "左键抬起"); break;
        case ID_CTRLA:  DoKeyTap(0x01, 0x04, "Ctrl+A"); break;
        case ID_WIND:   DoKeyTap(0x08, 0x07, "Win+D"); break;
        case ID_KEYSEND: {
            char b[8] = { 0 };
            GetWindowTextA(g_edit, b, sizeof(b));
            if (!b[0]) { SetStatusF(TRUE, "输入框是空的"); break; }
            unsigned char mods = 0;
            unsigned char u = AsciiToUsage(b[0], &mods);
            if (!u) { SetStatusF(TRUE, "无法识别的字符：%c", b[0]); break; }
            char lb[32];
            sprintf_s(lb, sizeof(lb), "字符 '%c'", b[0]);
            DoKeyTap(mods, u, lb);
            break;
        }
        }
        return 0;

    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        HWND ctl = (HWND)lp;
        /* 用不透明背景，否则倒计时 / 坐标频繁刷新会留下拖影 */
        SetBkMode(dc, OPAQUE);
        SetBkColor(dc, GetSysColor(COLOR_WINDOW));
        if (ctl == g_status && g_statusErr) SetTextColor(dc, RGB(190, 0, 0));
        else                                SetTextColor(dc, RGB(32, 32, 32));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }

    case WM_DESTROY:
        KillTimer(hWnd, IDT_COUNTDOWN);
        KillTimer(hWnd, IDT_CURSOR);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hWnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
    (void)hPrev; (void)lpCmd;

    WNDCLASSA we = { 0 };
    we.lpfnWndProc   = EchoProc;
    we.hInstance     = hInst;
    we.lpszClassName = ECHO_CLASS;
    we.hCursor       = LoadCursor(NULL, IDC_ARROW);
    we.hbrBackground = NULL;
    we.style         = CS_DBLCLKS;
    RegisterClassA(&we);

    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = "VhidLabWnd";
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassA(&wc);

    HWND hWnd = CreateWindowA("VhidLabWnd", "VhidLab 测试台（虚拟鼠标 / 键盘）",
                              WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
                              CW_USEDEFAULT, CW_USEDEFAULT, 700, 350,
                              NULL, NULL, hInst, NULL);
    g_hwnd = hWnd;
    ShowWindow(hWnd, nShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
