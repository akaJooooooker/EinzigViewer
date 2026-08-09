EinzigViewer 0.6.0
================

繁體中文
--------

簡介
EinzigViewer 是一款針對漫畫文字、線條與一般圖片放大的原生 64 位元 Windows
圖片檢視器，並整合 NVIDIA RTX Video VSR 與 HDR。VSR 是超解析度放大增強，
不是內容修復；它不會還原原圖沒有的細節，但對低解析度漫畫細字與邊緣通常
特別有幫助。程式支援資料夾自然排序、背景預讀、縮放、平移與全螢幕顯示。
主視窗第一次啟動會在目前螢幕正中央以 960 x 640 開啟，之後會記住自行
調整的大小與位置。

專案更名
自 0.6.0 起，專案由 Eidolyx 更名為 EinzigViewer；0.4.4 與更早版本曾使用
MiraView。Einzig 是德文的「獨特／唯一」，Viewer 表示圖片檢視器。若新版設定
尚未建立，第一次啟動會依序讀取 Eidolyx、MiraView 設定；關閉後改存到
%LOCALAPPDATA%\EinzigViewer\settings.ini。

安裝與啟動
1. 將 EinzigViewer-版本-win-x64.zip 完整解壓縮到同一個資料夾。
2. 執行 EinzigViewer.exe。
3. 按 O 開啟圖片、把圖片拖進視窗，或在命令列把圖片路徑傳給 EinzigViewer.exe。
4. VSR 需要 nvngx_vsr.dll；整合 HDR 顯示另需 nvngx_truehdr.dll，兩者都要
   放在 EinzigViewer.exe 的同一個資料夾。0.6.0 不需要獨立 HDR 執行檔。

Windows SmartScreen 可能因為此版本尚未使用商業程式碼簽章而顯示警告。
請只從本專案的官方 GitHub Release 下載；若確認來源正確，可選擇「其他資訊」
再按「仍要執行」。

支援格式
- 內建：JPG/JPEG、PNG、BMP、GIF 第一幀、TIFF、ICO、JPEG XR。
- WebP、HEIC、AVIF 等格式取決於 Windows 是否已安裝對應的 WIC codec。
- HDR JXR/WDP/HDP 的 fixed-point 或 floating-point extended RGB 會先以 128bpp
  float scRGB 解碼，再做 HDR -> SDR/sRGB 色調映射，避免一開始就壓成 8-bit。

PNG 轉存與縮小
- Ctrl+S 或「檔案 -> 轉存 PNG」：以原尺寸儲存一般 8-bit sRGB PNG。
- Ctrl+Shift+S 或「檔案 -> 縮小並轉存 PNG」：輸入目標長邊像素，自動保持
  比例並使用 WIC Fant 高品質縮小；只允許縮小，不會放大原圖。
- 輸出來源是完整解碼後的原圖；不會把 RTX 顯示增強結果誤存進檔案。

操作說明
- O：開啟圖片。
- Ctrl+S：以原尺寸轉存 PNG。
- Ctrl+Shift+S：保持比例縮小並轉存 PNG。
- 左方向鍵 / Page Up / Backspace：上一張。
- 右方向鍵 / Page Down / Space：下一張。
- Home / End：第一張 / 最後一張。
- 1：適合視窗。
- 2：適合寬度。
- 3：原始大小（100%）。
- + / -：放大 / 縮小。
- 滑鼠左鍵拖曳：平移圖片。
- F11 或雙擊：切換全螢幕。
- F11 全螢幕閒置 3 秒後，游標在 EinzigViewer 所在螢幕時會自動隱藏；移動、
  開啟右鍵選單、切換程式或離開全螢幕會恢復，其他螢幕的游標不會被隱藏。
- M：切換視窗最大化，保留標題列、選單與工作列。
- I：顯示或隱藏資訊列。
- R：開啟或關閉「RTX VSR 超解析度」。
- H：在原本主視窗開啟或關閉「RTX 視訊增強(VSR + HDR)」。
- 按住 C：暫時顯示原圖；放開後恢復 RTX VSR 結果。
- 右鍵：開啟快速功能選單。「RTX 視訊增強(VSR + HDR)」只負責開關；
  獨立的「HDR 模式」子選單只包含標準、鮮明與柔和。
- 從「說明 -> 語言」選擇繁體中文或 English；程式會記住選擇。

VSR + HDR 整合顯示操作
- 原本的 EinzigViewer 主視窗會由 10-bit HDR10 swap chain 接管，不再另開 HDR 視窗；
  VSR Ultra 的 GPU texture 會直接交給 TrueHDR，再輸出 HDR10／Rec.2020。
- HDR 圖片使用主視窗內的專用顯示區，上下資訊列保持可見。從 HDR 直接切回
  純 VSR 時，程式會先結束舊 TrueHDR 工作，避免畫面或翻頁卡在上一張 HDR 幀。
- 左右鍵 / Page Up / Page Down / Space / 滾輪：瀏覽同資料夾圖片；程式沿用主
  檢視器前後各 8 張預讀，TrueHDR 在背景處理，快速翻頁只保留最新請求。
- 可同時開啟多個 EinzigViewer 程序；每個程序使用獨立 NGX
  實例。可同時處理的數量與速度取決於 GPU 負載和顯存。
- M：視窗最大化；F11：無邊框全螢幕；1／2／3 保留為原本的檢視尺寸快捷鍵。
  TrueHDR 強度請從「RTX -> HDR 預設」選擇標準、鮮明或柔和。
- 必須先在 Windows 顯示器設定中開啟 HDR；程式會選用主視窗所在的 HDR 螢幕。

滑鼠滾輪模式
從「檢視 -> 滑鼠滾輪」選擇：
- 「上一張／下一張」：滾輪用來翻頁。
- 「放大／縮小」：滾輪以游標位置為中心縮放。
選擇會自動保存。無論目前選擇哪一種，Ctrl + 滾輪都會強制縮放。

一般使用門檻
- 64 位元 Windows 10 或更新版本。
- 可正常執行 Direct2D 與 WIC 的顯示環境。
- 一般圖片檢視功能不需要 NVIDIA RTX 顯示卡。

RTX Video VSR 使用門檻
- GeForce RTX 20 系列或更新，或 NVIDIA RTX 1000 系列或更新的 GPU。
- 支援 RTX Video SDK 的 NVIDIA 驅動程式；SDK 1.1 文件標示最低 550.58。
- EinzigViewer 必須實際使用 NVIDIA RTX GPU。雙顯卡筆電請在 Windows 圖形設定或
  NVIDIA App 將 EinzigViewer.exe 設為「高效能」。
- nvngx_vsr.dll 必須與 EinzigViewer.exe 位於同一資料夾。
- 整合 HDR 顯示需要 nvngx_truehdr.dll、HDR 顯示器及已開啟的 Windows HDR；
  HDR10 swap chain 已內建於 EinzigViewer.exe，程式會依螢幕回報亮度設定
  400 到 2000 nits。
- NVIDIA 控制面板或 NVIDIA App 裡的「影片／視訊增強」不必開啟。EinzigViewer
  直接呼叫 RTX Video SDK；硬體、驅動、runtime 與 RTX GPU 條件仍不可少。
- 只有圖片需要放大時才會執行 VSR。輸出依目前顯示尺寸計算，4K 顯示可產生
  接近 3840 x 2160 的輸出，最高單邊 7680 像素。

不支援 RTX 時
- 沒有相容 RTX GPU、驅動過舊、HDR 未開啟或必要 DLL 遺失時，程式會顯示
  錯誤對話框並繼續以一般圖片檢視模式運作，不會直接關閉。
- HDR 與 VSR 都使用背景工作與例外攔截；HDR 初始化或處理失敗時會釋放 HDR10
  swap chain，在同一個主視窗恢復一般 Direct2D 圖片顯示。

目前限制
- GIF 與動態 WebP 目前只顯示第一幀。
- HDR JXR -> PNG 目前使用固定 SDR tone mapping，尚未提供曝光、paper white
  或曲線調整；PNG 輸出固定為 8-bit sRGB。
- 尚未包含縮圖瀏覽器、資料夾樹、CBZ、雙頁模式與完整 ICC 色彩管理。
- VSR 與 TrueHDR 已使用同一條 GPU texture 管線，10-bit HDR10 輸出也已整合回
  原本的 EinzigViewer 主視窗。
- 第一次啟用 RTX 需要初始化 NGX，可能需數秒。

第三方軟體
EinzigViewer 使用 NVIDIA RTX Video SDK 1.1。NVIDIA runtime 仍受隨附的 NVIDIA
RTX SDKs LICENSE 約束，並不改採 EinzigViewer 的授權。詳見 THIRD_PARTY_NOTICES.md
與 LICENSES 資料夾。NVIDIA 及相關商標屬 NVIDIA Corporation；本專案不代表
受到 NVIDIA 贊助或背書。


English
-------

Overview
EinzigViewer is a native 64-bit Windows image viewer for enlarged comic text,
linework, and general images, with NVIDIA RTX Video VSR and HDR support. VSR is
super-resolution upscaling, not content restoration: it cannot recover detail
that was never present, but is often especially useful for small comic text and
edges. It also provides natural sorting, prefetching, zoom, pan, and fullscreen.
The main window initially opens centered at a compact 960 x 640, then remembers
your manually adjusted size and position.

Project rename
Starting with 0.6.0, the project is renamed from Eidolyx to EinzigViewer; version
0.4.4 and earlier used MiraView. Einzig means unique in German, and Viewer describes
the application's purpose. If no new settings file exists, the first launch imports
Eidolyx and then MiraView settings; on exit, settings are saved to
%LOCALAPPDATA%\EinzigViewer\settings.ini.

Install and start
1. Extract the entire EinzigViewer-<version>-win-x64.zip archive into one folder.
2. Run EinzigViewer.exe.
3. Press O to open an image, drag an image into the window, or pass an image
   path to EinzigViewer.exe on the command line.
4. VSR requires nvngx_vsr.dll. Integrated HDR also requires nvngx_truehdr.dll;
   keep both beside EinzigViewer.exe. v0.6.0 does not need a separate HDR executable.

Windows SmartScreen may warn because this release does not yet have a commercial
code-signing certificate. Download only from the official GitHub Release for
this project. If the source is correct, choose "More info" and "Run anyway."

Supported formats
- Built in: JPG/JPEG, PNG, BMP, first frame of GIF, TIFF, ICO, and JPEG XR.
- WebP, HEIC, AVIF, and similar formats depend on installed Windows WIC codecs.
- Fixed-point or floating-point extended-RGB JXR/WDP/HDP is decoded through 128bpp
  float scRGB before HDR-to-SDR/sRGB tone mapping instead of being reduced to 8-bit
  at the start of decoding.

PNG export and downscaling
- Ctrl+S or File -> Export PNG: Save a standard 8-bit sRGB PNG at original size.
- Ctrl+Shift+S or File -> Downscale and Export PNG: Enter a target long edge;
  the aspect ratio is preserved and WIC Fant high-quality downscaling is used.
- Export always uses the fully decoded source image, not the RTX presentation result.

Controls
- O: Open an image.
- Ctrl+S: Export a PNG at original size.
- Ctrl+Shift+S: Preserve aspect ratio, downscale, and export a PNG.
- Left / Page Up / Backspace: Previous image.
- Right / Page Down / Space: Next image.
- Home / End: First / last image.
- 1: Fit window.
- 2: Fit width.
- 3: Actual size (100%).
- + / -: Zoom in / out.
- Left mouse drag: Pan the image.
- F11 or double-click: Toggle fullscreen.
- In F11 fullscreen, the cursor hides after three idle seconds only while it is
  on EinzigViewer's monitor. Movement, the context menu, switching apps, or leaving
  fullscreen restores it; a cursor on another monitor is never hidden.
- M: Toggle a maximized window while keeping the title bar, menu, and taskbar.
- I: Show or hide the information bar.
- R: Enable or disable "RTX VSR Super Resolution."
- H: Toggle "RTX Video Enhancement (VSR + HDR)" in the original main window.
- Hold C: Temporarily show the original image; release it to restore RTX VSR.
- Right-click: Open the quick action menu. “RTX Video Enhancement (VSR + HDR)”
  is only the toggle; the separate “HDR mode” submenu contains only Standard,
  Vivid, and Gentle.
- Choose Traditional Chinese or English under Help -> Language. The selection
  is saved automatically.

Integrated VSR + HDR controls
- The original EinzigViewer main window is taken over by a 10-bit HDR10 swap chain;
  no separate HDR window opens. The VSR Ultra GPU texture is passed directly to
  TrueHDR and then to HDR10/Rec.2020 output.
- HDR uses a dedicated content surface inside the main window, keeping both
  information bars visible. A direct HDR-to-VSR switch retires the old TrueHDR
  task first so the display and navigation cannot stick on the last HDR frame.
- Left/Right, Page Up/Page Down, Space, or the wheel navigates the indexed
  folder. The main viewer's eight-image prefetch on either side is reused.
  TrueHDR runs in the background, and rapid navigation retains only the latest
  request.
- Multiple EinzigViewer processes can run at once, each with an independent NGX
  instance. Practical concurrency and speed depend on GPU load and VRAM.
- M: Maximize; F11: Borderless fullscreen. Keys 1/2/3 retain the original view
  size actions. Choose Standard, Vivid, or Gentle under RTX -> HDR preset.
- Windows HDR must be enabled first. The integrated window uses the HDR display
  containing the main EinzigViewer window.

Mouse wheel mode
Choose one under View -> Mouse Wheel:
- Previous/Next: Use the wheel to navigate images.
- Zoom In/Out: Zoom around the pointer position.
The selection is saved automatically. Ctrl + wheel always zooms in either mode.

General requirements
- 64-bit Windows 10 or later.
- A display environment capable of running Direct2D and WIC.
- Normal image viewing does not require an NVIDIA RTX GPU.

RTX Video VSR requirements
- GeForce RTX 20 series or newer, or NVIDIA RTX 1000 series or newer.
- A compatible NVIDIA driver; the SDK 1.1 documentation lists 550.58 minimum.
- EinzigViewer must run on the NVIDIA RTX GPU. On dual-GPU laptops, select the
  high-performance NVIDIA GPU for EinzigViewer.exe in Windows Graphics settings or
  the NVIDIA App.
- nvngx_vsr.dll must remain beside EinzigViewer.exe.
- Integrated HDR output requires nvngx_truehdr.dll, an HDR display, and Windows
  HDR enabled. The HDR10 swap chain is built into EinzigViewer.exe. Reported display
  luminance is clamped to 400-2000 nits.
- The Video Enhancement switch in NVIDIA Control Panel or the NVIDIA App does
  not need to be enabled. EinzigViewer calls the RTX Video SDK directly; compatible
  hardware, driver, runtime, and use of the RTX GPU are still required.
- VSR runs only when an image needs enlargement. Output follows the current
  display size, can approach 3840 x 2160 on a 4K display, and is capped at 7680
  pixels per side.

When RTX is unsupported
- If no compatible RTX GPU is present, the driver is too old, HDR is disabled,
  or a required DLL is missing, EinzigViewer shows an error dialog and continues in
  normal image-viewing mode instead of closing.
- HDR and VSR both use background work with exception handling. If HDR
  initialization or processing fails, EinzigViewer releases the HDR10 swap chain and
  restores normal Direct2D image viewing in the same main window.

Current limitations
- GIF and animated WebP currently display only the first frame.
- HDR JPEG XR to PNG currently uses a fixed SDR tone map; exposure, paper white,
  and curve controls are not yet exposed. PNG output is always 8-bit sRGB.
- Thumbnail browser, folder tree, CBZ, two-page mode, and complete ICC color
  management are not implemented yet.
- VSR and TrueHDR share one GPU texture pipeline, and 10-bit HDR10 output is now
  integrated into the original EinzigViewer main window.
- The first RTX activation initializes NGX and may take several seconds.

Third-party software
EinzigViewer uses NVIDIA RTX Video SDK 1.1. The NVIDIA runtime remains governed by
the included NVIDIA RTX SDKs LICENSE and is not relicensed under EinzigViewer terms.
See THIRD_PARTY_NOTICES.md and the LICENSES directory. NVIDIA and related marks
belong to NVIDIA Corporation. This project is not sponsored or endorsed by
NVIDIA.

Project: https://github.com/akaJooooooker/EinzigViewer
