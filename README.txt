# EinzigViewer v1.0.0

**繁體中文** | [English](README.en.md)

Windows 原生圖片與 EPUB 圖片閱讀器，提供單頁、直式連續閱讀、本機 AI 超解析度，以及 NVIDIA RTX VSR／HDR。

## 下載與安裝

從 [v1.0.0 發布頁](https://github.com/akaJooooooker/EinzigViewer/releases/tag/v1.0.0) 選擇：

- **安裝版 Setup.exe**：安裝至目前使用者的資料夾，提供開始功能表捷徑與解除安裝，不需系統管理員權限。
- **免安裝版 ZIP**：完整解壓縮到可寫入的固定資料夾，再執行 EinzigViewer.exe。請保留所有 DLL、ai 資料夾與 portable.ini；設定寫入同資料夾的 settings.ini。

支援 Windows 10／11 x64。一般看圖不需要 NVIDIA 顯卡；AI 需要可用的 Vulkan GPU／驅動，RTX VSR／HDR 需要相容 NVIDIA RTX GPU。HDR 另需顯示器及 Windows HDR 已啟用。隨包提供 AI 模型與所需 Visual C++ Runtime，不會於啟動時自動下載模型。

## AI 功能仍在測試與改進中

AI 預設關閉，按 **A** 手動開啟。先顯示原圖，背景完成後才換成增強圖；再按 A 立即還原。提供 **2×／4×**，按住 **C** 可比較原圖。會在資源預算內預處理目前資料夾或 EPUB，單頁與直式模式可重用仍有效的已完成快取。

處理速度與畫質仍持續調整。**首次處理可能需要明顯等待，尤其大圖與 4×；目前不保證即時處理。** 時間會隨圖片尺寸、倍率、硬體及其他程式負載而異。已完成快取切換較快，不代表新圖能瞬間完成。AI 可能改動極小文字、網點和邊緣，不能還原原圖沒有的真實細節；可隨時切回原圖。

AI 使用本機 Real-CUGAN models-se，不上傳圖片。需保留完整 ai/realcugan 資料夾；缺檔、資源不足或處理失敗時保留原圖並顯示原因。AI 使用 SDR，與 RTX VSR／HDR 互斥。

「AI → 資源設定」提供自動或手動 RAM、磁碟快取、AI 子程序 CPU、批次頁數與顯存保留設定。實際工作仍依當下資源保留餘裕，不足時暫停。顯存保留是監測門檻；調高資源上限不保證等比例加速。設定可以儲存，AI 開關每次啟動仍預設關閉。

## 操作

| 按鍵 | 功能 |
| --- | --- |
| O／拖放 | 開啟圖片或 EPUB；自動索引同資料夾圖片 |
| V | 單頁／直式連續閱讀 |
| 1／2／3 | 適合視窗／適合寬度／原始大小 |
| Ctrl＋滾輪、＋／－ | 縮放 |
| 方向鍵、Page Up／Down、Space | 翻頁或捲動 |
| [／] | 目前頁向左／右旋轉 90°，不改寫原檔 |
| Ctrl＋C | 複製目前已旋轉的 SDR 原圖 |
| Ctrl＋Shift＋C | 拖曳框選複製，可跨直式相鄰頁；Esc 取消 |
| Ctrl＋S／Ctrl＋Shift＋S | 原圖轉存 PNG／縮小轉存 |
| A／C | AI 開關／按住比較原圖 |
| R／H | 單頁 RTX 增強／HDR |
| M／F11 | 最大化／全螢幕 |

支援 WIC 圖片格式、EXIF 方向、自然檔名排序、背景預讀、中英介面與 EPUB spine 圖片順序。EPUB 只呈現內嵌點陣圖片，不提供文字／CSS 排版或 DRM 解密。直式閱讀使用 SDR。

## 設為 Windows 預設圖片檢視器

- **安裝版**：在「說明 → 設定預設圖片檢視器」進入 Windows 設定，選擇 EinzigViewer 並確認要開啟的格式。安裝本身不會更改既有預設程式。
- **免安裝版**：先解壓縮到固定位置，對圖片按右鍵 → 開啟方式 → 選擇其他應用程式，指定 EinzigViewer.exe 並設為一律使用。不同副檔名請分別確認；移動資料夾後需重新指定。

## 更新與設定

兩版都有 GitHub Releases 新版提示，可在「說明」手動檢查或關閉自動檢查。背景每 24 小時最多查詢一次；發現新版可下載、稍後或略過。下載會開啟瀏覽器，不會自動替換程式；更新前請關閉檢視器。免安裝版更新時保留 settings.ini。0.7.0 沒有更新提示，需先手動升級一次。

安裝版設定存於 %LOCALAPPDATA%/EinzigViewer/settings.ini；免安裝版存於程式旁。各第三方元件的原授權隨包附於 LICENSES 與 ai/realcugan/licenses；請參閱 [第三方告知](THIRD_PARTY_NOTICES.md)。

## 開發建置

使用 Visual Studio 2022 C++、Windows SDK 與 CMake 3.24 以上。cmake -S . -B build，接著 cmake --build build --config Release。RTX SDK 可由 EINZIGVIEWER_RTX_SDK_ROOT 指定，未提供時仍可建置一般閱讀器。以 scripts/Fetch-AiModels.ps1 準備固定版本的本機 AI 資產，scripts/Package-Release.ps1 建立 Setup／ZIP。

---

# EinzigViewer v1.0.0

[繁體中文](README.md) | **English**

A native Windows image and EPUB image viewer with single-page and continuous reading, local AI upscaling, and NVIDIA RTX VSR/HDR.

## Downloads and installation

Choose an edition from the [v1.0.0 release page](https://github.com/akaJooooooker/EinzigViewer/releases/tag/v1.0.0):

- **Setup.exe installer**: installs for the current user, with Start menu shortcuts and an uninstaller. Administrator rights are not required.
- **Portable ZIP**: extract everything into a writable, permanent folder, then run EinzigViewer.exe. Keep all DLLs, the ai directory and portable.ini; settings are stored in the adjacent settings.ini.

Supports Windows 10/11 x64. Ordinary viewing does not require NVIDIA hardware. AI requires a working Vulkan GPU and driver; RTX VSR/HDR requires compatible NVIDIA RTX hardware. HDR also requires a suitable display and Windows HDR enabled. AI models and the required Visual C++ Runtime files are bundled; models are not downloaded on startup.

## AI is still being tested and improved

AI starts off. Press **A** to enable it manually. Originals appear first, then completed results replace them; press A again to restore originals immediately. Choose **2×/4×** and hold **C** to compare. Preprocessing covers the current folder or EPUB within resource budgets. Single-page and continuous reading reuse valid completed results.

Speed and quality remain under improvement. **First-time processing can take a noticeable amount of time, especially with large images and 4×; real-time processing is not guaranteed.** Time depends on image dimensions, scale, hardware and other workloads. Fast cached switching does not mean new images process instantly. AI may alter tiny text, screentones and edges, and cannot recover true details absent from the original; you can return to the original at any time.

AI uses local Real-CUGAN models-se without uploading images. Keep the complete ai/realcugan directory; missing files, insufficient resources or processing failures retain the original and show a reason. AI uses SDR and is mutually exclusive with RTX VSR/HDR.

AI → Resource settings provides automatic or manual RAM, disk-cache, child CPU, batch and free-VRAM controls. Work still preserves current resource reserves and pauses when resources are insufficient. The VRAM reserve is a monitoring threshold; raising resource ceilings does not guarantee proportionally faster processing. Limits persist, while AI still starts off on each launch.

## Controls

| Key | Action |
| --- | --- |
| O / drag-drop | Open images or EPUB; automatically index images in the same folder |
| V | Single-page / continuous reading |
| 1 / 2 / 3 | Fit window / fit width / actual size |
| Ctrl+wheel, + / - | Zoom |
| Arrows, Page Up/Down, Space | Navigate or scroll |
| [ / ] | Rotate the current page left/right by 90° without rewriting its file |
| Ctrl+C | Copy the rotated SDR original |
| Ctrl+Shift+C | Drag to copy a displayed region, including adjacent continuous pages; Esc cancels |
| Ctrl+S / Ctrl+Shift+S | Export original PNG / downscale and export |
| A / C | Toggle AI / hold to compare with the original |
| R / H | Single-page RTX enhancement / HDR |
| M / F11 | Maximize / fullscreen |

Supports WIC image formats, EXIF orientation, natural sorting, background preloading, Chinese/English UI and EPUB spine image order. EPUB displays embedded raster images without text/CSS layout or DRM decryption. Continuous reading uses SDR.

## Set as the Windows default image viewer

- **Installed**: Help → Set as default image viewer opens Windows Settings. Select EinzigViewer and confirm the formats it should open. Installing does not change existing defaults.
- **Portable**: extract to a fixed location, then right-click an image → Open with → Choose another app. Select EinzigViewer.exe and choose always use. Confirm each extension separately; moving the folder requires selecting its new path.

## Updates and settings

Both editions notify about newer GitHub Releases. Use Help to check manually or disable automatic checks. Background checks run at most once per 24 hours; choose Download, Later or Skip when a new release appears. Downloads open a browser and do not replace files automatically; close the viewer before upgrading. Preserve portable settings.ini when updating. Version 0.7.0 has no update notifications and needs one manual upgrade.

Installed settings use %LOCALAPPDATA%/EinzigViewer/settings.ini; portable settings remain next to the executable. Third-party terms accompany LICENSES and ai/realcugan/licenses; see [third-party notices](THIRD_PARTY_NOTICES.md).

## Building

Use Visual Studio 2022 C++, Windows SDK and CMake 3.24+. Run cmake -S . -B build, then cmake --build build --config Release. EINZIGVIEWER_RTX_SDK_ROOT selects the optional NVIDIA SDK; the ordinary viewer can also be built without it. scripts/Fetch-AiModels.ps1 prepares pinned AI assets, and scripts/Package-Release.ps1 creates Setup/ZIP packages.
