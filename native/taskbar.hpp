#pragma once
#include "core.hpp"
namespace quota {
struct TaskbarSpace {
    HMONITOR monitor = nullptr;
    HWND host = nullptr;
    RECT bounds{};
    std::vector<RECT> occupied;
};
struct TaskbarUpdate {
    int generation = 0;
    std::vector<TaskbarSpace> spaces;
};
std::optional<int> freeTaskbarX(RECT lane, int width, int desired, const std::vector<RECT> &occupied, int gap);
std::vector<std::optional<int>> arrangeTaskbarWidgets(RECT lane, const std::vector<int> &widths,
    const std::vector<int> &desired, const std::vector<RECT> &occupied, int gap);
TaskbarSpace readTaskbarSpace(const Monitor &monitor, std::atomic<bool> &stop);
} // namespace quota
