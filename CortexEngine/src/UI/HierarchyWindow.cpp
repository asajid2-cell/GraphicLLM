#include "HierarchyWindow.h"

#include "Core/ServiceLocator.h"
#include "Core/Engine.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <unordered_map>
#include <cstdint>

namespace Cortex::UI {

namespace {

struct HierarchyNode {
    entt::entity entity = entt::null;
    std::string name;
    std::vector<HierarchyNode> children;
};

struct HierarchyState {
    bool initialized = false;
    bool visible = false;
    HWND parent = nullptr;

    HWND hwnd = nullptr;
    HWND tree = nullptr;
    HFONT font = nullptr;

    // Map entity -> tree item for selection sync.
    std::unordered_map<entt::entity, HTREEITEM> entityToItem;
};

HierarchyState g_hierarchy;

const wchar_t* kHierarchyWindowClassName = L"CortexHierarchyWindow";

std::string WStringToUtf8(const std::wstring& s) {
    return std::string(s.begin(), s.end());
}

std::wstring Utf8ToWide(const std::string& s) {
    return std::wstring(s.begin(), s.end());
}

std::vector<HierarchyNode> BuildSceneHierarchy(Scene::ECS_Registry* ecs) {
    std::vector<HierarchyNode> roots;
    if (!ecs) {
        return roots;
    }

    auto& reg = ecs->GetRegistry();

    // First collect all entities that have a transform; use parent field for hierarchy.
    std::unordered_map<entt::entity, HierarchyNode> nodes;
    auto view = reg.view<Scene::TransformComponent>();
    for (auto entity : view) {
        HierarchyNode node;
        node.entity = entity;

        if (reg.all_of<Scene::TagComponent>(entity)) {
            const auto& tag = reg.get<Scene::TagComponent>(entity);
            node.name = tag.tag;
        } else {
            node.name = "Entity_" + std::to_string(static_cast<std::uint32_t>(entity));
        }

        nodes.emplace(entity, std::move(node));
    }

    // Assign children based on TransformComponent::parent.
    for (auto entity : view) {
        auto& tc = view.get<Scene::TransformComponent>(entity);
        if (tc.parent != entt::null && reg.valid(tc.parent) && nodes.count(tc.parent)) {
            auto& parentNode = nodes[tc.parent];
            parentNode.children.push_back(nodes[entity]);
        }
    }

    // Anything whose parent is invalid or null becomes a root.
    for (auto& [entity, node] : nodes) {
        const auto& tc = reg.get<Scene::TransformComponent>(entity);
        if (tc.parent == entt::null || !reg.valid(tc.parent) || !nodes.count(tc.parent)) {
            roots.push_back(node);
        }
    }

    // Simple sort by name for stable presentation.
    auto sortFn = [](const HierarchyNode& a, const HierarchyNode& b) {
        return a.name < b.name;
    };
    std::function<void(std::vector<HierarchyNode>&)> sortRecursive =
        [&](std::vector<HierarchyNode>& vec) {
            std::sort(vec.begin(), vec.end(), sortFn);
            for (auto& n : vec) {
                sortRecursive(n.children);
            }
        };
    sortRecursive(roots);

    return roots;
}

void RebuildTree() {
    if (!g_hierarchy.tree) {
        return;
    }

    // Clear all items.
    SendMessageW(g_hierarchy.tree, TVM_DELETEITEM, 0, reinterpret_cast<LPARAM>(TVI_ROOT));
    g_hierarchy.entityToItem.clear();

    auto* engine = Cortex::ServiceLocator::GetEngine();
    Scene::ECS_Registry* ecs = engine ? engine->GetRegistry() : nullptr;
    auto roots = BuildSceneHierarchy(ecs);

    std::function<void(const HierarchyNode&, HTREEITEM)> insertNode =
        [&](const HierarchyNode& node, HTREEITEM parentItem) {
            TVINSERTSTRUCTW tvis{};
            tvis.hParent = parentItem;
            tvis.hInsertAfter = TVI_LAST;

            std::wstring wname = Utf8ToWide(node.name);
            tvis.item.mask = TVIF_TEXT | TVIF_PARAM;
            tvis.item.pszText = const_cast<LPWSTR>(wname.c_str());
            tvis.item.lParam = static_cast<LPARAM>(static_cast<std::uint32_t>(node.entity));

            HTREEITEM hItem = reinterpret_cast<HTREEITEM>(
                SendMessageW(g_hierarchy.tree, TVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&tvis)));
            g_hierarchy.entityToItem[node.entity] = hItem;

            for (const auto& child : node.children) {
                insertNode(child, hItem);
            }
        };

    for (const auto& root : roots) {
        insertNode(root, TVI_ROOT);
    }

    (void)engine;
    (void)ecs;
}

void EnsureWindowCreated();

void RegisterHierarchyWindowClass() {
    static bool registered = false;
    if (registered) {
        return;
    }

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_TREEVIEW_CLASSES;
    InitCommonControlsEx(&icc);

    WNDCLASSW wc{};
    wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
        switch (msg) {
        case WM_CREATE: {
            g_hierarchy.font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

            RECT rc{};
            GetClientRect(hwnd, &rc);
            int width = rc.right - rc.left;
            int height = rc.bottom - rc.top;

            g_hierarchy.tree = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                WC_TREEVIEWW,
                L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS,
                0, 0,
                width, height,
                hwnd,
                nullptr,
                GetModuleHandleW(nullptr),
                nullptr);

            if (g_hierarchy.tree && g_hierarchy.font) {
                SendMessageW(g_hierarchy.tree, WM_SETFONT, reinterpret_cast<WPARAM>(g_hierarchy.font), TRUE);
            }

            RebuildTree();
            return 0;
        }
        case WM_SIZE: {
            if (g_hierarchy.tree) {
                RECT rc{};
                GetClientRect(hwnd, &rc);
                MoveWindow(g_hierarchy.tree, 0, 0, rc.right - rc.left, rc.bottom - rc.top, TRUE);
            }
            return 0;
        }
        case WM_NOTIFY: {
            LPNMHDR hdr = reinterpret_cast<LPNMHDR>(lParam);
            if (hdr && hdr->hwndFrom == g_hierarchy.tree && hdr->code == TVN_SELCHANGEDW) {
                auto* nmtv = reinterpret_cast<LPNMTREEVIEWW>(lParam);
                HTREEITEM item = nmtv->itemNew.hItem;
                if (item) {
                    TVITEMW tvi{};
                    tvi.mask = TVIF_PARAM;
                    tvi.hItem = item;
                    if (SendMessageW(g_hierarchy.tree, TVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&tvi))) {
                        entt::entity selected = static_cast<entt::entity>(
                            static_cast<std::uint32_t>(tvi.lParam));
                        auto* engine = Cortex::ServiceLocator::GetEngine();
                        if (engine && selected != entt::null) {
                            engine->SetSelectedEntity(selected);
                        }
                    }
                }
                return 0;
            }
            break;
        }
        case WM_CLOSE:
            HierarchyWindow::SetVisible(false);
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_DESTROY:
            g_hierarchy.hwnd = nullptr;
            g_hierarchy.tree = nullptr;
            g_hierarchy.visible = false;
            g_hierarchy.entityToItem.clear();
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    };

    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kHierarchyWindowClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    RegisterClassW(&wc);
    registered = true;
}

void EnsureWindowCreated() {
    if (!g_hierarchy.initialized || g_hierarchy.hwnd) {
        return;
    }

    RegisterHierarchyWindowClass();

    int width = 320;
    int height = 540;

    RECT rc{0, 0, width, height};
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenW - width) / 2;
    int y = (screenH - height) / 2;

    if (g_hierarchy.parent) {
        RECT pr{};
        GetWindowRect(g_hierarchy.parent, &pr);
        x = pr.left + ((pr.right - pr.left) - width) / 2;
        y = pr.top + ((pr.bottom - pr.top) - height) / 2;
    }

    g_hierarchy.hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kHierarchyWindowClassName,
        L"Cortex Scene Hierarchy",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        x, y,
        width, height,
        g_hierarchy.parent,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);

    if (g_hierarchy.hwnd) {
        ShowWindow(g_hierarchy.hwnd, SW_HIDE);
        UpdateWindow(g_hierarchy.hwnd);
    }
}

} // namespace

void HierarchyWindow::Initialize(HWND parent) {
    g_hierarchy.parent = parent;
    g_hierarchy.initialized = true;
}

void HierarchyWindow::Shutdown() {
    if (g_hierarchy.hwnd) {
        DestroyWindow(g_hierarchy.hwnd);
        g_hierarchy.hwnd = nullptr;
        g_hierarchy.tree = nullptr;
    }
    g_hierarchy = HierarchyState{};
}

void HierarchyWindow::SetVisible(bool visible) {
    if (!g_hierarchy.initialized) {
        return;
    }

    EnsureWindowCreated();
    if (!g_hierarchy.hwnd) {
        return;
    }

    if (visible) {
        RebuildTree();
        ShowWindow(g_hierarchy.hwnd, SW_SHOWNORMAL);
        SetForegroundWindow(g_hierarchy.hwnd);
        g_hierarchy.visible = true;
    } else {
        ShowWindow(g_hierarchy.hwnd, SW_HIDE);
        g_hierarchy.visible = false;
    }
}

void HierarchyWindow::Toggle() {
    if (!g_hierarchy.initialized) {
        return;
    }
    SetVisible(!g_hierarchy.visible);
}

bool HierarchyWindow::IsVisible() {
    return g_hierarchy.visible;
}

void HierarchyWindow::Refresh() {
    if (!g_hierarchy.hwnd || !g_hierarchy.tree) {
        return;
    }
    RebuildTree();
}

void HierarchyWindow::OnSelectionChanged() {
    if (!g_hierarchy.hwnd || !g_hierarchy.tree) {
        return;
    }

    auto* engine = Cortex::ServiceLocator::GetEngine();
    auto* ecs = engine ? engine->GetRegistry() : nullptr;
    if (!engine || !ecs) {
        return;
    }

    // We do not have direct access to the selected entity; rely on focus
    // target tag and map back to entity.
    std::string focus = engine->GetFocusTarget();
    if (focus.empty()) {
        return;
    }

    auto& reg = ecs->GetRegistry();
    auto view = reg.view<Scene::TagComponent>();
    entt::entity target = entt::null;
    for (auto entity : view) {
        const auto& tag = view.get<Scene::TagComponent>(entity);
        if (tag.tag == focus) {
            target = entity;
            break;
        }
    }

    if (target == entt::null) {
        return;
    }

    auto it = g_hierarchy.entityToItem.find(target);
    if (it != g_hierarchy.entityToItem.end()) {
        SendMessageW(g_hierarchy.tree,
                     TVM_SELECTITEM,
                     static_cast<WPARAM>(TVGN_CARET),
                     reinterpret_cast<LPARAM>(it->second));
    }
}

} // namespace Cortex::UI
