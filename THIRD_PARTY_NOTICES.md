# 第三方軟體告知

## Microsoft Visual C++ Runtime

Windows release packages include the x64 Visual C++ runtime DLLs from the licensed
Visual Studio installation's redistributable directory, for app-local deployment.
These Microsoft components retain their own license; they are not relicensed as
EinzigViewer source. Windows 10 or newer provides the Universal CRT.

## miniz 3.1.2

EPUB ZIP reading uses miniz 3.1.2 from https://github.com/richgel999/miniz/tree/3.1.2.
Vendored source is in `third_party/miniz`; the unmodified upstream license is
`third_party/miniz/LICENSE`, included in binary distributions as `LICENSES/miniz-LICENSE.txt`.
EPUB ZIP 讀取使用 miniz；完整 MIT 授權隨原始碼及發行包附上。

## Vulkan resource-query declarations

The private resource-query ABI declarations in `src/AiResources.cpp` adapt a subset of Khronos Vulkan-Headers v1.1.130, Copyright (c) 2015-2019 The Khronos Group Inc., under Apache-2.0. Names and field grouping were changed for dynamic queries and x64 ABI checks. Retained license and modification notice are in `third_party/vulkan-abi/`. Only the existing system driver is queried; the Vulkan SDK/loader and NVML library are not bundled.

## Real-CUGAN ncnn Vulkan / models-se

Optional local AI uses the unmodified official Windows 20220728 Real-CUGAN ncnn Vulkan executable, its models-se no-denoise 2×/4× weights and bundled vcomp140.dll. The executable wrapper and model project use MIT licenses; these do not relicense Microsoft components. Pinned acquisition, archive SHA-256, inventory and all retained notices are documented in `third_party/realcugan/README.md`. Copies accompany the local runtime in `ai/realcugan/LICENSE` and `ai/realcugan/licenses/`. Models and binaries are fetched development assets and are not source files committed to this repository.

## NVIDIA RTX Video SDK 1.1

EinzigViewer 的可選 RTX 增強功能使用 NVIDIA RTX Video SDK 1.1／NGX VSR 與 TrueHDR。

**This software contains source code provided by NVIDIA Corporation.**

NVIDIA RTX Video SDK、NGX headers、import libraries、`nvngx_vsr.dll` 與 `nvngx_truehdr.dll` 皆為 NVIDIA Corporation 及其關係企業的專有軟體，受隨 SDK 提供的 **NVIDIA RTX SDKs LICENSE** 與 RTX supplement 約束。它們不因與 EinzigViewer 一起建置或散布而改採本專案的授權。

專案不提交或獨立散布 SDK 套件；開發者需自行從 NVIDIA 取得 SDK 並接受其條款。應用程式 Release 版只依授權所允許的方式併入 VSR／TrueHDR object code runtime，且必須提供實質的圖片檢視功能。

如果未來商業發佈使用 NGX／RTX SDK 的版本，發佈者必須在發佈前完成 NVIDIA 授權中要求的通知、署名、商標審核與終端使用者條款。

NVIDIA、NVIDIA RTX、GeForce RTX 與相關標誌為 NVIDIA Corporation 在美國及其他國家或地區的商標或註冊商標。此專案不表示受到 NVIDIA 贊助或背書。

