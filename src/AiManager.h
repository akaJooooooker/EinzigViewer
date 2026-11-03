#pragma once

#include "AiRuntime.h"
#include "AiResources.h"

#include <Windows.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ai {

using Executor = std::function<Output(
    const RuntimeOptions&, const ImageData&, int, const std::filesystem::path&,
    const std::atomic_bool&, std::wstring&)>;

struct Request {
    std::wstring key;
    std::shared_ptr<ImageData> source;
};

struct Page {
    std::wstring key;
    std::filesystem::path path;
    int rotation = 0;
    std::shared_ptr<void> lease;
    std::function<bool(const std::wstring&, std::wstring&)> prepare;
    std::weak_ptr<ImageData> decoded;
};

struct Progress {
    std::size_t total = 0, complete = 0, failed = 0;
    bool processing = false, paused = false;
    std::wstring reason;
    ResourceLimits limits;
};

class Manager {
public:
    static constexpr UINT ReadyMessage = WM_APP + 27;

    Manager(HWND notificationWindow, RuntimeOptions options,
        std::filesystem::path workRoot, Executor executor = {},
        ResourceProbe resources = {}, BatchExecutor batchExecutor = {});
    ~Manager();

    Manager(const Manager&) = delete;
    Manager& operator=(const Manager&) = delete;

    void SetRequests(std::vector<Request> desired, int scale);
    void SetDocument(std::vector<Page> pages);
    void SetPriority(std::vector<Request> visible, int scale);
    void SetUserLimits(UserLimits);
    [[nodiscard]] Progress Status() const;
    void Cancel();
    void Clear();

    [[nodiscard]] std::shared_ptr<ImageData> TryGet(const std::wstring& key, int scale) const;
    [[nodiscard]] std::wstring ErrorFor(const std::wstring& key, int scale) const;
    [[nodiscard]] bool Pending() const;

private:
    struct State;
    std::shared_ptr<State> state_;
};

}
