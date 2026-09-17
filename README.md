# EinzigViewer

**繁體中文** | [English](README.en.md)

EinzigViewer 是一款針對漫畫文字、線條與一般圖片放大的原生 Windows 圖片檢視器，並整合 NVIDIA RTX Video VSR 與 HDR。VSR 是超解析度放大增強，不是內容修復；它不會憑空還原原圖沒有的細節，但對低解析度漫畫中的細字、網點與邊緣通常特別有幫助。開啟任意圖片後，程式會自動索引同資料夾、依自然檔名排序，並在背景預先解碼前後各 8 張圖片。

目前版本：**0.7.0：無間距直式連續閱讀、EPUB 圖片閱讀、長圖分段顯示與快取可靠性改善**。保留 HDR JXR → SDR PNG、圖片縮小轉存與單頁 RTX VSR／HDR。

## 0.7.0：直式閱讀與 EPUB

按 **V**，或在「檢視／右鍵 → 直式連續閱讀（無間距）」切換。圖片會適合閱讀寬度、首尾相接，滾輪與精密觸控板可連續移動；左鍵可拖曳，Ctrl+滾輪縮放。↑／↓小幅移動，Page Up／Page Down／Space 捲動約一個畫面，Shift+Space 往上；←／→跳到相鄰圖片，Home／End 到整份內容的開頭／底部。1／2／3 可回到單頁顯示。模式會保存。

按 O 或拖入 `.epub` 即可在背景載入，並自動進入直式模式。圖片依 EPUB 的 spine 章節順序、章節內出現順序排列，支援 XHTML `img` 與 SVG 包裝中的點陣圖片；保留重複出現的頁面，不用檔名重排整本書。載入時可按 Esc 取消，或開啟另一個檔案；失敗時保留目前內容。閱讀器不下載書內外部資源，也不執行腳本。

資訊列標示的是畫面上緣所在頁面；Ctrl+S／Ctrl+Shift+S 可將該頁轉存 PNG／縮小轉存。EPUB 圖片暫存於每本書獨立的系統暫存目錄，切換書籍且背景解碼結束後清除，正常結束程式也會清除。

直式模式目前使用 **SDR**，不需要 NVIDIA GPU。按 R／H 會切回單頁 RTX VSR／HDR；在 RTX 模式按 V 則返回直式 SDR 並提示。多頁 HDR 合成尚未實作。

此版本改善記憶體控制：768 MiB 解碼快取採硬上限、僅優先保留目前可見頁面；額外 Direct2D 分段快取上限為 96 MiB，長圖按可見區分段上傳。快取上限不包含解碼中的工作緩衝區與目前顯示圖片。解碼失敗不再於捲動時反覆重試，重新開啟才重試；單一解碼緩衝区上限 512 MiB，避免異常尺寸大量配置。

EPUB 圖片閱讀不包含文字／CSS 排版、純向量 SVG、DRM 解密。字型混淆不影響圖片閱讀。ZIP 上限為 50,000 項目、單項解壓縮 256 MiB、總計 4 GiB；不支援的書籍會顯示錯誤。閱讀順序參考 [W3C EPUB 3.3](https://www.w3.org/TR/epub-33/)。

> **專案更名：** 自 0.6.0 起，專案由 Eidolyx 更名為 **EinzigViewer**；0.4.4 與更早版本曾使用 MiraView。*Einzig* 是德文的「獨特／唯一」，Viewer 表示圖片檢視器。第一次啟動新版時，若新設定尚不存在，會依序承接 Eidolyx、MiraView 的設定；關閉後保存到 `%LOCALAPPDATA%\EinzigViewer\settings.ini`。

## 已完成

- 原生 C++20／Win32 應用程式，不需 .NET Runtime。
- WIC 解碼 JPG、PNG、BMP、GIF 第一幀、TIFF、ICO、JPEG XR，以及系統已安裝 codec 所支援的 WebP／HEIC／AVIF 等格式。
- JXR／WDP／HDP 若是 fixed-point 或 floating-point extended RGB，會先以 128bpp float 保留 scRGB，再以亮度保持的 HDR → SDR 色調映射轉為一般 sRGB 顯示；不再於解碼一開始直接壓成 8-bit。
- 「檔案 → 轉存 PNG」可把目前完整解碼的圖片（包含 HDR JXR 的 SDR 結果）以原尺寸儲存為帶 sRGB 標記的 PNG；不會誤存 RTX 顯示增強畫面。
- 「檔案 → 縮小並轉存 PNG」可輸入目標長邊像素，保持比例並使用 WIC Fant 高品質縮小；只允許縮小，不會意外放大原圖。
- 讀取 EXIF Orientation，自動旋轉 JPEG。
- 開啟一張圖後自動索引同資料夾。
- Windows 邏輯式自然排序：`1, 2, 3, 10`，不會排成 `1, 10, 2, 3`。
- 4 條背景解碼工作執行緒。
- 前後各 8 張的方向感知預讀，下一張優先於上一張。
- 768 MiB LRU 解碼快取；目前頁面優先保留，超出預算仍會回收。
- 快速連按翻頁時清除過時的排隊工作，眼前頁面永遠是最高優先。
- Direct2D 顯示、適合視窗、適合寬度、100%、滑鼠拖曳與游標定位縮放。
- 原生 Windows 選單列（檔案、瀏覽、檢視、RTX、說明）、右鍵選單與拖放開圖。
- 主視窗首次會在目前螢幕工作區正中央以 960×640 開啟；之後仍會記住使用者自行調整的位置與大小。
- 專屬 EinzigViewer 應用程式圖示，已嵌入 EXE 與視窗標題列。
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
- `H` 鍵會在原本的 EinzigViewer 主視窗切換 VSR Ultra → TrueHDR D3D11 GPU 管線，直接輸出 10-bit HDR10／Rec.2020；不再開啟獨立 HDR 視窗，VSR 與 HDR 之間也不經 CPU 讀回。
- HDR 畫面會在主視窗中央的專用顯示區呈現，上下資訊列保持可見；從 HDR 直接切到純 VSR 時，會先安全結束舊的 TrueHDR 工作再啟動 VSR，畫面與翻頁不會卡在上一張 HDR 幀。
- 整合顯示會先確認 Windows HDR 已開啟，依螢幕峰值亮度處理，並提供標準、鮮明、柔和三種預設。
- HDR 顯示沿用主程式的資料夾索引與前後各 8 張預讀；可用方向鍵、Page Up／Page Down、Space 或滾輪連續翻圖。TrueHDR 在背景佇列處理，快速翻頁只保留最新請求，不會阻塞主視窗或讓舊結果覆蓋新圖。
- 可同時開啟多個 EinzigViewer 程序；每個程序使用獨立 NGX 實例。本機已驗證兩條 VSR Ultra → TrueHDR 管線可在同一張 RTX 4070 Ti SUPER 同時完成，實際數量與速度取決於 GPU 負載和顯存。
- 一般視窗可按 `M` 最大化並保留標題列、選單與工作列；`F11` 則切換無邊框全螢幕。
- F11 全螢幕時，游標若停留在 EinzigViewer 所在螢幕並閒置 3 秒會自動隱藏；移動游標、開啟右鍵選單、切換到其他程式或離開全螢幕都會立即恢復。游標位於其他螢幕時不會被 EinzigViewer 隱藏。
- 右鍵選單將「RTX 視訊增強(VSR + HDR)」開關與「HDR 模式」分開；HDR 模式子選單只用來選擇標準、鮮明或柔和。
- 「說明 → 語言」可在繁體中文與 English 之間切換；主視窗、右鍵選單、提示及 RTX 視窗會跟隨並記住選擇。

## 執行

一般使用者可從 [GitHub Releases](https://github.com/akaJooooooker/EinzigViewer/releases/latest) 下載 `EinzigViewer-0.7.0-win-x64.zip`，完整解壓縮後直接執行 `EinzigViewer.exe`。請保留 `nvngx_vsr.dll` 與 `nvngx_truehdr.dll` 在執行檔旁，RTX 功能才可使用。壓縮檔內附中英雙語 `README.txt`。

Release 執行檔位於：

```text
out\EinzigViewer.exe
```

可直接啟動後拖入圖片，也可以把圖片路徑當成第一個命令列參數：

```powershell
.\out\EinzigViewer.exe "D:\Manga\001.jpg"
```

## 操作

| 操作 | 功能 |
|---|---|
| `O` | 開啟圖片或 EPUB |
| `V` | 切換無間距直式連續閱讀；普通滾輪在此模式固定捲動 |
| `Ctrl+S` | 以原尺寸轉存一般 sRGB PNG；HDR JXR 會先完成 HDR → SDR 色調映射 |
| `Ctrl+Shift+S` | 輸入目標長邊，保持比例縮小並轉存 PNG |
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

## JXR 與 PNG 轉存

Windows 內建 WIC 原生 JPEG XR codec；JXR 可包含 8／16／32-bit integer，也可包含 HDR／廣色域 fixed-point 或 floating-point 資料。EinzigViewer 對 extended RGB JXR 依 WIC 規則視為 linear scRGB，以 scRGB 參考亮度 80 nits、SDR paper white 203 nits，並以亮度保持的 ACES shoulder 壓縮高光，再做 linear → sRGB 編碼。輸出的 PNG 是一般 8-bit sRGB，適合瀏覽器、社群網站與一般圖片軟體；它不是保留 HDR 的封存格式。

縮小功能使用 WIC `IWICBitmapScaler` 的 Fant interpolation，輸入目標長邊後自動計算另一邊，避免非等比例變形。官方技術參考：[WIC JPEG XR／原生像素格式](https://learn.microsoft.com/en-us/windows/win32/wic/-wic-codec-native-pixel-formats)、[WIC Bitmap Scaling](https://learn.microsoft.com/en-us/windows/win32/wic/-wic-bitmapsources-howto-scale)。

## RTX 使用門檻

- 64 位元 Windows 10 或更新版本。
- GeForce RTX 20 系列或更新，或 NVIDIA RTX 1000 系列或更新的 GPU。
- 支援該顯示卡與 RTX Video SDK 的 NVIDIA 驅動程式；本機 SDK 1.1 文件列出的最低版本是 550.58。
- EinzigViewer 必須實際在 NVIDIA RTX GPU 上執行；雙顯卡筆電可在 Windows 圖形設定或 NVIDIA App 將 EinzigViewer 指定為高效能 NVIDIA GPU。
- VSR 需要 `nvngx_vsr.dll`；整合 HDR 顯示另需 `nvngx_truehdr.dll`。0.6.0 的 HDR10 swap chain 已直接內建於 `EinzigViewer.exe`。
- 整合 HDR 顯示需要 HDR 顯示器，並在「Windows 設定 → 系統 → 顯示器 → HDR」開啟 HDR。

EinzigViewer 是直接呼叫 RTX Video SDK，而不是借用瀏覽器的驅動程式增強開關。因此達到上述門檻後，即使 NVIDIA 控制面板／NVIDIA App 裡的「影片／視訊增強」沒有開啟，EinzigViewer 的 `R` 鍵 RTX 功能仍可使用。該開關主要控制 NVIDIA 驅動替支援的瀏覽器或播放器所做的增強；硬體、驅動、SDK runtime 與程式使用 NVIDIA GPU 的條件仍然不可少。

若電腦沒有相容的 RTX GPU、驅動程式過舊、Windows／螢幕未開啟 HDR，或必要 DLL 遺失，EinzigViewer 會顯示錯誤對話框並在同一個主視窗退回一般圖片檢視，不會因為不支援 RTX 而直接關閉。背景 VSR 與 TrueHDR 工作都有例外攔截；HDR 結果失敗時會釋放 HDR10 swap chain 並恢復 Direct2D。

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
  -DEINZIGVIEWER_RTX_SDK_ROOT="D:\SDK\RTX_Video_SDK_v1.1.0"
```

設定成功時 CMake 會顯示 `EinzigViewer RTX backend: enabled`，並自動把 Release 版 `nvngx_vsr.dll` 複製到執行檔旁。沒有 SDK 時仍可建置一般檢視版。

也可用 Visual Studio 直接開啟此資料夾或 `CMakeLists.txt`。

## 目前限制

- 直式閱讀為 SDR；RTX VSR／HDR 使用單頁模式。EPUB 為圖片閱讀，不呈現文字／CSS 排版或純向量 SVG，不支援 DRM。

- GIF／動態 WebP 目前只顯示第一幀。
- WebP、HEIC、AVIF、JPEG XL 是否可開啟取決於 Windows 已安裝的 WIC codec。
- HDR JXR → PNG 目前使用固定的 SDR tone mapping；尚未提供曝光、paper white 或曲線的手動調整。PNG 輸出固定為 8-bit sRGB。
- 尚未有縮圖瀏覽器、資料夾樹、CBZ、雙頁模式、檔案管理與完整 ICC 色彩管理。
- RTX VSR 與 TrueHDR 已在同一個 D3D11 裝置中以 GPU texture 串接，10-bit HDR swap chain 也已整合回原本的 EinzigViewer 主視窗。
- 目前 RTX 輸出會讀回 CPU 再交給 Direct2D；下一階段將改為 D3D11 texture 零拷貝顯示。
- 第一次開啟 RTX 需初始化 NGX，實測約數秒；後續圖片較快。
- 目前 VSR 結果只保留在記憶體，尚未建立磁碟快取。

## NVIDIA SDK 告知

EinzigViewer 使用 NVIDIA RTX Video SDK。SDK 與 runtime 受 NVIDIA RTX SDKs License 約束，並非本專案的一般原始碼資產。詳見 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

更詳細的設計與路線見 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) 和 [docs/RTX_INTEGRATION.md](docs/RTX_INTEGRATION.md)。
