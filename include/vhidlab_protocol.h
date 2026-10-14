/*
 * vhidlab_protocol.h — 用户态 app 与内核驱动共享的 IOCTL 接口。纯 C。
 * 驱动用 KMDF + VHF 建虚拟鼠标（主）和虚拟键盘（次）。app 通过 IOCTL
 * 把动作发给驱动，驱动转成标准 HID input report 用 VhfReadReportSubmit 上报。
 */
#ifndef VHIDLAB_PROTOCOL_H
#define VHIDLAB_PROTOCOL_H

#define VHIDLAB_DOS_NAME   L"\\DosDevices\\VhidLab"
#define VHIDLAB_WIN32_NAME L"\\\\.\\VhidLab"
#define VHIDLAB_NT_NAME    L"\\Device\\VhidLab"

#ifndef CTL_CODE
#include <winioctl.h>
#endif

#define VHIDLAB_IOCTL(i) CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800 + (i), METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VHIDLAB_MOUSE_MOVE   VHIDLAB_IOCTL(0)
#define IOCTL_VHIDLAB_MOUSE_BUTTON VHIDLAB_IOCTL(1)
#define IOCTL_VHIDLAB_MOUSE_WHEEL  VHIDLAB_IOCTL(2)
#define IOCTL_VHIDLAB_KEY_EVENT    VHIDLAB_IOCTL(3)

#define VHIDLAB_BTN_LEFT   0x01
#define VHIDLAB_BTN_RIGHT  0x02
#define VHIDLAB_BTN_MIDDLE 0x04
#define VHIDLAB_FLAG_CLICK 0x01

#include <pshpack1.h>
typedef struct _VHIDLAB_MOUSE_IN {
    signed char dx; signed char dy; signed char wheel;
    unsigned char buttons; unsigned char is_down; unsigned char flags;
} VHIDLAB_MOUSE_IN;
typedef struct _VHIDLAB_KEY_IN {
    unsigned char modifier; unsigned char usage; unsigned char is_down; unsigned char flags;
} VHIDLAB_KEY_IN;
#include <poppack.h>
#endif
