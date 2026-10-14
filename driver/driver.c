/*
 * driver.c — VhidLab：KMDF + VHF 虚拟鼠标/键盘源驱动。
 * 结构：root 枚举的软件设备 → EvtDeviceAdd 建 WDFDEVICE + 默认队列收 IOCTL →
 *       VhfCreate/VhfStart 建虚拟 HID 树 → 收到 IOCTL 时组 report 并 VhfReadReportSubmit。
 * 日志用 DbgPrintEx，DebugView 可看（过滤 VhidLab）。
 *
 * 需链接 Vhfkm.lib，包含 vhf.h（WDK）。标 // VERIFY: 处请对照 PC09 上真实 WDK 头核对。
 */
#include <ntddk.h>
#include <wdf.h>
#include <hidport.h>   // VERIFY: VHF 相关类型是否需要此头；部分 WDK 用 <vhf.h> 即可
#include <vhf.h>
#include "..\\include\\vhidlab_protocol.h"
#include "hid_descriptors.h"

#define VLOG(fmt, ...) DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "VhidLab: " fmt "\n", __VA_ARGS__)

typedef struct _DEVICE_CONTEXT {
    VHFHANDLE VhfHandle;
    BOOLEAN   VhfStarted;
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, DeviceGetContext)

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD VhidLabEvtDeviceAdd;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL VhidLabEvtIoDeviceControl;

/* VHF 要求的回调：这里是纯输出设备（只上报 input report），对 host→device
 * 的 GetFeature/SetOutput 用空实现即可。VERIFY: 回调签名与 WDK vhf.h 对齐。 */
EVT_VHF_ASYNC_OPERATION VhidLabEvtVhfReadyForNextReadReport; // 可选
static VOID VhidLabEvtVhfReadyForNextReadReport(PVOID Ctx, VHFOPERATIONHANDLE Op, PVOID Buf, ULONG Len)
{
    UNREFERENCED_PARAMETER(Ctx); UNREFERENCED_PARAMETER(Buf); UNREFERENCED_PARAMETER(Len);
    // 对纯输出设备无需处理；完成操作。VERIFY: 是否需要 VhfAsyncOperationComplete。
    VhfAsyncOperationComplete(Op, STATUS_SUCCESS);
}

static NTSTATUS VhidLabStartVhf(WDFDEVICE device)
{
    PDEVICE_CONTEXT ctx = DeviceGetContext(device);
    VHF_CONFIG cfg;
    NTSTATUS status;

    VHF_CONFIG_INIT(&cfg,
                    WdfDeviceWdmGetDeviceObject(device),
                    sizeof(g_VhidLabReportDescriptor),
                    (PUCHAR)g_VhidLabReportDescriptor);
    cfg.OperationContextSize = 0;
    cfg.EvtVhfReadyForNextReadReport = VhidLabEvtVhfReadyForNextReadReport; // VERIFY: 字段名
    // 虚拟设备身份，后续可改做伪装（VID/PID/字符串）
    cfg.VendorID  = 0x1209; // 占位 VID（开源 pid.codes 测试段），正式研究再改
    cfg.ProductID = 0x0001;
    cfg.VersionNumber = 0x0001;

    status = VhfCreate(&cfg, &ctx->VhfHandle);
    if (!NT_SUCCESS(status)) { VLOG("VhfCreate failed 0x%x", status); return status; }

    status = VhfStart(ctx->VhfHandle);
    if (!NT_SUCCESS(status)) { VLOG("VhfStart failed 0x%x", status); return status; }
    ctx->VhfStarted = TRUE;
    VLOG("VHF started");
    return STATUS_SUCCESS;
}

static NTSTATUS VhidLabSubmit(PDEVICE_CONTEXT ctx, PVOID report, ULONG len)
{
    // 把一帧 input report 交给系统 HID 栈
    return VhfReadReportSubmit(ctx->VhfHandle, report, len); // VERIFY: 参数顺序/类型
}

static NTSTATUS VhidLabSendMouse(PDEVICE_CONTEXT ctx, signed char x, signed char y,
                                 signed char wheel, unsigned char buttons)
{
    VHIDLAB_MOUSE_REPORT r = {0};
    r.reportId = VHIDLAB_REPORTID_MOUSE;
    r.buttons = buttons; r.x = x; r.y = y; r.wheel = wheel;
    return VhidLabSubmit(ctx, &r, sizeof(r));
}

static NTSTATUS VhidLabSendKey(PDEVICE_CONTEXT ctx, unsigned char modifier, unsigned char usage)
{
    VHIDLAB_KBD_REPORT r = {0};
    r.reportId = VHIDLAB_REPORTID_KBD;
    r.modifier = modifier;
    if (usage) r.keys[0] = usage;
    return VhidLabSubmit(ctx, &r, sizeof(r));
}

VOID VhidLabEvtIoDeviceControl(WDFQUEUE Queue, WDFREQUEST Request, size_t OutLen,
                               size_t InLen, ULONG IoControlCode)
{
    WDFDEVICE device = WdfIoQueueGetDevice(Queue);
    PDEVICE_CONTEXT ctx = DeviceGetContext(device);
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    PVOID inBuf = NULL; size_t inBufLen = 0;
    UNREFERENCED_PARAMETER(OutLen);

    switch (IoControlCode) {
    case IOCTL_VHIDLAB_MOUSE_MOVE:
    case IOCTL_VHIDLAB_MOUSE_BUTTON:
    case IOCTL_VHIDLAB_MOUSE_WHEEL: {
        if (InLen < sizeof(VHIDLAB_MOUSE_IN)) { status = STATUS_BUFFER_TOO_SMALL; break; }
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(VHIDLAB_MOUSE_IN), &inBuf, &inBufLen);
        if (!NT_SUCCESS(status)) break;
        VHIDLAB_MOUSE_IN *m = (VHIDLAB_MOUSE_IN*)inBuf;
        if (m->flags & VHIDLAB_FLAG_CLICK) {
            status = VhidLabSendMouse(ctx, m->dx, m->dy, m->wheel, m->buttons); // 按下
            if (NT_SUCCESS(status)) status = VhidLabSendMouse(ctx, 0, 0, 0, 0); // 释放
        } else {
            unsigned char btn = m->is_down ? m->buttons : 0;
            status = VhidLabSendMouse(ctx, m->dx, m->dy, m->wheel, btn);
        }
        break;
    }
    case IOCTL_VHIDLAB_KEY_EVENT: {
        if (InLen < sizeof(VHIDLAB_KEY_IN)) { status = STATUS_BUFFER_TOO_SMALL; break; }
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(VHIDLAB_KEY_IN), &inBuf, &inBufLen);
        if (!NT_SUCCESS(status)) break;
        VHIDLAB_KEY_IN *k = (VHIDLAB_KEY_IN*)inBuf;
        if (k->flags & VHIDLAB_FLAG_CLICK) {
            status = VhidLabSendKey(ctx, k->modifier, k->usage); // 按下
            if (NT_SUCCESS(status)) status = VhidLabSendKey(ctx, 0, 0); // 全部释放
        } else {
            status = VhidLabSendKey(ctx, k->modifier, k->is_down ? k->usage : 0);
        }
        break;
    }
    default: break;
    }
    WdfRequestComplete(Request, status);
}

NTSTATUS VhidLabEvtDeviceAdd(WDFDRIVER Driver, PWDFDEVICE_INIT DeviceInit)
{
    UNREFERENCED_PARAMETER(Driver);
    NTSTATUS status;
    WDF_OBJECT_ATTRIBUTES attr;
    WDFDEVICE device;
    WDF_IO_QUEUE_CONFIG qcfg;
    DECLARE_CONST_UNICODE_STRING(symLink, L"\\DosDevices\\VhidLab");

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, DEVICE_CONTEXT);
    status = WdfDeviceCreate(&DeviceInit, &attr, &device);
    if (!NT_SUCCESS(status)) return status;

    status = WdfDeviceCreateSymbolicLink(device, &symLink);
    if (!NT_SUCCESS(status)) { VLOG("symlink 0x%x", status); return status; }

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&qcfg, WdfIoQueueDispatchParallel);
    qcfg.EvtIoDeviceControl = VhidLabEvtIoDeviceControl;
    status = WdfIoQueueCreate(device, &qcfg, WDF_NO_OBJECT_ATTRIBUTES, NULL);
    if (!NT_SUCCESS(status)) return status;

    status = VhidLabStartVhf(device);
    return status;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, VhidLabEvtDeviceAdd);
    VLOG("DriverEntry");
    return WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
}
