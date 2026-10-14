# VhidLab — Windows 虚拟 HID（VHF）研究工具

独立小工程：用微软 **Virtual HID Framework (VHF)** 在本机造一台虚拟鼠标 + 虚拟键盘，
用户态窗口发左击/右击/滚轮/移动和简单按键。研究用，**仅测试签名**，不申请 EV。

## 架构

```
[Win32 测试窗口]
      |  DeviceIoControl (\\\\.\\VhidLab)
[vhidlab.sys  KMDF HID Source]
      |  VhfReadReportSubmit
[vhf.sys 系统驱动] → HID 栈 → 系统当成鼠标/键盘
```

Report ID 1 = 鼠标（相对 X/Y、3 键、滚轮）；Report ID 2 = 键盘（boot keyboard）。

## 文件

| 路径 | 作用 |
|---|---|
| `include/vhidlab_protocol.h` | app/驱动共享 IOCTL 与结构体 |
| `driver/hid_descriptors.h` | HID report descriptor（逐字节注释） |
| `driver/driver.c` | KMDF + VHF 源驱动 |
| `driver/vhidlab.inf` | 安装 INF（LowerFilters=vhf，root\\vhidlab） |
| `app/main.c` | 测试窗口 |
| `scripts/*.ps1` | 测试签名 / 证书 / 签名 / 安装 / 卸载 |
| `vhid-lab.sln` | VS2022 解决方案 |

## 编译（PC09，需已装 VS2022 + WDK）

1. 用 Visual Studio 2022 打开 `vhid-lab.sln`
2. 配置选 **Release|x64**（或 Debug|x64）
3. 先编 `vhidlab` 驱动工程，再编 `app`
4. 输出大致在 `x64\\Release\\`（以 VS 实际输出为准）

驱动工程标了几处 `// VERIFY:`：第一次编译时请对照本机 WDK 的 `vhf.h`，
核对 `VHF_CONFIG` 字段名、`VhfReadReportSubmit` / `VhfAsyncOperationComplete` 签名。

## 部署（管理员 PowerShell，研究机）

```powershell
cd ...\\vhid-lab\\scripts
.\\enable-testsigning.ps1   # 然后重启；桌面会出现 Test Mode
.\\make-testcert.ps1        # 自签证书并导入信任
.\\sign-driver.ps1 -SysDir ..\\x64\\Release   # 路径按实际输出改
.\\install-driver.ps1 -InfDir ..\\x64\\Release
# 若设备未自动出现：
devgen /add /hardwareid "root\\vhidlab"
```

跑 `app.exe`。设备管理器应能看到虚拟 HID；DebugView 勾选 Capture Kernel，过滤 `VhidLab`。

## 已知限制

- 设备树里是软件/虚拟节点，**没有真实 USB 父口**
- 仅测试签名；HVCI / Memory Integrity 可能拦加载
- 第一次在真实 WDK 上编译时，`VERIFY` 注释处可能要按头文件微调
- 键盘是次要能力；只做了字符与两个组合键演示

## 后续 TODO

- VID/PID / 字符串伪装
- 接入 llm_rpa 作 `VhidMouseDesktop`（对齐幽灵盒语义，但不走真 USB）
