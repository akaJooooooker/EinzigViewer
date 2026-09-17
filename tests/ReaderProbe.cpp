#include "EpubBook.h"
#include "FolderModel.h"
#include "ContinuousLayout.h"
#include "ImageCache.h"
#include "ImageExporter.h"
#include "WicDecoder.h"
#include <miniz.h>
#include <Windows.h>
#include <objbase.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void Require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
using Entries = std::vector<std::pair<std::string, std::string>>;
void WriteZip(const std::filesystem::path& path, const Entries& entries) {
    mz_zip_archive zip{};
    Require(mz_zip_writer_init_heap(&zip, 0, 0) != 0, "zip init");
    for (const auto& [name, data] : entries) {
        Require(mz_zip_writer_add_mem(&zip, name.c_str(), data.data(), data.size(), name == "mimetype" ? 0 : MZ_BEST_COMPRESSION) != 0, "zip add");
    }
    void* bytes = nullptr; size_t size = 0;
    Require(mz_zip_writer_finalize_heap_archive(&zip, &bytes, &size) != 0, "zip finish");
    std::ofstream file(path, std::ios::binary);
    file.write(static_cast<const char*>(bytes), static_cast<std::streamsize>(size));
    mz_free(bytes); mz_zip_writer_end(&zip);
    Require(file.good(), "zip write");
}
std::string Bytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
void Page(const std::filesystem::path& path, unsigned width, unsigned height, unsigned color) {
    ImageData image; image.width = width; image.height = height; image.stride = width * 4;
    image.pixels.resize(static_cast<std::size_t>(image.stride) * height);
    for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) {
        auto offset = static_cast<std::size_t>(y) * image.stride + x * 4;
        // Horizontal stripes visibly align with page boundaries and tall-image tiles.
        const bool line = y % 100 < 4 || x % 100 < 4;
        image.pixels[offset] = static_cast<std::byte>(line ? 32 : color & 255);
        image.pixels[offset + 1] = static_cast<std::byte>(line ? 32 : (color >> 8) & 255);
        image.pixels[offset + 2] = static_cast<std::byte>(line ? 32 : (color >> 16) & 255);
        image.pixels[offset + 3] = std::byte{255};
    }
    std::wstring error; Require(ImageExporter::SavePng(image, path, 0, error), "page PNG");
}
}

int wmain(int argc, wchar_t** argv) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    try {
        const auto root = std::filesystem::absolute(argc > 1 ? argv[1] : L"tmp/reader-probe");
        std::filesystem::create_directories(root);
        Page(root / L"001.png", 800, 900, 0xF0CC88);
        Page(root / L"002.png", 800, 600, 0xA0D8E8);
        Page(root / L"003.png", 800, 20000, 0xC0DEA0);
        const auto a = Bytes(root / L"001.png"), b = Bytes(root / L"002.png");
        const std::string container = "<container xmlns='urn:oasis:names:tc:opendocument:xmlns:container'><rootfiles><rootfile full-path='OPS/book.opf'/></rootfiles></container>";
        const std::string opf = "<package xmlns='http://www.idpf.org/2007/opf'><manifest>"
            "<item id='one' href='Text/a.xhtml' media-type='application/xhtml+xml'/>"
            "<item id='two' href='Text/b.xhtml' media-type='application/xhtml+xml'/>"
            "<item id='skip' href='Text/missing.xhtml' media-type='application/xhtml+xml'/>"
            "</manifest><spine><itemref idref='two'/><itemref idref='skip' linear='no'/><itemref idref='one'/></spine></package>";
        Entries entries{{"mimetype", "application/epub+zip"}, {"META-INF/container.xml", container}, {"OPS/book.opf", opf},
            {"OPS/Text/a.xhtml", "<!DOCTYPE html PUBLIC '-//W3C//DTD XHTML 1.1//EN' 'http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd'><html xmlns='http://www.w3.org/1999/xhtml'><body><img src='../Images/one.png'/><img src='../Images/one.png'/></body></html>"},
            {"OPS/Text/b.xhtml", "<html xmlns='http://www.w3.org/1999/xhtml'><body><svg xmlns='http://www.w3.org/2000/svg' xmlns:xlink='http://www.w3.org/1999/xlink'><image xlink:href='../Images/two%20page.png#view'/></svg><img src='https://example.invalid/never-fetch.png'/></body></html>"},
            {"OPS/Images/one.png", a}, {"OPS/Images/two page.png", b}};
        const auto epub = root / L"閱讀測試.epub";
        WriteZip(epub, entries);
        std::atomic_bool cancel{false}; std::wstring error;
        auto book = EpubBook::Open(epub, cancel, error);
        if (!book) std::wcerr << error << L'\n';
        Require(book && book->pages.size() == 3, "EPUB image count/DOCTYPE");
        Require(book->labels[0] == L"OPS/Images/two page.png" && book->labels[1] == L"OPS/Images/one.png", "spine/SVG/percent URI order");
        Require(book->pages[1] == book->pages[2], "repeated image extraction reused");
        auto image = WicDecoder::Decode(book->pages[0].wstring(), error);
        Require(image && image->width == 800 && image->height == 600, "extracted PNG decode");
        Require(ImageExporter::SavePng(*image, root / L"epub-export.png", 400, error), "EPUB export");
        image = WicDecoder::Decode((root / L"epub-export.png").wstring(), error);
        Require(image && image->width == 400 && image->height == 300, "EPUB downsized dimensions");
        const auto extracted = book->pages[0].parent_path();
        book.reset(); Require(!std::filesystem::exists(extracted), "temporary cleanup");
        auto reject = [&](const char* name, Entries bad) {
            const auto path = root / name; WriteZip(path, bad); error.clear();
            auto result = EpubBook::Open(path, cancel, error);
            Require(!result && !error.empty(), name);
        };
        auto bad = entries; bad.emplace_back("../../outside.png", a); reject("traversal.epub", bad);
        bad = entries; bad.emplace_back("OPS/Images/one.png", a); reject("duplicate.epub", bad);
        bad = entries; bad.pop_back(); reject("missing.epub", bad);
        bad = entries; bad[2].second = "<package>"; reject("invalid-xml.epub", bad);
        bad = entries; bad.emplace_back("META-INF/encryption.xml", "<encryption><EncryptedData><CipherData><CipherReference URI='OPS/Images/one.png'/></CipherData></EncryptedData></encryption>"); reject("drm.epub", bad);
        // Font obfuscation is common in legitimate EPUBs and must not reject image reading.
        bad.back().second = "<encryption><EncryptedData><CipherData><CipherReference URI='OPS/font.ttf'/></CipherData></EncryptedData></encryption>";
        WriteZip(root / L"font-obfuscated.epub", bad);
        Require(EpubBook::Open(root / L"font-obfuscated.epub", cancel, error) != nullptr, "font obfuscation allowed");
        cancel.store(true); Require(!EpubBook::Open(epub, cancel, error), "cancel import"); cancel.store(false);
        FolderModel folder; Require(folder.Open(root / L"001.png"), "folder index");
        const auto original = folder.Current(); Require(!folder.Open(root / L"missing.jpg") && folder.Current() == original, "transactional open");
        ContinuousLayout layout; layout.Reset(3); layout.SetSize(0, 800, 900); layout.SetSize(1, 800, 600); layout.SetSize(2, 800, 20000); layout.Rebuild();
        Require(layout.Top(1) == 1.125 && layout.Top(2) == 1.875 && layout.Total() == 26.875, "gap-free layout");
        Require(layout.PageAt(1.125) == 1 && layout.PageAt(26) == 2 && layout.PageAt(-5) == 0, "page boundary lookup");
        const double position = 2.0, fraction = (position - layout.Top(2)) / layout.Height(2);
        layout.SetSize(0, 800, 1600); layout.Rebuild();
        Require(std::abs((layout.Top(2) + fraction * layout.Height(2)) - 2.875) < 1e-9, "reflow anchor");
        {
            ImageCache cache(4 * 1024 * 1024);
            cache.Pin({(root / L"001.png").wstring(), (root / L"002.png").wstring()});
            cache.Request((root / L"001.png").wstring(), 100);
            cache.Request((root / L"002.png").wstring(), 90);
            for (int i = 0; i < 40; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(25));
            Require(cache.CurrentBytes() <= 4 * 1024 * 1024, "hard cache budget with pinned pages");
            cache.Request((root / L"missing.png").wstring(), 100);
            for (int i = 0; i < 40 && cache.ErrorFor((root / L"missing.png").wstring()).empty(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            Require(!cache.ErrorFor((root / L"missing.png").wstring()).empty(), "cache decode failure recorded");
        }
        std::cout << "PASS: EPUB spine, SVG wrapper, URI escaping, repeat images, Unicode, PNG export, cleanup, traversal, duplicate, missing file, malformed XML, DRM, font obfuscation, cancellation, transactional open, continuous layout/anchor, cache budget.\n";
        CoUninitialize(); return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; CoUninitialize(); return 1;
    }
}
