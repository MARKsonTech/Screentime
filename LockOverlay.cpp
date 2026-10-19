#include <windows.h>
#include <bcrypt.h>
#include <string>
#include <vector>
#include <fstream>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "bcrypt.lib")

#define ID_SHUTDOWN_BTN 101
#define ID_PASS_EDIT    102
#define ID_UNLOCK_BTN   103

// Standaard binaire SHA-256 hash voor fallback wachtwoord "1234" (32 bytes)
const std::vector<BYTE> g_fallbackHash = {
    0x03, 0xac, 0x67, 0x42, 0x16, 0xf3, 0xe1, 0x5c,
    0x76, 0x1e, 0xe1, 0xa5, 0xe2, 0x55, 0xf0, 0x67,
    0x95, 0x36, 0x23, 0xc8, 0xb3, 0x88, 0xb4, 0x45,
    0x9e, 0x13, 0xf9, 0x78, 0xd7, 0xc8, 0x46, 0xf4
};

std::vector<BYTE> g_targetHash = g_fallbackHash;
std::wstring g_message = L"Screen locked.";
HWND g_hwnd = NULL;
HWND g_hEditPass = NULL;

HHOOK hKeyboardHook = NULL;
DWORD g_origEdgeSwipeVal = 1;

void RestartWindowsExplorer() {
    wchar_t killCmd[] = L"taskkill.exe /f /im explorer.exe";
    STARTUPINFO siKill = { sizeof(siKill) };
    PROCESS_INFORMATION piKill;
    if (CreateProcessW(NULL, killCmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &siKill, &piKill)) {
        WaitForSingleObject(piKill.hProcess, 1500); 
        CloseHandle(piKill.hProcess);
        CloseHandle(piKill.hThread);
    }

    wchar_t startCmd[] = L"explorer.exe";
    STARTUPINFO siStart = { sizeof(siStart) };
    PROCESS_INFORMATION piStart;
    if (CreateProcessW(NULL, startCmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &siStart, &piStart)) {
        CloseHandle(piStart.hProcess);
        CloseHandle(piStart.hThread);
    }
}

void SetRegistryEdgeSwipe(DWORD value) {
    HKEY hKey;
    const wchar_t* path = L"SOFTWARE\\Policies\\Microsoft\\Windows\\EdgeUI";
    
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"AllowEdgeSwipe", 0, REG_DWORD, (const BYTE*)&value, sizeof(value));
        RegCloseKey(hKey);
    }
    if (RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"AllowEdgeSwipe", 0, REG_DWORD, (const BYTE*)&value, sizeof(value));
        RegCloseKey(hKey);
    }
}

void BackupRegistrySettings() {
    HKEY hKey;
    const wchar_t* path = L"SOFTWARE\\Policies\\Microsoft\\Windows\\EdgeUI";
    DWORD dwType = REG_DWORD;
    DWORD dwSize = sizeof(g_origEdgeSwipeVal);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExW(hKey, L"AllowEdgeSwipe", NULL, &dwType, (LPBYTE)&g_origEdgeSwipeVal, &dwSize) != ERROR_SUCCESS) {
            g_origEdgeSwipeVal = 1; 
        }
        RegCloseKey(hKey);
    }
}

std::vector<BYTE> HashSHA256Binary(const std::wstring& input) {
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    std::vector<BYTE> hashBuffer(32, 0);

    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0) {
        if (BCryptCreateHash(hAlg, &hHash, NULL, 0, NULL, 0, 0) == 0) {
            ULONG cbData = (ULONG)(input.length() * sizeof(wchar_t));
            BCryptHashData(hHash, (PUCHAR)input.c_str(), cbData, 0);
            BCryptFinishHash(hHash, hashBuffer.data(), (ULONG)hashBuffer.size(), 0);
            BCryptDestroyHash(hHash);
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }
    return hashBuffer;
}

void LoadConfigDAT() {
    g_targetHash = g_fallbackHash; // Reset naar fallback "1234"

    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring exeStr(exePath);
    std::wstring::size_type pos = exeStr.find_last_of(L"\\/");
    std::wstring configPath = exeStr.substr(0, pos) + L"\\config.dat";

    std::ifstream file(configPath.c_str(), std::ios::binary | std::ios::ate);
    if (!file.is_open()) return;

    std::streamsize size = file.tellg();
    if (size >= 32) { // Een SHA-256 hash is minstens 32 bytes
        file.seekg(0, std::ios::beg);
        std::vector<BYTE> buffer(32);
        if (file.read(reinterpret_cast<char*>(buffer.data()), 32)) {
            g_targetHash = buffer;
        }
    }
    file.close();
}

void VerifyPasswordAndUnlock() {
    if (!g_hEditPass) return;

    wchar_t buffer[256] = { 0 };
    GetWindowTextW(g_hEditPass, buffer, 256);

    LoadConfigDAT(); // Lees dynamic eventuele updates van config.dat

    std::vector<BYTE> enteredHash = HashSHA256Binary(buffer);

    if (enteredHash == g_targetHash) {
        DestroyWindow(g_hwnd);
    } else {
        SetWindowTextW(g_hEditPass, L"");
        MessageBoxW(g_hwnd, L"Incorrect password.", L"Error", MB_OK | MB_ICONERROR);
    }
}

void TriggerShutdown() {
    wchar_t cmd[] = L"shutdown.exe /s /t 0";
    STARTUPINFO si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess); 
        CloseHandle(pi.hThread);
    }
}

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wp, LPARAM lp) {
    if (nCode == HC_ACTION && wp == WM_KEYDOWN) {
        KBDLLHOOKSTRUCT* pKey = (KBDLLHOOKSTRUCT*)lp;
        
        if (pKey->vkCode == VK_LWIN || pKey->vkCode == VK_RWIN) return 1; 
        if (pKey->vkCode == VK_F4 && (pKey->flags & LLKHF_ALTDOWN)) return 1; 
        if (pKey->vkCode == VK_TAB && (pKey->flags & LLKHF_ALTDOWN)) return 1;
        if (pKey->vkCode == VK_ESCAPE && (pKey->flags & LLKHF_ALTDOWN)) return 1;
    }
    return CallNextHookEx(hKeyboardHook, nCode, wp, lp);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        g_hEditPass = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_PASSWORD | ES_AUTOHSCROLL,
            100, 240, 250, 35, hwnd, (HMENU)ID_PASS_EDIT,
            (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL
        );

        CreateWindowExW(0, L"BUTTON", L"Unlock", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
            360, 240, 90, 35, hwnd, (HMENU)ID_UNLOCK_BTN, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL);

        CreateWindowExW(0, L"BUTTON", L"Shutdown PC", WS_TABSTOP | WS_VISIBLE | WS_CHILD,
            100, 380, 160, 40, hwnd, (HMENU)ID_SHUTDOWN_BTN, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL);

        SetFocus(g_hEditPass);
        return 0;
    }

    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
        if (wmId == ID_SHUTDOWN_BTN) {
            TriggerShutdown();
        } else if (wmId == ID_UNLOCK_BTN) {
            VerifyPasswordAndUnlock();
        }
        return 0;
    }

    case WM_WINDOWPOSCHANGING: {
        WINDOWPOS* wp = (WINDOWPOS*)lParam;
        wp->flags |= SWP_NOSIZE | SWP_NOMOVE;
        wp->hwndInsertAfter = HWND_TOPMOST; 
        break;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        HBRUSH bgBrush = CreateSolidBrush(RGB(0, 120, 215));
        FillRect(hdc, &ps.rcPaint, bgBrush);
        DeleteObject(bgBrush);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));

        HFONT hTitleFont = CreateFont(36, 0, 0, 0, FW_LIGHT, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT hBodyFont = CreateFont(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        HFONT hOldFont = (HFONT)SelectObject(hdc, hTitleFont);
        RECT titleRect = { 100, 80, 1000, 140 };
        DrawText(hdc, L"Access Restricted", -1, &titleRect, DT_LEFT | DT_TOP);

        SelectObject(hdc, hBodyFont);
        RECT bodyRect = { 100, 150, 1000, 220 };
        DrawText(hdc, g_message.c_str(), -1, &bodyRect, DT_LEFT | DT_TOP | DT_WORDBREAK);

        SelectObject(hdc, hOldFont);
        DeleteObject(hTitleFont); 
        DeleteObject(hBodyFont);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        SetRegistryEdgeSwipe(g_origEdgeSwipeVal);
        RestartWindowsExplorer();

        if (hKeyboardHook != NULL) UnhookWindowsHookEx(hKeyboardHook);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    UNREFERENCED_PARAMETER(hPrevInstance); 
    UNREFERENCED_PARAMETER(lpCmdLine); 
    UNREFERENCED_PARAMETER(nCmdShow);

    LoadConfigDAT();
    BackupRegistrySettings();
    
    SetRegistryEdgeSwipe(0);
    RestartWindowsExplorer();

    const wchar_t CLASS_NAME[] = L"LockOverlayClass";
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    g_hwnd = CreateWindowEx(
        WS_EX_TOPMOST, 
        CLASS_NAME, L"Screen Lock", WS_POPUP | WS_VISIBLE,
        0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), NULL, NULL, hInstance, NULL
    );

    if (g_hwnd == NULL) {
        SetRegistryEdgeSwipe(g_origEdgeSwipeVal);
        RestartWindowsExplorer();
        return 0;
    }

    SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, hInstance, 0);
    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    MSG msg = { 0 };
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}