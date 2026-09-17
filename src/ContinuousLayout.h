#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

// Page-width units keep layout independent of DPI and window size.
class ContinuousLayout {
public:
    void Reset(std::size_t count) { ratios_.assign(count, 1.41421356); tops_.assign(count + 1, 0); dirty_ = true; }
    bool SetSize(std::size_t page, unsigned width, unsigned height) {
        if (page >= ratios_.size() || !width || !height) return false;
        const double ratio = static_cast<double>(height) / width;
        if (std::abs(ratios_[page] - ratio) < 1e-10) return false;
        ratios_[page] = ratio; dirty_ = true; return true;
    }
    void Rebuild() {
        if (!dirty_) return;
        for (std::size_t i = 0; i < ratios_.size(); ++i) tops_[i + 1] = tops_[i] + ratios_[i];
        dirty_ = false;
    }
    double Top(std::size_t page) const { return tops_.at(page); }
    double Height(std::size_t page) const { return ratios_.at(page); }
    double Total() const { return tops_.empty() ? 0 : tops_.back(); }
    std::size_t PageAt(double position) const {
        if (ratios_.empty()) return 0;
        auto it = std::upper_bound(tops_.begin(), tops_.end(), std::max(0.0, position));
        return std::min(ratios_.size() - 1, static_cast<std::size_t>(it - tops_.begin() - 1));
    }
private:
    std::vector<double> ratios_;
    std::vector<double> tops_;
    bool dirty_ = false;
};
