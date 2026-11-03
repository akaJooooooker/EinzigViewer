#include "ImageClipboard.h"

#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

using Microsoft::WRL::ComPtr;

namespace {
constexpr std::uint64_t maximumRawBytes = 256ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t maximumEncodedBytes = 272ULL * 1024ULL * 1024ULL;

// WIC writes through this bounded stream, so an encoder cannot grow an HGLOBAL
// without limit before we get a chance to inspect its final size.
class BoundedMemoryStream final : public IStream {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (id != IID_IUnknown && id != IID_ISequentialStream && id != IID_IStream) return E_NOINTERFACE;
        *object = static_cast<IStream*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&references_)); }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto references = static_cast<ULONG>(InterlockedDecrement(&references_));
        if (references == 0) delete this;
        return references;
    }
    HRESULT STDMETHODCALLTYPE Read(void* destination, ULONG count, ULONG* read) override {
        if (read) *read = 0;
        if (!destination && count != 0) return STG_E_INVALIDPOINTER;
        const auto available = position_ < bytes_.size() ? bytes_.size() - position_ : 0;
        const auto length = std::min<std::size_t>(available, count);
        if (length != 0) std::memcpy(destination, bytes_.data() + position_, length);
        position_ += length;
        if (read) *read = static_cast<ULONG>(length);
        return length == count ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Write(const void* source, ULONG count, ULONG* written) override {
        if (written) *written = 0;
        if (!source && count != 0) return STG_E_INVALIDPOINTER;
        if (count > maximumEncodedBytes - position_) return STG_E_MEDIUMFULL;
        if (count == 0) return S_OK;
        const auto end = position_ + count;
        try {
            if (end > bytes_.capacity()) {
                const auto grown = std::max(end, bytes_.capacity() + bytes_.capacity() / 2U);
                bytes_.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(grown, maximumEncodedBytes)));
            }
            if (end > bytes_.size()) bytes_.resize(end);
        } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
        catch (const std::length_error&) { return STG_E_MEDIUMFULL; }
        std::memcpy(bytes_.data() + position_, source, count);
        position_ = end;
        if (written) *written = count;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER offset, DWORD origin, ULARGE_INTEGER* position) override {
        std::int64_t base = 0;
        if (origin == STREAM_SEEK_CUR) base = static_cast<std::int64_t>(position_);
        else if (origin == STREAM_SEEK_END) base = static_cast<std::int64_t>(bytes_.size());
        else if (origin != STREAM_SEEK_SET) return STG_E_INVALIDFUNCTION;
        if (offset.QuadPart < -base || offset.QuadPart > static_cast<std::int64_t>(maximumEncodedBytes) - base) {
            return STG_E_INVALIDFUNCTION;
        }
        position_ = static_cast<std::size_t>(base + offset.QuadPart);
        if (position) position->QuadPart = position_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER size) override {
        if (size.QuadPart > maximumEncodedBytes) return STG_E_MEDIUMFULL;
        try {
            const auto length = static_cast<std::size_t>(size.QuadPart);
            if (length > bytes_.capacity()) bytes_.reserve(length);
            bytes_.resize(length);
        }
        catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
        catch (const std::length_error&) { return STG_E_MEDIUMFULL; }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CopyTo(IStream* destination, ULARGE_INTEGER count,
        ULARGE_INTEGER* read, ULARGE_INTEGER* written) override {
        if (read) read->QuadPart = 0;
        if (written) written->QuadPart = 0;
        if (!destination) return STG_E_INVALIDPOINTER;
        const auto available = position_ < bytes_.size() ? bytes_.size() - position_ : 0;
        const auto length = static_cast<ULONG>(std::min<std::uint64_t>(available, count.QuadPart));
        if (length == 0) return count.QuadPart == 0 ? S_OK : S_FALSE;
        ULONG copied = 0;
        const HRESULT result = destination->Write(bytes_.data() + position_, length, &copied);
        position_ += copied;
        if (read) read->QuadPart = copied;
        if (written) written->QuadPart = copied;
        if (FAILED(result)) return result;
        return copied == count.QuadPart ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Revert() override { return STG_E_INVALIDFUNCTION; }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
    HRESULT STDMETHODCALLTYPE Stat(STATSTG* information, DWORD) override {
        if (!information) return STG_E_INVALIDPOINTER;
        *information = {};
        information->type = STGTY_STREAM;
        information->cbSize.QuadPart = bytes_.size();
        information->grfMode = STGM_READWRITE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Clone(IStream** stream) override {
        if (!stream) return E_POINTER;
        *stream = nullptr;
        return E_NOTIMPL;
    }
    std::vector<std::byte> TakeBytes() { return std::move(bytes_); }

private:
    LONG references_ = 1;
    std::size_t position_ = 0;
    std::vector<std::byte> bytes_;
};

class ComApartment {
public:
    ComApartment() : result(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ComApartment() { if (SUCCEEDED(result)) CoUninitialize(); }
    const HRESULT result;
};

std::wstring SystemError(const DWORD code) {
    wchar_t* buffer = nullptr;
    const DWORD count = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring message = count != 0 && buffer ? std::wstring(buffer, count) : std::to_wstring(code);
    if (buffer) LocalFree(buffer);
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n')) message.pop_back();
    return message;
}

HRESULT EncodePng(const ImageData& image, std::vector<std::byte>& png) {
    ComApartment apartment;
    if (FAILED(apartment.result) && apartment.result != RPC_E_CHANGED_MODE) return apartment.result;
    ComPtr<IWICImagingFactory> factory;
    HRESULT result = CoCreateInstance(
        CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(result)) result = CoCreateInstance(
        CLSID_WICImagingFactory1, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    ComPtr<IWICBitmap> bitmap;
    if (SUCCEEDED(result)) result = factory->CreateBitmapFromMemory(
        image.width, image.height, GUID_WICPixelFormat32bppPBGRA, image.stride,
        static_cast<UINT>(image.pixels.size()),
        const_cast<BYTE*>(reinterpret_cast<const BYTE*>(image.pixels.data())), &bitmap);
    ComPtr<IWICFormatConverter> converter;
    if (SUCCEEDED(result)) result = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(result)) result = converter->Initialize(
        bitmap.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
        nullptr, 0.0, WICBitmapPaletteTypeCustom);
    ComPtr<BoundedMemoryStream> stream;
    if (SUCCEEDED(result)) {
        stream.Attach(new (std::nothrow) BoundedMemoryStream());
        if (!stream) result = E_OUTOFMEMORY;
    }
    ComPtr<IWICBitmapEncoder> encoder;
    if (SUCCEEDED(result)) result = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(result)) result = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> options;
    if (SUCCEEDED(result)) result = encoder->CreateNewFrame(&frame, &options);
    if (SUCCEEDED(result)) result = frame->Initialize(options.Get());
    if (SUCCEEDED(result)) result = frame->SetSize(image.width, image.height);
    if (SUCCEEDED(result)) result = frame->SetResolution(96.0, 96.0);
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(result)) result = frame->SetPixelFormat(&format);
    if (SUCCEEDED(result) && !IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA)) {
        result = WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    }
    if (SUCCEEDED(result)) result = frame->WriteSource(converter.Get(), nullptr);
    if (SUCCEEDED(result)) result = frame->Commit();
    if (SUCCEEDED(result)) result = encoder->Commit();
    if (SUCCEEDED(result)) png = stream->TakeBytes();
    return result;
}

class GlobalBuffer {
public:
    ~GlobalBuffer() { if (handle_) GlobalFree(handle_); }
    bool Initialize(const std::vector<std::byte>& bytes) {
        handle_ = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
        if (!handle_) return false;
        void* destination = GlobalLock(handle_);
        if (!destination) return false;
        std::memcpy(destination, bytes.data(), bytes.size());
        GlobalUnlock(handle_);
        return true;
    }
    HGLOBAL Get() const { return handle_; }
    void Release() { handle_ = nullptr; }

private:
    HGLOBAL handle_ = nullptr;
};

class ClipboardLock {
public:
    explicit ClipboardLock(HWND owner) : open(OpenClipboard(owner) != FALSE) {}
    ~ClipboardLock() { if (open) CloseClipboard(); }
    const bool open;
};
}

bool image_clipboard::Encode(const ImageData& image, EncodedImage& encoded, std::wstring& error) {
    encoded = {};
    error.clear();
    const std::uint64_t rowBytes = static_cast<std::uint64_t>(image.width) * 4ULL;
    const std::uint64_t rawBytes = static_cast<std::uint64_t>(image.stride) * image.height;
    if (image.width == 0 || image.height == 0 || image.width > LONG_MAX || image.height > LONG_MAX ||
        image.stride < rowBytes || rawBytes > maximumRawBytes || image.pixels.size() > maximumRawBytes ||
        rawBytes > image.pixels.size()) {
        error = L"圖片資料無效或超過 256 MiB，無法複製。 / Invalid image data or image exceeds the 256 MiB copy limit.";
        return false;
    }
    const std::uint64_t dibStride = (static_cast<std::uint64_t>(image.width) * 3ULL + 3ULL) & ~3ULL;
    const std::uint64_t dibPixels = dibStride * image.height;
    if (dibPixels + sizeof(BITMAPINFOHEADER) > maximumEncodedBytes) {
        error = L"圖片太大，無法複製。 / The image is too large to copy.";
        return false;
    }
    try {
        EncodedImage prepared;
        const HRESULT result = EncodePng(image, prepared.png);
        if (FAILED(result) || prepared.png.empty()) {
            error = L"無法編碼剪貼簿 PNG。 / Could not encode clipboard PNG: " + SystemError(static_cast<DWORD>(result));
            return false;
        }
        prepared.dib.resize(static_cast<std::size_t>(sizeof(BITMAPINFOHEADER) + dibPixels));
        BITMAPINFOHEADER header{};
        header.biSize = sizeof(header);
        header.biWidth = static_cast<LONG>(image.width);
        header.biHeight = static_cast<LONG>(image.height);
        header.biPlanes = 1;
        header.biBitCount = 24;
        header.biCompression = BI_RGB;
        header.biSizeImage = static_cast<DWORD>(dibPixels);
        std::memcpy(prepared.dib.data(), &header, sizeof(header));
        for (std::uint32_t y = 0; y < image.height; ++y) {
            const auto* source = image.pixels.data() + static_cast<std::size_t>(y) * image.stride;
            auto* destination = prepared.dib.data() + sizeof(header) +
                static_cast<std::size_t>(image.height - 1U - y) * static_cast<std::size_t>(dibStride);
            for (std::uint32_t x = 0; x < image.width; ++x) {
                const auto* pixel = source + static_cast<std::size_t>(x) * 4U;
                const auto alpha = std::to_integer<unsigned int>(pixel[3]);
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    destination[static_cast<std::size_t>(x) * 3U + channel] = static_cast<std::byte>(
                        std::min(255U, std::to_integer<unsigned int>(pixel[channel]) + 255U - alpha));
                }
            }
        }
        encoded = std::move(prepared);
        return true;
    } catch (const std::bad_alloc&) {
        error = L"記憶體不足，無法複製圖片。 / Not enough memory to copy the image.";
    } catch (const std::length_error&) {
        error = L"圖片太大，無法複製。 / The image is too large to copy.";
    }
    return false;
}

bool image_clipboard::Copy(HWND owner, const ImageData& image, std::wstring& error) {
    error.clear();
    if (!owner || !IsWindow(owner)) {
        error = L"無法取得複製圖片的視窗。 / The image copy window is unavailable.";
        return false;
    }
    EncodedImage encoded;
    if (!Encode(image, encoded, error)) return false;
    const UINT pngFormat = RegisterClipboardFormatW(L"PNG");
    if (pngFormat == 0) {
        error = L"無法註冊 PNG 剪貼簿格式。 / Could not register the PNG clipboard format.";
        return false;
    }
    GlobalBuffer png;
    GlobalBuffer dib;
    if (!png.Initialize(encoded.png) || !dib.Initialize(encoded.dib)) {
        error = L"記憶體不足，無法準備剪貼簿圖片。 / Not enough memory to prepare the clipboard image.";
        return false;
    }
    // All encoding and allocation finishes before touching the existing clipboard.
    ClipboardLock clipboard(owner);
    if (!clipboard.open) {
        error = L"剪貼簿正由其他程式使用，請稍後再試。 / The clipboard is busy; please try again.";
        return false;
    }
    if (!EmptyClipboard()) {
        error = L"無法更新剪貼簿，請稍後再試。 / Could not update the clipboard; please try again.";
        return false;
    }
    const bool pngCopied = SetClipboardData(pngFormat, png.Get()) != nullptr;
    if (pngCopied) png.Release();
    const bool dibCopied = SetClipboardData(CF_DIB, dib.Get()) != nullptr;
    if (dibCopied) dib.Release();
    if (!pngCopied || !dibCopied) {
        error = pngCopied || dibCopied
            ? L"僅複製了部分圖片格式，請重試。 / Only some image formats were copied; please try again."
            : L"無法將圖片放入剪貼簿，請重試。 / Could not place the image on the clipboard; please try again.";
        return false;
    }
    return true;
}
