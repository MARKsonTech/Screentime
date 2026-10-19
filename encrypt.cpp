#include <windows.h>
#include <shlobj.h>
#include <string>

#define ID_EDIT_PASS  101
#define ID_BTN_ACTION 102

HWND hEditPass;
std::wstring g_targetDir;
bool g_isLocked = false;
std::wstring g_progDataDir;

std::wstring GetProgDataDir() {
    WCHAR progData[MAX_PATH];
    SHGetFolderPathW(NULL, CSIDL_COMMON_APPDATA, NULL, 0, progData);
    return std::wstring(progData) + L"\\ScreentimeLock";
}

bool CreateStartMenuShortcut(const std::wstring& exePath, const std::wstring& shortcutName) {
    WCHAR startMenuPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, startMenuPath))) {
        std::wstring lnkPath = std::wstring(startMenuPath) + L"\\" + shortcutName + L".lnk";
        
        IShellLinkW* pShellLink = NULL;
        if (SUCCEEDED(CoInitialize(NULL)) && 
            SUCCEEDED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&pShellLink))) {
            
            pShellLink->SetPath(exePath.c_str());
            pShellLink->SetWorkingDirectory(g_progDataDir.c_str());
            pShellLink->SetDescription(L"Unlock Screentime Folder");

            IPersistFile* pPersistFile = NULL;
            if (SUCCEEDED(pShellLink->QueryInterface(IID_IPersistFile, (void**)&pPersistFile))) {
                pPersistFile->Save(lnkPath.c_str(), TRUE);
                pPersistFile->Release();
            }
            pShellLink->Release();
            CoUninitialize();
            return true;
        }
    }
    return false;
}

void RemoveStartMenuShortcut(const std::wstring& shortcutName) {
    WCHAR startMenuPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, startMenuPath))) {
        std::wstring lnkPath = std::wstring(startMenuPath) + L"\\" + shortcutName + L".lnk";
        DeleteFileW(lnkPath.c_str());
    }
}

void ExecuteAction(HWND hwnd) {
    WCHAR passBuf[256];
    GetWindowTextW(hEditPass, passBuf, 256);
    std::wstring enteredPass(passBuf);

    std::wstring datPath = g_targetDir + L"\\encrypt.dat";

    if (!g_isLocked) {
        // --- MAP VERGRENDELEN ---
        // 1. Schrijf encrypt.dat in de doelmap
        SetFileAttributesW(datPath.c_str(), FILE_ATTRIBUTE_NORMAL);
        HANDLE hFile = CreateFileW(datPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            DWORD written;
            WriteFile(hFile, enteredPass.c_str(), (DWORD)(enteredPass.length() * sizeof(WCHAR)), &written, NULL);
            CloseHandle(hFile);
            SetFileAttributesW(datPath.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
        }

        // 2. Maak ProgramData map aan en kopieer exe erheen
        CreateDirectoryW(g_progDataDir.c_str(), NULL);
        
        WCHAR currentExe[MAX_PATH];
        GetModuleFileNameW(NULL, currentExe, MAX_PATH);
        std::wstring destExe = g_progDataDir + L"\\UnlockScreentime.exe";
        CopyFileW(currentExe, destExe.c_str(), FALSE);

        // 3. Sla het echte doelpad op in target.txt
        std::wstring targetTxtPath = g_progDataDir + L"\\target.txt";
        HANDLE hTargetFile = CreateFileW(targetTxtPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hTargetFile != INVALID_HANDLE_VALUE) {
            DWORD written;
            WriteFile(hTargetFile, g_targetDir.c_str(), (DWORD)(g_targetDir.length() * sizeof(WCHAR)), &written, NULL);
            CloseHandle(hTargetFile);
        }

        // 4. Maak Startmenu snelkoppeling
        CreateStartMenuShortcut(destExe, L"Unlock ScreentimeFolder");

        // 5. Zet map op slot via icacls
        WCHAR username[256];
        DWORD size = 256;
        GetUserNameW(username, &size);
        std::wstring cmd = L"cmd.exe /c icacls \"" + g_targetDir + L"\" /deny " + username + L":F";

        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = { 0 };
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        std::wstring mutableCmd = cmd;
        if (CreateProcessW(NULL, &mutableCmd[0], NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }

        MessageBoxW(hwnd, L"Map is vergrendeld en Startmenu snelkoppeling is aangemaakt!", L"Vergrendeld", MB_ICONINFORMATION);
        ExitProcess(0);

    } else {
        // --- MAP ONTGRENDELEN ---
        SetFileAttributesW(datPath.c_str(), FILE_ATTRIBUTE_NORMAL);
        HANDLE hFile = CreateFileW(datPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        
        std::wstring storedPass = L"";
        if (hFile != INVALID_HANDLE_VALUE) {
            WCHAR buffer[256] = { 0 };
            DWORD read;
            ReadFile(hFile, buffer, sizeof(buffer) - sizeof(WCHAR), &read, NULL);
            CloseHandle(hFile);
            storedPass = std::wstring(buffer, read / sizeof(WCHAR));
        }

        if (!storedPass.empty() && storedPass != enteredPass) {
            SetFileAttributesW(datPath.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
            MessageBoxW(hwnd, L"Onjuist wachtwoord!", L"Toegang Geweigerd", MB_ICONERROR);
            return;
        }

        // Verwijder icacls deny regel
        WCHAR username[256];
        DWORD size = 256;
        GetUserNameW(username, &size);
        std::wstring cmd = L"cmd.exe /c icacls \"" + g_targetDir + L"\" /remove:d " + username;

        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = { 0 };
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        std::wstring mutableCmd = cmd;
        if (CreateProcessW(NULL, &mutableCmd[0], NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }

        // Cleanup
        DeleteFileW(datPath.c_str());
        RemoveStartMenuShortcut(L"Unlock ScreentimeFolder");
        
        std::wstring targetTxtPath = g_progDataDir + L"\\target.txt";
        DeleteFileW(targetTxtPath.c_str());

        MessageBoxW(hwnd, L"Map succesvol ontgrendeld!", L"Ontgrendeld", MB_ICONINFORMATION);
        ExitProcess(0);
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        WCHAR titleBuf[512];
        if (g_isLocked) {
            lstrcpyW(titleBuf, L"Voer wachtwoord in om te ontgrendelen");
        } else {
            lstrcpyW(titleBuf, L"Stel wachtwoord in & Vergrendel map");
        }
        
        CreateWindowW(L"STATIC", titleBuf, WS_CHILD | WS_VISIBLE | SS_LEFT, 20, 15, 360, 20, hwnd, NULL, NULL, NULL);
        CreateWindowW(L"STATIC", L"Wachtwoord:", WS_CHILD | WS_VISIBLE | SS_LEFT, 20, 50, 100, 20, hwnd, NULL, NULL, NULL);
        
        hEditPass = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_PASSWORD, 20, 75, 340, 26, hwnd, (HMENU)ID_EDIT_PASS, NULL, NULL);
        
        std::wstring btnText = g_isLocked ? L"Ontgrendel Map" : L"Vergrendel Map";
        CreateWindowW(L"BUTTON", btnText.c_str(), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 20, 115, 340, 36, hwnd, (HMENU)ID_BTN_ACTION, NULL, NULL);
        
        break;
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == ID_BTN_ACTION) {
            ExecuteAction(hwnd);
        }
        break;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    g_progDataDir = GetProgDataDir();

    // Bepaal waar het programma vandaan wordt gestart
    WCHAR pathBuf[MAX_PATH];
    GetModuleFileNameW(NULL, pathBuf, MAX_PATH);
    std::wstring fullPath(pathBuf);
    std::wstring currentDir = fullPath.substr(0, fullPath.find_last_of(L"\\/"));

    if (currentDir == g_progDataDir) {
        // Gestart vanuit ProgramData (via Startmenu snelkoppeling) -> Lees het echte doelpad uit target.txt
        std::wstring targetTxtPath = g_progDataDir + L"\\target.txt";
        HANDLE hFile = CreateFileW(targetTxtPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            WCHAR buffer[MAX_PATH] = { 0 };
            DWORD read;
            ReadFile(hFile, buffer, sizeof(buffer) - sizeof(WCHAR), &read, NULL);
            CloseHandle(hFile);
            g_targetDir = std::wstring(buffer, read / sizeof(WCHAR));
        }
        g_isLocked = true; // Aangezien het via de snelkoppeling uit ProgramData draait, is de map vergrendeld
    } else {
        // Gestart vanuit de lokale map zelf (eerste keer instellen)
        g_targetDir = currentDir;
        std::wstring datPath = g_targetDir + L"\\encrypt.dat";
        DWORD attr = GetFileAttributesW(datPath.c_str());
        g_isLocked = (attr != INVALID_FILE_ATTRIBUTES);
    }

    const wchar_t CLASS_NAME[] = L"ScreentimeLockClass";
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = CLASS_NAME;

    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0, CLASS_NAME, L"ScreentimeFolder Beveiliging",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 210,
        NULL, NULL, hInstance, NULL
    );

    if (hwnd == NULL) return 0;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg = { 0 };
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}