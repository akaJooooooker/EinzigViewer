# EinzigViewer v1.0.0

## 繁體中文

### 主要功能

- 單頁與直式連續閱讀，支援圖片與 EPUB。
- 單頁旋轉、原圖複製、框選複製與 PNG 轉存。
- 本機 AI 2×／4×、RTX VSR／HDR，以及新版提示。

### AI 功能仍在測試與改進中

速度與畫質會持續調整。首次處理、大圖與 4× 可能需要明顯等待，目前不保證即時處理；時間隨圖片、硬體及其他負載而異。已完成快取切換較快。AI 可能改動極小文字、網點及邊緣，可按住 C 比較原圖。

AI 預設關閉，按 A 手動開啟；提供自動或手動資源設定。需可用 Vulkan GPU，模型已隨包附上。

### 下載與預設圖片檢視器

- **Setup.exe 安裝版**：目前使用者安裝、不需管理員權限，提供捷徑與解除安裝。「說明 → 設定預設圖片檢視器」開啟 Windows 設定，由你指定各格式；安裝不會改變既有預設程式。
- **ZIP 免安裝版**：完整解壓縮，保留 DLL、ai 資料夾與 portable.ini。透過 Windows「開啟方式」指定 EinzigViewer.exe，各副檔名分別確認，並保持資料夾位置固定。更新時保留 settings.ini。

### 更新

兩版都有新版提示與手動檢查。下載由瀏覽器開啟，不會自動覆蓋程式；更新前請關閉檢視器。0.7.0 使用者需先手動升級一次。

### 系統需求

Windows 10／11 x64。一般看圖無須 NVIDIA；AI 需要可用 Vulkan GPU，RTX 功能需要相容 NVIDIA RTX GPU，HDR 另需啟用 Windows HDR。

---

## English

### Main features

- Single-page and continuous reading for images and EPUB.
- Per-page rotation, original-image copy, region copy and PNG export.
- Local AI 2×/4×, RTX VSR/HDR and update notifications.

### AI is still being tested and improved

Speed and quality remain under improvement. First-time processing, especially large images and 4×, can take noticeable time; real-time processing is not guaranteed. Time depends on images, hardware and other workloads. Cached results switch faster. AI may alter tiny text, screentones and edges; hold C to compare with the original.

AI starts off; press A to enable it manually. Automatic and manual resource settings are available. A working Vulkan GPU is required, and models are bundled.

### Downloads and the default image viewer

- **Setup.exe installer**: installs for the current user without administrator rights, with shortcuts and an uninstaller. Help → Set as default image viewer opens Windows Settings so you can choose each format; installing does not change existing defaults.
- **Portable ZIP**: extract everything and retain DLLs, the ai directory and portable.ini. Use Windows Open with to select EinzigViewer.exe for each extension, and keep the folder in a fixed location. Preserve settings.ini when updating.

### Updates

Both editions offer update notifications and manual checks. Downloads open a browser and do not replace files automatically; close the viewer before upgrading. Version 0.7.0 requires one manual upgrade.

### Requirements

Windows 10/11 x64. Ordinary viewing does not require NVIDIA hardware; AI requires a working Vulkan GPU, RTX features require compatible NVIDIA RTX hardware, and HDR also requires Windows HDR enabled.
