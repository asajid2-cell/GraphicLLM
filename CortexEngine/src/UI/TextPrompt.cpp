#include "TextPrompt.h"
#include <commctrl.h>
#include <string>

namespace Cortex::UI {

namespace {
    constexpr int ID_EDIT = 1001;
    constexpr int ID_OK = 1002;
    constexpr int ID_CANCEL = 1003;

    struct PromptState {
        std::string result;
        HWND hwnd = nullptr;
        HWND edit = nullptr;
        bool done = false;
    };

    LRESULT CALLBACK PromptWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        auto* state = reinterpret_cast<PromptState*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

        switch (msg) {
        case WM_NCCREATE: {
            auto cs = reinterpret_cast<LPCREATESTRUCT>(lParam);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            break;
        }
        case WM_CREATE: {
            state = reinterpret_cast<PromptState*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
            HFONT hFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

            RECT client{};
            GetClientRect(hwnd, &client);
            int width = client.right - client.left;
            int height = client.bottom - client.top;

            int margin = 12;
            int labelHeight = 20;

            int contentWidth = width - 2 * margin;
            int y = margin;

            // Title line
            HWND lblTitle = CreateWindowExW(
                0, L"STATIC", L"Describe the change:",
                WS_CHILD | WS_VISIBLE,
                margin, y, contentWidth, labelHeight,
                hwnd, nullptr, nullptr, nullptr);
            SendMessage(lblTitle, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
            y += labelHeight + 4;

            // Helper text
            const wchar_t* helperText = L"Example: \"Add a shiny metal sphere next to the cube\"";
            HWND lblHelper = CreateWindowExW(
                0, L"STATIC", helperText,
                WS_CHILD | WS_VISIBLE,
                margin, y, contentWidth, labelHeight + 4,
                hwnd, nullptr, nullptr, nullptr);
            SendMessage(lblHelper, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
            y += labelHeight + 10;

            // Multi-line text box
            int buttonsHeight = 30;
            int editHeight = height - y - buttonsHeight - margin * 2;
            if (editHeight < 60) {
                editHeight = 60;
            }

            state->edit = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                L"EDIT",
                L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_AUTOVSCROLL | ES_MULTILINE | ES_WANTRETURN,
                margin, y,
                contentWidth, editHeight,
                hwnd,
                reinterpret_cast<HMENU>(ID_EDIT),
                nullptr,
                nullptr);
            SendMessage(state->edit, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

            // Buttons row
            int buttonWidth = 90;
            int buttonHeight = 26;
            int buttonGap = 12;
            int totalButtonsWidth = buttonWidth * 2 + buttonGap;
            int buttonY = height - margin - buttonHeight;
            int buttonX = (width - totalButtonsWidth) / 2;

            HWND btnOk = CreateWindowExW(
                0, L"BUTTON", L"OK",
                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                buttonX, buttonY, buttonWidth, buttonHeight,
                hwnd, reinterpret_cast<HMENU>(ID_OK), nullptr, nullptr);

            HWND btnCancel = CreateWindowExW(
                0, L"BUTTON", L"Cancel",
                WS_CHILD | WS_VISIBLE,
                buttonX + buttonWidth + buttonGap, buttonY, buttonWidth, buttonHeight,
                hwnd, reinterpret_cast<HMENU>(ID_CANCEL), nullptr, nullptr);

            SendMessage(btnOk, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
            SendMessage(btnCancel, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

            // Give keyboard focus to the edit control once it exists.
            SetFocus(state->edit);

            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == ID_OK || (id == ID_EDIT && HIWORD(wParam) == EN_MAXTEXT)) {
                int len = GetWindowTextLengthA(state->edit);
                if (len > 0) {
                    // Allocate space for the characters plus the null terminator,
                    // then assign only the characters actually read into result.
                    std::string buffer(static_cast<size_t>(len) + 1, '\0');
                    int written = GetWindowTextA(
                        state->edit,
                        buffer.data(),
                        len + 1
                    );
                    if (written < 0) {
                        written = 0;
                    }
                    state->result.assign(buffer.data(),
                                         static_cast<size_t>(written));
                } else {
                    state->result.clear();
                }
                state->done = true;
                DestroyWindow(hwnd);
                return 0;
            }
            if (id == ID_CANCEL) {
                state->result.clear();
                state->done = true;
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        }
        case WM_CLOSE:
            state->result.clear();
            state->done = true;
            DestroyWindow(hwnd);
            return 0;
        }

        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    void RegisterPromptClass() {
        static bool registered = false;
        if (registered) return;

        WNDCLASSW wc = {};
        wc.lpfnWndProc = PromptWndProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = L"CortexPromptWindow";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

        RegisterClassW(&wc);
        registered = true;
    }

    RECT CenterRect(HWND parent, int width, int height) {
        RECT rc = { 0, 0, width, height };
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);

        int x = (screenW - width) / 2;
        int y = (screenH - height) / 2;

        if (parent) {
            RECT pr{};
            GetWindowRect(parent, &pr);
            x = pr.left + ((pr.right - pr.left) - width) / 2;
            y = pr.top + ((pr.bottom - pr.top) - height) / 2;
        }
        OffsetRect(&rc, x, y);
        return rc;
    }
}

std::string TextPrompt::Show(HWND parent, const std::string& title, const std::string& /*prompt*/) {
    RegisterPromptClass();

    PromptState state{};
    RECT rc = CenterRect(parent, 560, 260);

    state.hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        L"CortexPromptWindow",
        std::wstring(title.begin(), title.end()).c_str(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        rc.left, rc.top,
        rc.right - rc.left, rc.bottom - rc.top,
        parent,
        nullptr,
        GetModuleHandle(nullptr),
        &state);

    if (!state.hwnd) {
        return {};
    }

    ShowWindow(state.hwnd, SW_SHOW);
    UpdateWindow(state.hwnd);

    MSG msg;
    while (!state.done && GetMessage(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
            SendMessage(state.hwnd, WM_COMMAND, ID_OK, 0);
        } else if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            SendMessage(state.hwnd, WM_COMMAND, ID_CANCEL, 0);
        } else {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return state.result;
}

} // namespace Cortex::UI
