/*
 * hid_descriptors.h — 虚拟设备的 HID report descriptor。
 * 方案选择：一个 VHF 设备，用一份组合描述符，内含两个顶层 collection：
 *   Report ID 1 = 鼠标（相对 X/Y、3 键、滚轮）
 *   Report ID 2 = 键盘（boot keyboard：1 修饰字节 + 1 保留 + 6 键码）
 * 组合成一台设备便于单一 \Device\VhidLab 控制，字节已逐行注释。
 */
#ifndef VHIDLAB_HID_DESCRIPTORS_H
#define VHIDLAB_HID_DESCRIPTORS_H

#define VHIDLAB_REPORTID_MOUSE 1
#define VHIDLAB_REPORTID_KBD   2

/* 发给 VhfReadReportSubmit 的 report 负载（含 Report ID 前缀） */
#include <pshpack1.h>
typedef struct _VHIDLAB_MOUSE_REPORT {
    unsigned char reportId;  /* = 1 */
    unsigned char buttons;   /* bit0 左 bit1 右 bit2 中 */
    signed char   x;         /* 相对 */
    signed char   y;
    signed char   wheel;
} VHIDLAB_MOUSE_REPORT;
typedef struct _VHIDLAB_KBD_REPORT {
    unsigned char reportId;  /* = 2 */
    unsigned char modifier;
    unsigned char reserved;  /* = 0 */
    unsigned char keys[6];
} VHIDLAB_KBD_REPORT;
#include <poppack.h>

static const unsigned char g_VhidLabReportDescriptor[] = {
    /* ===== 鼠标 collection, Report ID 1 ===== */
    0x05, 0x01,       /* Usage Page (Generic Desktop)        */
    0x09, 0x02,       /* Usage (Mouse)                       */
    0xA1, 0x01,       /* Collection (Application)            */
    0x85, VHIDLAB_REPORTID_MOUSE, /*   Report ID (1)         */
    0x09, 0x01,       /*   Usage (Pointer)                   */
    0xA1, 0x00,       /*   Collection (Physical)             */
    0x05, 0x09,       /*     Usage Page (Buttons)            */
    0x19, 0x01,       /*     Usage Minimum (1)               */
    0x29, 0x03,       /*     Usage Maximum (3)               */
    0x15, 0x00,       /*     Logical Minimum (0)             */
    0x25, 0x01,       /*     Logical Maximum (1)             */
    0x95, 0x03,       /*     Report Count (3)                */
    0x75, 0x01,       /*     Report Size (1)                 */
    0x81, 0x02,       /*     Input (Data,Var,Abs) 3 按键位    */
    0x95, 0x01,       /*     Report Count (1)                */
    0x75, 0x05,       /*     Report Size (5)                 */
    0x81, 0x03,       /*     Input (Cnst,Var,Abs) 填充 5 位   */
    0x05, 0x01,       /*     Usage Page (Generic Desktop)    */
    0x09, 0x30,       /*     Usage (X)                       */
    0x09, 0x31,       /*     Usage (Y)                       */
    0x09, 0x38,       /*     Usage (Wheel)                   */
    0x15, 0x81,       /*     Logical Minimum (-127)          */
    0x25, 0x7F,       /*     Logical Maximum (127)           */
    0x75, 0x08,       /*     Report Size (8)                 */
    0x95, 0x03,       /*     Report Count (3) X,Y,Wheel      */
    0x81, 0x06,       /*     Input (Data,Var,Rel) 相对        */
    0xC0,             /*   End Collection (Physical)         */
    0xC0,             /* End Collection (Application)        */

    /* ===== 键盘 collection, Report ID 2 (boot keyboard) ===== */
    0x05, 0x01,       /* Usage Page (Generic Desktop)        */
    0x09, 0x06,       /* Usage (Keyboard)                    */
    0xA1, 0x01,       /* Collection (Application)            */
    0x85, VHIDLAB_REPORTID_KBD, /*   Report ID (2)           */
    0x05, 0x07,       /*   Usage Page (Key Codes)            */
    0x19, 0xE0,       /*   Usage Minimum (224 LCtrl)         */
    0x29, 0xE7,       /*   Usage Maximum (231 RGui)          */
    0x15, 0x00,       /*   Logical Minimum (0)               */
    0x25, 0x01,       /*   Logical Maximum (1)               */
    0x75, 0x01,       /*   Report Size (1)                   */
    0x95, 0x08,       /*   Report Count (8) 修饰 8 位         */
    0x81, 0x02,       /*   Input (Data,Var,Abs)              */
    0x95, 0x01,       /*   Report Count (1)                  */
    0x75, 0x08,       /*   Report Size (8)                   */
    0x81, 0x03,       /*   Input (Cnst,Var,Abs) 保留字节      */
    0x95, 0x06,       /*   Report Count (6)                  */
    0x75, 0x08,       /*   Report Size (8)                   */
    0x15, 0x00,       /*   Logical Minimum (0)               */
    0x25, 0x65,       /*   Logical Maximum (101)             */
    0x05, 0x07,       /*   Usage Page (Key Codes)            */
    0x19, 0x00,       /*   Usage Minimum (0)                 */
    0x29, 0x65,       /*   Usage Maximum (101)               */
    0x81, 0x00,       /*   Input (Data,Array) 6 键码          */
    0xC0              /* End Collection                      */
};

#endif
