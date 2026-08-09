# Eidolyx

**繁體中文** | [English](README.en.md)

Eidolyx 是一款針對漫畫文字、線條與一般圖片放大的原生 Windows 圖片檢視器，並整合 NVIDIA RTX Video VSR 與 HDR。VSR 是超解析度放大增強，不是內容修復；它不會憑空還原原圖沒有的細節，但對低解析度漫畫中的細字、網點與邊緣通常特別有幫助。開啟任意圖片後，程式會自動索引同資料夾、依自然檔名排序，並在背景預先解碼前後各 8 張圖片。

目前版本：**0.5.0 主視窗整合式 RTX Video VSR／HDR 顯示**。

> **專案更名：** 自 0.5.0 起，專案由 MiraView 更名為 **Eidolyx**，以避開既有同名專案。Eidolyx 是由希臘文 *eidolon*（影像）與拉丁文 *lux*（光）延伸的新造字。第一次啟動新版時，程式會在新的設定檔尚不存在時自動讀取舊版 MiraView 設定；關閉程式後便會保存到 `%LOCALAPPDATA%\Eidolyx\settings.ini`。

## 已完成

- 原生 C++20／Win32 應用程式，不需 .NET Runtime。
- WIC 解碼 JPG、PNG、BMP、GIF 第一幀、TIFF、ICO、JPEG XR，以及系統已安裝 codec 所支援的 WebP／HEIC／AVIF 等格式。
- 讀取 EXIF Orientation，自動旋轉 JPEG。
- 開啟一張圖後自動索引同資料夾。
- Windows 邏輯式自然排序：`1, 2, 3, 10`，不會排成 `1, 10, 2, 3`。
- 4 條背景解碼工作執行緒。
- 前後各 8 張的方向感知預讀，下一張優先於上一張。
- 768 MB LRU 記憶體快取；眼前頁面與鄰近頁面固定保留。
- 快速連按翻頁時清除過時的排隊工作，眼前頁面永遠是最高優先。
- Direct2D 顯示、適合視窗、適合寬度、100%、滑鼠拖曳與游標定位縮放。
- 原生 Windows 選單列（檔案、瀏覽、檢視、RTX、說明）、右鍵選單與拖放開圖。
- 主視窗首次會在目前螢幕工作區正中央以 960×640 開啟；之後仍會記住使用者自行調整的位置與大小。
- 專屬 Eidolyx 應用程式圖示，已嵌入 EXE 與視窗標題列。
- 全螢幕、頁碼／尺寸／縮放資訊列；全螢幕時自動隱藏選單列。
- 滑鼠滾輪可選擇「上一張／下一張」或「放大／縮小」。
- 記住視窗位置、顯示模式、資訊列狀態與滾輪模式。
- 已整合 NVIDIA RTX Video SDK 1.1 的 D3D11 VSR 後端。
- `R` 鍵開關 RTX；只在圖片需要放大時執行，固定使用 Ultra（品質 4）。
- RTX 在獨立背景執行緒處理；翻頁不等 AI，舊頁結果也不會覆蓋新頁。
- RTX 成功套用後，視窗標題會顯示 `[RTX]`。
- RTX 套用後可按住 `C` 暫時顯示原圖，放開立即回到增強結果。
- VSR 輸出依實際顯示尺寸計算；4K 全螢幕會自動重算到接近 3840×2160，最高單邊 7680 像素。
- 切圖時不顯示「處理中／已套用」浮層，底部資訊列會顯示實際 RTX 輸出解析度。
- `H` 鍵會在原本的 Eidolyx 主視窗切換 VSR Ultra → TrueHDR D3D11 GPU 管線，直接輸出 10-bit HDR10／Rec.2020；不再開啟獨立 HDR 視窗，VSR 與 HDR 之間也不經 CPU 讀回。
- HDR 畫面會在主視窗中央的專用顯示區呈現，上下資訊列保持可見；從 HDR 直接切到純 VSR 時，會先安全結束舊的 TrueHDR 工作再啟動 VSR，畫面與翻頁不會卡在上一張 HDR 幀。
- 整合顯示會先確認 Windows HDR 已開啟，依螢幕峰值亮度處理，並提供標準、鮮明、柔和三種預設。
- HDR 顯示沿用主程式的資料夾索引與前後各 8 張預讀；可用方向鍵、Page Up／Page Down、Space 或滾輪連續翻圖。TrueHDR 在背景佇列處理，快速翻頁只保留最新請求，不會阻塞主視窗或讓舊結果覆蓋新圖。
- 可同時開啟多個 Eidolyx 程序；每個程序使用獨立 NGX 實例。本機已驗證兩條 VSR Ultra → TrueHDR 管線可在同一張 RTX 4070 Ti SUPER 同時完成，實際數量與速度取決於 GPU 負載和顯存。
- 一般視窗可按 `M` 最大化並保留標題列、選單與工作列；`F11` 則切換無邊框全螢幕。
- F11 全螢幕時，游標若停留在 Eidolyx 所在螢幕並閒置 3 秒會自動隱藏；移動游標、開啟右鍵選單、切換到其他程式或離開全螢幕都會立即恢復。游標位於其他螢幕時不會被 Eidolyx 隱藏。
- 右鍵選單將「RTX 視訊增強(VSR + HDR)」開關與「HDR 模式」分開；HDR 模式子選單只用來選擇標準、鮮明或柔和。
- 「說明 → 語言」可在繁體中文與 English 之間切換；主視窗、右鍵選單、提示及 RTX 視窗會跟隨並記住選擇。

## 執行

一般使用者可從 [GitHub Releases](https://github.com/akaJooooooker/Eidolyx/releases/latest) 下載 `Eidolyx-0.5.0-win-x64.zip`，完整解壓縮後直接執行 `Eidolyx.exe`。請保留 `nvngx_vsr.dll` 與 `nvngx_truehdr.dll` 在執行檔旁，RTX 功能才可使用；0.5.0 不需要獨立的 HDR 輔助執行檔。壓縮檔內附中英雙語 `README.txt`。

Release 執行檔位於：

```text
out\Eidolyx.exe
```

可直接啟動後拖入圖片，也可以把圖片路徑當成第一個命令列參數：

```powershell
.\out\Eidolyx.exe "D:\Manga\001.jpg"
```

## 操作

| 操作 | 功能 |
|---|---|
| `O` | 開啟圖片 |
| `←` / `Page Up` / `Backspace` | 上一張 |
| `→` / `Page Down` / `Space` | 下一張 |
| 滑鼠滾輪 | 依「檢視 → 滑鼠滾輪」設定翻頁或縮放 |
| `Ctrl` + 滾輪、`+` / `-` | 不受上述設定影響，強制以游標／畫面中心縮放 |
| 滑鼠左鍵拖曳 | 平移圖片 |
| `1` | 適合視窗 |
| `2` | 適合寬度 |
| `3` | 原始大小（100%） |
| `Home` / `End` | 第一張／最後一張 |
| `F11` 或雙擊 | 切換全螢幕 |
| `M` | 切換視窗最大化，保留標題列、選單與工作列 |
| `I` | 顯示／隱藏資訊列 |
| `R` | 開啟／關閉「RTX VSR 超解析度」 |
| `H` | 在原本主視窗開啟／關閉「RTX 視訊增強(VSR + HDR)」；方向鍵／滾輪仍可翻圖，`M` 最大化、`F11` 全螢幕，強度可從「RTX → HDR 預設」選擇 |
| 按住 `C` | 暫時顯示原圖，放開恢復 RTX VSR 結果 |
| 右鍵 | 功能選單；VSR + HDR 開關與只含標準／鮮明／柔和的「HDR 模式」分開顯示 |

## RTX 使用門檻

- 64 位元 Windows 10 或更新版本。
- GeForce RTX 20 系列或更新，或 NVIDIA RTX 1000 系列或更新的 GPU。
- 支援該顯示卡與 RTX Video SDK 的 NVIDIA 驅動程式；本機 SDK 1.1 文件列出的最低版本是 550.58。
- Eidolyx 必須實際在 NVIDIA RTX GPU 上執行；雙顯卡筆電可在 Windows 圖形設定或 NVIDIA App 將 Eidolyx 指定為高效能 NVIDIA GPU。
- VSR 需要 `nvngx_vsr.dll`；整合 HDR 顯示另需 `nvngx_truehdr.dll`。0.5.0 的 HDR10 swap chain 已直接內建於 `Eidolyx.exe`。
- 整合 HDR 顯示需要 HDR 顯示器，並在「Windows 設定 → 系統 → 顯示器 → HDR」開啟 HDR。

Eidolyx 是直接呼叫 RTX Video SDK，而不是借用瀏覽器的驅動程式增強開關。因此達到上述門檻後，即使 NVIDIA 控制面板／NVIDIA App 裡的「影片／視訊增強」沒有開啟，Eidolyx 的 `R` 鍵 RTX 功能仍可使用。該開關主要控制 NVIDIA 驅動替支援的瀏覽器或播放器所做的增強；硬體、驅動、SDK runtime 與程式使用 NVIDIA GPU 的條件仍然不可少。

若電腦沒有相容的 RTX GPU、驅動程式過舊、Windows／螢幕未開啟 HDR，或必要 DLL 遺失，Eidolyx 會顯示錯誤對話框並在同一個主視窗退回一般圖片檢視，不會因為不支援 RTX 而直接關閉。背景 VSR 與 TrueHDR 工作都有例外攔截；HDR 結果失敗時會釋放 HDR10 swap chain 並恢復 Direct2D。

官方參考：[RTX Video SDK Getting Started](https://developer.nvidia.com/rtx-video-sdk/getting-started)、[NVIDIA 控制面板影片影像設定說明](https://www.nvidia.com/content/Control-Panel-Help/vLatest/en-us/mergedProjects/Display/Reference_Adjust_Video_Image_Settings.htm)。

## 建置

需求：Visual Studio 2022，並安裝「使用 C++ 的桌面開發」工作負載與 Windows SDK。

```powershell
cmake --preset vs2022-x64
cmake --build --preset release
```

RTX 版還需要把 SDK 1.1 解壓到 `.sdk/rtx-video-1.1.0`，或在設定時指定：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DEIDOLYX_RTX_SDK_ROOT="D:\SDK\RTX_Video_SDK_v1.1.0"
```

設定成功時 CMake 會顯示 `Eidolyx RTX backend: enabled`，並自動把 Release 版 `nvngx_vsr.dll` 複製到執行檔旁。沒有 SDK 時仍可建置一般檢視版。

也可用 Visual Studio 直接開啟此資料夾或 `CMakeLists.txt`。

## 目前限制

- GIF／動態 WebP 目前只顯示第一幀。
- WebP、HEIC、AVIF、JPEG XL 是否可開啟取決於 Windows 已安裝的 WIC codec。
- 尚未有縮圖瀏覽器、資料夾樹、CBZ、雙頁模式、檔案管理與完整 ICC 色彩管理。
- RTX VSR 與 TrueHDR 已在同一個 D3D11 裝置中以 GPU texture 串接，10-bit HDR swap chain 也已整合回原本的 Eidolyx 主視窗。
- 目前 RTX 輸出會讀回 CPU 再交給 Direct2D；下一階段將改為 D3D11 texture 零拷貝顯示。
- 第一次開啟 RTX 需初始化 NGX，實測約數秒；後續圖片較快。
- 目前 VSR 結果只保留在記憶體，尚未建立磁碟快取。

## NVIDIA SDK 告知

Eidolyx 使用 NVIDIA RTX Video SDK。SDK 與 runtime 受 NVIDIA RTX SDKs License 約束，並非本專案的一般原始碼資產。詳見 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

更詳細的設計與路線見 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) 和 [docs/RTX_INTEGRATION.md](docs/RTX_INTEGRATION.md)。
