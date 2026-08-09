# RTX 增強接入策略

目前程式已接入 NVIDIA RTX Video SDK 1.1 的 D3D11 VSR，並保留 `ImageEnhancer` 介面讓一般版與 RTX 版使用同一套 UI。SDK 套件位於 Git 忽略的 `.sdk/rtx-video-1.1.0`；CMake 找不到套件時會自動回退 `NullImageEnhancer`。

## 已驗證結果

- GPU：NVIDIA GeForce RTX 4070 Ti SUPER，16 GB。
- 驅動：596.49，高於指南列出的 550.58 最低版本。
- 初始 probe：320×180 → 1280×720 BGRA SDR，成功。
- 4K probe：960×540 → 3840×2160 BGRA SDR，成功。
- VSR：Ultra，品質等級 4。
- 初始 probe 首次初始化＋處理：2,906 ms；像素雜湊 `0x56B9CAD7CFEB2F1D`。
- 4K probe 首次初始化＋處理：1,888 ms；像素雜湊 `0x2255E00658D025F`。

## 第一個技術驗證

1. 下載目標版本的 NVIDIA RTX Video SDK／NGX SDK 與官方 D3D11 範例。（已完成）
2. 確認 SDK 1.1 能在 RTX 4070 Ti SUPER 初始化並處理影像。（已完成）
3. 對同一批漫畫頁、專輯封面與照片比較：
   - Direct2D 線性縮放。
   - 高品質傳統縮放＋適度銳化。
   - Image Super Resolution（若套件提供且可用）。
   - Video Super Resolution 單畫格（若只有此路徑可用）。
4. 記錄支援的輸入／輸出格式、倍率、最小與最大尺寸、初始化時間、單張延遲及顯存用量。

這個驗證已通過，SDK 目前以可選 CMake 後端納入主程式。

## 實作順序

1. 已用獨立 `RtxImageEnhancer` 隔離 SDK 標頭與生命週期。
2. 已實作 CPU BGRA 輸入／輸出的背景版本，驗證品質、翻頁 generation 與關閉流程。
3. TrueHDR 呈現層已升級為原主視窗的 D3D11 HDR10 swap chain；SDR VSR 的 Direct2D texture 零拷貝仍是下一步。
4. 建立 `EnhancementCache`，與一般解碼快取分開計算 GPU 預算。
5. 加入取消 token 與 generation；翻頁後舊頁結果不可覆蓋新頁。
6. UI 加入 R 開關、品質、按住顯示原圖、分割比較與失敗原因。

## RTX 視訊增強（VSR + HDR）

- 主程式按 `H` 會讓原本的 Eidolyx 主視窗切換為 10-bit HDR10 swap chain；同一個 D3D11 裝置與 NGX 生命週期依序執行 VSR Ultra → TrueHDR，再按 `H` 會立即恢復 Direct2D。
- TrueHDR 輸入是 BGRA8 Rec.709 SDR，輸出為 `DXGI_FORMAT_R10G10B10A2_UNORM`。
- 主視窗使用 flip-model swap chain 並設定 `DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020`，不是在 SDR 畫面上模擬加亮。
- 啟動前透過 `IDXGIOutput6::GetDesc1` 確認 Windows HDR，並把螢幕峰值亮度限制在 SDK 接受的 400–2000 nits。
- 「RTX → HDR 預設」提供標準、鮮明、柔和三組 TrueHDR contrast／saturation／middle-gray 參數；`1`／`2`／`3` 保留主檢視器原本的尺寸模式。
- runtime 是 `nvngx_truehdr.dll`，與 VSR 使用的 `nvngx_vsr.dll` 不同。
- VSR 的 BGRA8 GPU texture 直接作為 TrueHDR 輸入，不經 CPU 讀回，最後複製到 HDR10 swap chain。
- TrueHDR evaluate 在背景執行；快速翻頁會覆寫尚未開始的舊請求，完成結果以 generation 核對，因此不會阻塞 Win32 訊息或覆蓋目前頁。

## 排程規則

- 圖片縮小顯示：不跑超解析度。
- 放大幅度小於約 10%：先使用傳統縮放，除非實測顯示 AI 有穩定收益。
- 像素圖：預設使用 nearest／integer scale，不自動跑 RTX。
- 目前頁：最高優先。
- 下一頁：目前頁完成後才預增強。
- 快速翻頁：立即取消看不到的排隊工作；已提交 GPU 的工作可完成，但結果不得套用。
- SDK 初始化或 evaluate 失敗：記錄錯誤並立刻回退一般圖片，不能阻塞或關閉程式。

## SDK 檔案管理

- SDK 與 redistributable 不直接提交 Git，除非授權明確允許。
- CMake 使用 `EIDOLYX_RTX_SDK_ROOT` 指向本機套件。
- 未設定 SDK 時仍要能完整建置一般版；RTX 是可選能力，不是基本檢視器的硬相依。
- 發佈前逐項確認 NVIDIA runtime、模型下載、驅動最低版本與再散布條款。
