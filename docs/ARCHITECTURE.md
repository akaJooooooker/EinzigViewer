# 架構與長期路線

## 0.7.0 連續閱讀與 EPUB

- `EpubBook`：miniz 3.1.2 ZIP + Windows XmlLite；背景依 container → OPF manifest/spine → XHTML/SVG 圖片參照讀取，僅將支援的點陣圖寫入 GUID 專屬暫存目錄。URI 正規化、CRC、重複 ZIP 路徑及解壓縮大小檢查；XmlLite 外部 resolver 為空且限制實體展開。
- `FolderModel`：同時提供資料夾自然排序或 EPUB 書內順序；失敗開啟不會清除目前內容。EPUB 頁面保留原始路徑標籤。
- `ContinuousLayout`：以頁寬為單位保存每頁比例與 prefix offsets，二分尋找可見頁。尺寸在背景解碼後更新時，保存上緣頁碼及頁內比例，避免跳頁。
- `ContinuousReader`：可見區附近預讀、Win32 捲軸、分段 Direct2D 上傳；只保留可見 tiles，GPU tile 快取限 96 MiB；此模式為 SDR。
- `ImageCache`：768 MiB 硬快取預算；失敗不會反覆重試。排程 Task 保留 EPUB 的 shared lifetime lease，確保切書時仍在工作的解碼器不會讀到已被刪除的暫存檔。
- EPUB 載入使用單一背景 future，可取消／被新開啟取代；成功後才提交至主視窗。主視窗輪詢完成結果，不從工作執行緒操作 UI。
- `ctest --test-dir build -C Release --output-on-failure` 執行 ReaderProbe、ImageIoProbe、ReaderUiProbe。最後者是隱藏視窗的應用程式內部整合測試，不注入全域輸入、不改使用者設定。

## 當前資料流

```text
使用者開啟圖片
      │
      ▼
FolderModel 掃描同資料夾並自然排序
      │
      ▼
ImageCache 依目前頁面距離排定優先順序
      │
      ├── WIC 背景解碼（最多 4 張並行）
      ├── 768 MB LRU RAM 快取
      └── 前後各 8 張預讀；只優先保留目前及緊鄰頁
      │
      ▼
UI 執行緒上傳目前 BGRA 圖片至 Direct2D bitmap
      │
      ▼
縮放、平移與呈現
```

翻頁的硬性原則是「一般解碼版本先顯示」。未來 RTX 增強一律在背景執行，完成後才無縫替換，不能成為導覽的前置條件。

## 模組責任

- `FolderModel`：資料夾索引、格式篩選、自然排序、目前位置。
- `WicDecoder`：每工作執行緒獨立初始化 COM/WIC、EXIF 方向；一般格式輸出 premultiplied BGRA，extended-RGB JXR 則先保留 128bpp float scRGB，再做亮度保持的 HDR → SDR/sRGB 色調映射。
- `ImageExporter`：將完整解碼後的原圖輸出為帶 sRGB 標記的 PNG；可用 WIC Fant interpolation 依目標長邊保持比例縮小，且不接受放大要求。
- `ImageCache`：優先佇列、背景工作、錯誤狀態、LRU 記憶體上限、鄰近頁固定保留。
- `App`：Win32 訊息、Direct2D／HDR10 主視窗呈現模式、輸入、檢視狀態與設定。
- `ImageEnhancer`：SDR VSR 增強後端抽象；沒有 SDK 時由 `NullImageEnhancer` 安全回退。
- `RtxHdrProcessor`：同一 D3D11 裝置內串接 VSR Ultra 與 TrueHDR texture。
- `RtxHdrPresenter`：主視窗內容區子視窗的 HDR10 swap chain、背景最新請求佇列、螢幕 HDR 驗證與 SDR 回復。

## 功能發展階段

### 0.1 高速檢視核心（已完成）

開圖、資料夾索引、自然排序、背景預讀、快取、縮放、全螢幕與設定保存。

### 0.2 漫畫閱讀

- CBZ／ZIP 直接閱讀與壓縮檔內預讀。
- 單頁／雙頁模式。
- 左到右／右到左閱讀方向。
- 長條漫畫連續捲動。
- 獨立設定檔與閱讀進度。

### 0.3 圖片瀏覽與管理

- 左側資料夾樹、中央縮圖格、檢視器模式快速切換。
- SQLite 縮圖磁碟快取，依檔案路徑、大小與修改時間失效。
- 大型資料夾增量載入與 FileSystemWatcher 更新。
- 收藏、評分、標籤與最近資料夾。

### 0.4 RTX 增強（第一階段已完成）

- 已在 RTX 4070 Ti SUPER 驗證 SDK 1.1：320×180 → 1280×720，以及 960×540 → 3840×2160 VSR Ultra 均成功。
- 只在原圖確實小於顯示尺寸時排程超解析度。
- 目前圖最高優先，下一圖第二優先；快速翻頁取消過時增強。
- 已完成 R 鍵開關增強，以及按住 C 暫時顯示原圖比較。
- 0.4.2 使用主視窗內的專用 HDR 子畫面承載 10-bit HDR10 swap chain，上下資訊列保持可見；TrueHDR 背景佇列只保留最新翻頁請求，離開 HDR 時等待舊工作安全退出後再啟動純 VSR。
- 0.4.3 在 F11 全螢幕加入螢幕感知的 3 秒游標隱藏計時，並由右鍵 HDR 子選單共用主選單的 HDR 開關與三種預設命令。
- 0.4.4 將右鍵的 VSR + HDR 開關與 HDR 模式子選單拆開；模式子選單只送出標準、鮮明、柔和三個預設命令。
- GPU／磁碟結果快取仍待加入，預計以來源雜湊、輸出尺寸、模型與品質作為 key。

### 0.5 格式與色彩

- 0.6.0 已完成 extended-RGB JXR／WDP／HDP 的 128bpp float scRGB 解碼、固定 HDR → SDR/sRGB 色調映射、原尺寸 PNG 轉存與 WIC Fant 保持比例縮小轉存。
- 動態 GIF／WebP、HEIF sequence。
- 內建 WebP／AVIF／JPEG XL codec，不再依賴系統擴充套件。
- RAW 預覽與完整解碼分級。
- LittleCMS／Windows Color System ICC 管線。
- 可調式 JXR tone mapping、16-bit PNG 輸出、完整 scRGB 與 HDR10 檔案顯示路徑。

### 0.6 檔案管理與可靠性

- 複製、移動、重新命名；刪除預設進資源回收筒並提供復原提示。
- 檔案變更監看、網路磁碟與離線磁碟處理。
- 崩潰回報、解碼逾時、惡意／損壞圖片隔離。
- 大量真實資料夾的效能與記憶體回歸測試。

## 關鍵驗收指標

- 預讀命中時，按鍵到畫面切換的 P95 小於 16 ms。
- HDD 上順序閱讀時，預讀命中率至少 95%。
- 快速連按翻頁時，記憶體不持續成長，舊工作不壓過目前頁。
- RTX 關閉、失敗或不支援時，基本檢視器功能完全不受影響。
- EXIF 方向、透明 PNG、超大圖與損壞檔案不造成程序崩潰。
