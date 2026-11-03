# Real-CUGAN local runtime

Einzig Viewer uses the official [nihui/realcugan-ncnn-vulkan 20220728 Windows release](https://github.com/nihui/realcugan-ncnn-vulkan/releases/tag/20220728) for optional, local AI super resolution. Model files and executables are development/runtime assets in ignored directories, not tracked source files. The viewer does not download them automatically.

Run `powershell -File scripts/Fetch-AiModels.ps1` from the repository to populate `out/ai/realcugan/`. Use `-Offline` to require the already verified local ZIP; `-DestinationDirectory` can select another local development output directory. The script validates the complete archive before inspecting every member and extracting only the selected assets. It retains its verified archive and selected files under `.sdk/realcugan/`.

## Pinned source

- URL: https://github.com/nihui/realcugan-ncnn-vulkan/releases/download/20220728/realcugan-ncnn-vulkan-20220728-windows.zip
- Archive size: 45,977,449 bytes.
- SHA-256: `C6E08D46C11704B1E3A1ADA9DDD591CB5005F52F132136C8633BA25DEF400E01`.
- Release source commit: `395302c5c70f1bff604c974e92e0a87e45c9f9ee`.
- Executable: `out/ai/realcugan/realcugan-ncnn-vulkan.exe`.
- Required bundled dependency: `out/ai/realcugan/vcomp140.dll` (the original, unmodified release file).
- Model directory: `out/ai/realcugan/models-se/`, with `up2x-no-denoise.bin/.param` and `up4x-no-denoise.bin/.param` only.

Runtime arguments are `-g 0 -n 0 -s 2` (or `4`) `-t 128 -j 1:1:1 -c 1 -f png`, with explicit input, output and model-directory paths. GPU IDs are nonnegative; there is no implicit CPU fallback. A working Vulkan driver is provided by the system/GPU vendor. No Vulkan SDK or global redistributable is installed by the fetch script.

The viewer exposes 2× and 4×. Background batches use directory inputs, one child at a time, under adaptive RAM/commit/GPU/disk quotas, a hard Job Object committed-memory limit and a shared 120-second deadline. Resource failures may retry once with a single page and tile64. Unknown adapter telemetry keeps conservative single-page operation.

## Retained notices

The original release includes only its main `LICENSE` and `README.md`; both are copied verbatim alongside the selected runtime. `LICENSE.txt` here is the same upstream Real-CUGAN ncnn Vulkan MIT license. Additional upstream notices are retained in `licenses/` and copied beside the runtime:

| Component | Source revision | Retained text |
| --- | --- | --- |
| Real-CUGAN model project, bilibili | [`680c4a26444f0ff2c7c6bae3b0712f3b478c8184`](https://github.com/bilibili/ailab/blob/680c4a26444f0ff2c7c6bae3b0712f3b478c8184/Real-CUGAN/LICENSE) (model-project license commit) | `Real-CUGAN-MIT.txt` |
| ncnn, Tencent | [`066614351391d309c96ae1e00c6fb1bd873b4949`](https://github.com/Tencent/ncnn/blob/066614351391d309c96ae1e00c6fb1bd873b4949/LICENSE.txt) (release submodule) | `ncnn-LICENSE.txt`, including bundled notices |
| glslang, Khronos | [`86ff4bca1ddc7e2262f119c16e7228d0efb67610`](https://github.com/KhronosGroup/glslang/blob/86ff4bca1ddc7e2262f119c16e7228d0efb67610/LICENSE.txt) (ncnn submodule) | Complete `glslang-LICENSE.txt` omnibus |
| libwebp, WebM Project | [`b9d2f9cd3bec5b0970edeb11ea03c0a4ea06e332`](https://github.com/webmproject/libwebp/tree/b9d2f9cd3bec5b0970edeb11ea03c0a4ea06e332) (release submodule) | `libwebp-COPYING.txt`, `libwebp-PATENTS.txt` |
| Microsoft Visual C++ 2015–2022 Runtime | Microsoft official runtime license | `Microsoft-Visual-C-Runtime-2015-2022.txt` |

These dependency revisions are recorded in the official release source tree. The Windows build uses WIC for PNG/JPEG and links ncnn/libwebp; stb's non-Windows image path is not used. The full upstream license collections are preserved, including entries for optional source/build components, without claiming every optional component is linked into this binary.

The [official Windows release workflow](https://github.com/nihui/realcugan-ncnn-vulkan/blob/395302c5c70f1bff604c974e92e0a87e45c9f9ee/.github/workflows/release.yml) copies `vcomp140.dll` from its build machine's Windows system directory. Its Microsoft terms are separate from the MIT licenses. The retained text comes from Microsoft's [Visual C++ Runtime 2015–2022 license](https://visualstudio.microsoft.com/license-terms/vs2022-cruntime/), extracted in paragraph order from the linked official DOCX (SHA-256 `F1E3D56CEB2AD68AAE0711B910375009E651AC5530FA0760F0DEA6E81E54FAE1`). Microsoft's [Visual Studio 2022 redistribution page](https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution) describes redistribution conditions.

## Runtime requirements and limitations

A working Vulkan GPU and driver are required. Missing drivers, invalid GPU IDs, insufficient GPU memory, large images or output/time limits can prevent processing. The models may invent or simplify details and cannot recover information absent from the original.
