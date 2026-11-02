#include "taskbar.hpp"
#include <initguid.h>
#include <uiautomation.h>
namespace quota {
std::optional<int> freeTaskbarX(RECT lane, int width, int desired, const std::vector<RECT> &occupied, int gap) {
    if (width <= 0 || lane.bottom <= lane.top || lane.right - lane.left < width) return {};
    std::vector<std::pair<int, int>> blocked;
    for (auto &r : occupied)
        if (r.bottom > lane.top && r.top < lane.bottom && r.right > r.left)
            blocked.emplace_back(std::max<int>(lane.left, r.left - gap), std::min<int>(lane.right, r.right + gap));
    std::sort(blocked.begin(), blocked.end());
    int left = lane.left;
    std::optional<int> best;
    auto consider = [&](int right) {
        if (right - left < width) return;
        int candidate = std::clamp(desired, left, right - width);
        if (!best || std::abs(int64_t(candidate) - desired) < std::abs(int64_t(*best) - desired)) best = candidate;
    };
    for (auto &r : blocked) {
        if (r.second <= left || r.first >= lane.right) continue;
        consider(r.first);
        left = std::max(left, r.second);
    }
    consider(lane.right);
    return best;
}
template <class T> struct ComRef {
    T *p = nullptr;
    ~ComRef() { if (p) p->Release(); }
    T *operator->() const { return p; }
};
TaskbarSpace readTaskbarSpace(const Monitor &monitor, std::atomic<bool> &stop) {
    TaskbarSpace space;
    space.monitor = monitor.handle;
    space.host = taskbarWindow(monitor.handle);
    if (stop || !space.host || !GetWindowRect(space.host, &space.bounds)) return space;
    // Classic taskbars expose HWNDs even if their accessibility provider fails.
    EnumChildWindows(space.host, [](HWND child, LPARAM data) -> BOOL {
        auto &s = *reinterpret_cast<TaskbarSpace *>(data);
        wchar_t name[128]{};
        GetClassNameW(child, name, 128);
        if (IsWindowVisible(child) && (std::wstring(name) == L"TrayNotifyWnd" ||
                                      std::wstring(name) == L"Start" || std::wstring(name) == L"Button")) {
            RECT r{}, clipped{};
            if (GetWindowRect(child, &r) && IntersectRect(&clipped, &r, &s.bounds)) s.occupied.push_back(clipped);
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&space));
    ComRef<IUIAutomation2> automation;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IUIAutomation2, reinterpret_cast<void **>(&automation.p)))) return space;
    automation->put_ConnectionTimeout(1000);
    automation->put_TransactionTimeout(1500);
    ComRef<IUIAutomationElement> root;
    ComRef<IUIAutomationCondition> condition;
    ComRef<IUIAutomationCacheRequest> cache;
    ComRef<IUIAutomationElementArray> elements;
    VARIANT control{};
    control.vt = VT_BOOL; control.boolVal = VARIANT_TRUE;
    if (FAILED(automation->ElementFromHandle(space.host, &root.p)) ||
        FAILED(automation->CreatePropertyCondition(UIA_IsControlElementPropertyId, control, &condition.p)) ||
        FAILED(automation->CreateCacheRequest(&cache.p))) return space;
    for (auto id : {UIA_ControlTypePropertyId, UIA_BoundingRectanglePropertyId, UIA_IsOffscreenPropertyId,
                    UIA_ProcessIdPropertyId, UIA_IsKeyboardFocusablePropertyId})
        if (FAILED(cache->AddProperty(id))) return space;
    if (stop || FAILED(root->FindAllBuildCache(TreeScope_Descendants, condition.p, cache.p, &elements.p)) ||
        !elements.p) return space;
    int count = 0;
    elements->get_Length(&count);
    for (int i = 0; i < count && !stop; ++i) {
        ComRef<IUIAutomationElement> item;
        if (FAILED(elements->GetElement(i, &item.p))) continue;
        int type = 0, pid = 0;
        BOOL offscreen = TRUE, focusable = FALSE;
        RECT r{}, clipped{};
        if (FAILED(item->get_CachedProcessId(&pid)) || pid == int(GetCurrentProcessId()) ||
            FAILED(item->get_CachedIsOffscreen(&offscreen)) || offscreen ||
            FAILED(item->get_CachedControlType(&type)) ||
            FAILED(item->get_CachedBoundingRectangle(&r))) continue;
        item->get_CachedIsKeyboardFocusable(&focusable);
        bool button = type == UIA_ButtonControlTypeId || type == UIA_CheckBoxControlTypeId ||
                      type == UIA_RadioButtonControlTypeId || type == UIA_ListItemControlTypeId ||
                      type == UIA_MenuItemControlTypeId || type == UIA_SplitButtonControlTypeId;
        // Frames may cover the entire bar, but are not occupied icon regions.
        if ((button || focusable) && IntersectRect(&clipped, &r, &space.bounds) &&
            clipped.right - clipped.left < space.bounds.right - space.bounds.left)
            space.occupied.push_back(clipped);
    }
    return space;
}
} // namespace quota
