#include "EpubBook.h"
#include "FolderModel.h"
#include <Windows.h>
#include <shlwapi.h>
#include <xmllite.h>
#include <wrl/client.h>
#include <miniz.h>
#include <algorithm>
#include <fstream>
#include <functional>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

using Microsoft::WRL::ComPtr;
namespace {
constexpr mz_uint64 MaxEntryBytes = 256ULL * 1024 * 1024;
constexpr mz_uint64 MaxBookBytes = 4ULL * 1024 * 1024 * 1024;
constexpr mz_uint MaxEntries = 50000;
constexpr std::size_t MaxXmlBytes = 8ULL * 1024 * 1024;
struct Failure { std::wstring message; };
void Check(bool okay, const wchar_t* message) { if (!okay) throw Failure{message}; }
void Cancel(const std::atomic_bool& cancelled) {
    Check(!cancelled.load(), L"已取消載入。 / Loading cancelled.");
}
std::wstring Wide(const std::string& text) {
    if (text.empty()) return {};
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    Check(count > 0, L"EPUB 路徑不是有效的 UTF-8。 / Invalid UTF-8 EPUB path.");
    std::wstring output(count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), output.data(), count);
    return output;
}
std::string Utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    Check(count > 0, L"EPUB 路徑無效。 / Invalid EPUB path.");
    std::string output(count, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), output.data(), count, nullptr, nullptr);
    return output;
}
// Resolve archive URIs without ever using their names as output filesystem paths.
std::string Resolve(const std::string& base, std::string ref, bool uri = true) {
    if (uri) {
        ref = ref.substr(0, ref.find_first_of("?#"));
        std::string decoded;
        for (std::size_t i = 0; i < ref.size(); ++i) {
            if (ref[i] == '%') {
                auto hex = [](char c) -> int {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                    return -1;
                };
                Check(i + 2 < ref.size() && hex(ref[i + 1]) >= 0 && hex(ref[i + 2]) >= 0,
                    L"EPUB 路徑編碼無效。 / Invalid EPUB URI escaping.");
                decoded += static_cast<char>(hex(ref[i + 1]) * 16 + hex(ref[i + 2]));
                i += 2;
            } else decoded += ref[i];
        }
        ref = std::move(decoded);
    }
    Check(!ref.empty() && ref[0] != '/' && ref.find(':') == std::string::npos &&
        ref.find('\\') == std::string::npos && ref.find('\0') == std::string::npos,
        L"EPUB 包含外部或不安全的路徑。 / EPUB contains an external or unsafe path.");
    const auto slash = base.find_last_of('/');
    const std::string combined = (slash == std::string::npos ? "" : base.substr(0, slash + 1)) + ref;
    std::vector<std::string> parts;
    for (std::size_t start = 0; start < combined.size();) {
        auto end = combined.find('/', start);
        if (end == std::string::npos) end = combined.size();
        const auto part = combined.substr(start, end - start);
        if (part == "..") {
            Check(!parts.empty(), L"EPUB 路徑超出書籍範圍。 / EPUB path escapes the archive.");
            parts.pop_back();
        } else if (!part.empty() && part != ".") parts.push_back(part);
        start = end + 1;
    }
    std::string result;
    for (const auto& part : parts) { if (!result.empty()) result += '/'; result += part; }
    Check(!result.empty(), L"EPUB 路徑為空。 / Empty EPUB path.");
    return result;
}
struct Zip {
    std::ifstream input;
    mz_zip_archive archive{};
    std::unordered_map<std::string, mz_uint> entries;
    ~Zip() { if (archive.m_pState) mz_zip_reader_end(&archive); }
    void Open(const std::filesystem::path& path, const std::atomic_bool& cancelled) {
        input.open(path, std::ios::binary | std::ios::ate);
        Check(input.good(), L"無法開啟 EPUB。 / Could not open EPUB.");
        const auto size = input.tellg();
        Check(size > 0 && static_cast<mz_uint64>(size) <= MaxBookBytes,
            L"EPUB 為空或超過 4 GiB。 / EPUB is empty or exceeds 4 GiB.");
        archive.m_pIO_opaque = &input;
        archive.m_pRead = [](void* opaque, mz_uint64 offset, void* buffer, size_t bytes) -> size_t {
            auto& stream = *static_cast<std::ifstream*>(opaque);
            stream.clear(); stream.seekg(static_cast<std::streamoff>(offset));
            stream.read(static_cast<char*>(buffer), static_cast<std::streamsize>(bytes));
            return static_cast<size_t>(stream.gcount());
        };
        Check(mz_zip_reader_init(&archive, static_cast<mz_uint64>(size), 0) != 0,
            L"EPUB ZIP 結構損壞。 / Invalid EPUB ZIP structure.");
        const auto count = mz_zip_reader_get_num_files(&archive);
        Check(count <= MaxEntries, L"EPUB 項目過多（上限 50,000）。 / Too many EPUB entries (limit 50,000).");
        mz_uint64 total = 0;
        for (mz_uint index = 0; index < count; ++index) {
            Cancel(cancelled);
            mz_zip_archive_file_stat stat{};
            Check(mz_zip_reader_file_stat(&archive, index, &stat) != 0, L"EPUB 索引損壞。 / Corrupt EPUB index.");
            if (stat.m_is_directory) continue;
            Check(!stat.m_is_encrypted && stat.m_is_supported,
                L"不支援加密或此壓縮方式的 EPUB。 / Encrypted or unsupported EPUB compression.");
            Check(stat.m_uncomp_size <= MaxEntryBytes && total <= MaxBookBytes - stat.m_uncomp_size,
                L"EPUB 解壓縮大小超過限制（單檔 256 MiB，總計 4 GiB）。 / EPUB extraction size limit exceeded (256 MiB per entry, 4 GiB total).");
            total += stat.m_uncomp_size;
            const auto length = mz_zip_reader_get_filename(&archive, index, nullptr, 0);
            Check(length > 1 && length <= 4096, L"EPUB 檔名過長或無效。 / Invalid EPUB filename.");
            std::string name(length, '\0');
            mz_zip_reader_get_filename(&archive, index, name.data(), length);
            name.pop_back();
            (void)Wide(name);
            const auto normalized = Resolve("", name, false);
            Check(entries.emplace(normalized, index).second, L"EPUB 有重複路徑。 / Duplicate EPUB paths.");
        }
    }
    std::vector<unsigned char> Read(const std::string& name, std::size_t limit) {
        const auto found = entries.find(name);
        Check(found != entries.end(), L"EPUB 遺失參照的檔案。 / EPUB references a missing file.");
        mz_zip_archive_file_stat stat{};
        Check(mz_zip_reader_file_stat(&archive, found->second, &stat) && stat.m_uncomp_size <= limit,
            L"EPUB 項目過大或損壞。 / EPUB entry is too large or corrupt.");
        std::vector<unsigned char> bytes(static_cast<std::size_t>(stat.m_uncomp_size));
        Check(mz_zip_reader_extract_to_mem(&archive, found->second, bytes.data(), bytes.size(), 0) != 0,
            L"EPUB 解壓縮或 CRC 校驗失敗。 / EPUB decompression or CRC check failed.");
        return bytes;
    }
};
using Attributes = std::map<std::wstring, std::wstring>;
std::wstring Attribute(const Attributes& attrs, const wchar_t* name) {
    const auto found = attrs.find(name); return found == attrs.end() ? L"" : found->second;
}
void Xml(const std::vector<unsigned char>& bytes,
    const std::function<void(const std::wstring&, const Attributes&)>& element) {
    ComPtr<IStream> stream;
    stream.Attach(SHCreateMemStream(bytes.data(), static_cast<UINT>(bytes.size())));
    ComPtr<IXmlReader> reader;
    Check(stream && SUCCEEDED(CreateXmlReader(__uuidof(IXmlReader), &reader, nullptr)),
        L"無法建立 EPUB XML 讀取器。 / Could not create EPUB XML reader.");
    // EPUB 2 commonly declares a DOCTYPE; XmlLite has no external resolver
    // by default. Keep it null and bound internal entity expansion.
    reader->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Parse);
    reader->SetProperty(XmlReaderProperty_XmlResolver, 0);
    reader->SetProperty(XmlReaderProperty_MaxEntityExpansion, 4096);
    reader->SetProperty(XmlReaderProperty_MaxElementDepth, 128);
    Check(SUCCEEDED(reader->SetInput(stream.Get())), L"EPUB XML 無效。 / Invalid EPUB XML.");
    XmlNodeType type{};
    HRESULT result;
    while ((result = reader->Read(&type)) == S_OK) {
        if (type != XmlNodeType_Element) continue;
        const wchar_t* name = nullptr;
        reader->GetLocalName(&name, nullptr);
        const std::wstring local = name ? name : L"";
        Attributes attrs;
        if (reader->MoveToFirstAttribute() == S_OK) {
            do {
                const wchar_t* key = nullptr; const wchar_t* value = nullptr;
                reader->GetLocalName(&key, nullptr); reader->GetValue(&value, nullptr);
                if (key && value) attrs[key] = value;
            } while (reader->MoveToNextAttribute() == S_OK);
            reader->MoveToElement();
        }
        element(local, attrs);
    }
    Check(result == S_FALSE, L"EPUB XML 損壞或含不支援的實體。 / Invalid EPUB XML or unsupported entities.");
}
}

EpubBook::~EpubBook() {
    if (extractionDirectory_.empty()) return;
    std::error_code ec;
    // This exact path was created by us, with a fresh GUID. No book-supplied names.
    std::filesystem::remove_all(extractionDirectory_, ec);
}

std::shared_ptr<EpubBook> EpubBook::Open(const std::filesystem::path& path,
    const std::atomic_bool& cancelled, std::wstring& error) {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    struct ComScope { HRESULT result; ~ComScope() { if (SUCCEEDED(result)) CoUninitialize(); } } scope{com};
    try {
        auto book = std::make_shared<EpubBook>();
        book->source = std::filesystem::absolute(path);
        Zip zip; zip.Open(path, cancelled);
        const auto mime = zip.Read("mimetype", 128);
        Check(std::string(mime.begin(), mime.end()) == "application/epub+zip",
            L"檔案不是有效的 EPUB。 / Not a valid EPUB file.");
        std::unordered_set<std::string> encrypted;
        if (zip.entries.contains("META-INF/encryption.xml")) {
            Xml(zip.Read("META-INF/encryption.xml", MaxXmlBytes), [&](const auto& name, const auto& attrs) {
                if (name == L"CipherReference") encrypted.insert(Resolve("", Utf8(Attribute(attrs, L"URI"))));
            });
        }
        std::string package;
        Xml(zip.Read("META-INF/container.xml", MaxXmlBytes), [&](const auto& name, const auto& attrs) {
            if (name == L"rootfile" && package.empty()) package = Resolve("", Utf8(Attribute(attrs, L"full-path")));
        });
        Check(!package.empty(), L"EPUB 缺少套件文件。 / Missing EPUB package document.");
        Check(!encrypted.contains(package), L"不支援 DRM 加密 EPUB。 / DRM-encrypted EPUB is unsupported.");
        struct Item { std::string path; std::wstring type; };
        std::unordered_map<std::wstring, Item> manifest;
        std::vector<std::wstring> spine;
        Xml(zip.Read(package, MaxXmlBytes), [&](const auto& name, const auto& attrs) {
            if (name == L"item") {
                const auto href = Utf8(Attribute(attrs, L"href"));
                // Remote resources are not needed by an offline image reader.
                if (href.find(':') != std::string::npos || href.starts_with("//")) return;
                manifest.emplace(Attribute(attrs, L"id"), Item{Resolve(package, href), Attribute(attrs, L"media-type")});
            } else if (name == L"itemref" && Attribute(attrs, L"linear") != L"no") {
                spine.push_back(Attribute(attrs, L"idref"));
            }
        });
        Check(!spine.empty(), L"EPUB 缺少閱讀順序。 / Missing EPUB reading order.");
        std::vector<std::string> images;
        std::unordered_set<std::string> stack;
        std::function<void(const std::string&, unsigned)> collect;
        collect = [&](const std::string& document, unsigned depth) {
            Cancel(cancelled);
            Check(!encrypted.contains(document), L"書頁或圖片受 DRM 加密，無法讀取。 / A page or image is DRM-encrypted.");
            Check(depth <= 8 && stack.insert(document).second, L"EPUB 文件參照過深或循環。 / Recursive EPUB document reference.");
            Xml(zip.Read(document, MaxXmlBytes), [&](const auto& name, const auto& attrs) {
                std::wstring reference;
                if (name == L"img") reference = Attribute(attrs, L"src");
                else if (name == L"image") reference = Attribute(attrs, L"href"); // SVG xlink:href/local href
                else if (name == L"object") reference = Attribute(attrs, L"data");
                if (reference.empty()) return;
                const auto ref = Utf8(reference);
                if (ref.find(':') != std::string::npos || ref.starts_with("//")) return;
                const auto resolved = Resolve(document, ref);
                Check(!encrypted.contains(resolved), L"EPUB 圖片受 DRM 加密。 / EPUB image is DRM-encrypted.");
                if (FolderModel::IsSupportedImage(std::filesystem::path(Wide(resolved)))) images.push_back(resolved);
                else if (std::filesystem::path(Wide(resolved)).extension() == L".svg") collect(resolved, depth + 1);
                Check(images.size() <= MaxEntries, L"EPUB 圖片數量過多。 / Too many EPUB images.");
            });
            stack.erase(document);
        };
        for (const auto& id : spine) {
            Cancel(cancelled);
            const auto found = manifest.find(id);
            Check(found != manifest.end(), L"EPUB 閱讀順序有遺失項目。 / Missing EPUB spine item.");
            const auto& item = found->second;
            if (item.type == L"application/xhtml+xml" || item.type == L"image/svg+xml" || item.type == L"text/html") collect(item.path, 0);
            else if (FolderModel::IsSupportedImage(std::filesystem::path(Wide(item.path)))) images.push_back(item.path);
        }
        Check(!images.empty(), L"EPUB 中沒有可讀取的點陣圖片；此功能不呈現文字、CSS 或純向量 SVG。 / No supported raster images in EPUB; text, CSS and vector-only SVG are not rendered.");
        GUID guid{}; Check(SUCCEEDED(CoCreateGuid(&guid)), L"無法建立暫存識別碼。 / Could not create temporary ID.");
        wchar_t guidText[40]{}; StringFromGUID2(guid, guidText, 40);
        auto directory = std::filesystem::temp_directory_path() / (L"EinzigViewer-epub-" + std::wstring(guidText));
        Check(std::filesystem::create_directory(directory), L"無法建立 EPUB 暫存目錄。 / Could not create EPUB temporary directory.");
        book->extractionDirectory_ = directory;
        std::unordered_map<std::string, std::filesystem::path> extracted;
        for (const auto& name : images) {
            Cancel(cancelled);
            Check(!encrypted.contains(name), L"EPUB 圖片受 DRM 加密。 / EPUB image is DRM-encrypted.");
            auto found = extracted.find(name);
            if (found == extracted.end()) {
                const auto bytes = zip.Read(name, static_cast<std::size_t>(MaxEntryBytes));
                const auto output = directory / (std::to_wstring(extracted.size()) + std::filesystem::path(Wide(name)).extension().wstring());
                std::ofstream file(output, std::ios::binary | std::ios::trunc);
                file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                file.close();
                Check(file.good(), L"無法寫入 EPUB 暫存圖片（磁碟可能已滿）。 / Could not write EPUB image; disk may be full.");
                found = extracted.emplace(name, output).first;
            }
            book->pages.push_back(found->second);
            book->labels.push_back(Wide(name));
        }
        return book;
    } catch (const Failure& failure) { error = failure.message; }
    catch (const std::exception&) { error = L"EPUB 讀取失敗：檔案、磁碟或記憶體不足。 / EPUB load failed: file, disk or memory error."; }
    return {};
}
