#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <winhttp.h>
#include <shellapi.h>
#include <string>
#include <fstream>
#include <sstream>
#include <ctime>
#include <thread>
#include <chrono>
#include <algorithm>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

const std::string SUPABASE_URL = "ddxscnalvhuresqpdows.supabase.co";
const std::string SUPABASE_ANON_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImRkeHNjbmFsdvhuresqpdowsIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NjA1MzY5MjksImV4cCI6MjA3NjExMjkyOX0.1ixT38jQN36PV2-R68DmBAx8fRZzeY9Nw5kzsc3fZzY";
const std::string CONFIG_FILE = "config.json";
const std::string CACHE_FILE = "screentime_cache.json";

struct UserConfig {
    std::string userId;
    std::string email;
    int dailyLimitMinutes = 120;
    std::string blockedStart = "17:00";
    std::string blockedEnd = "06:00";
    int usedMinutesToday = 0;
    std::string lastDate = "";
};

UserConfig g_config;
bool g_isLoggedIn = false;

LRESULT CALLBACK LoginWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
void ShowLoginDialog();

std::string GenerateUserId() {
    GUID guid;
    CoCreateGuid(&guid);
    char szGuid[64];
    sprintf_s(szGuid, "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
        guid.Data1, guid.Data2, guid.Data3,
        guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
        guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    std::string s(szGuid);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
    return s;
}

std::string GetCurrentDateStr() {
    time_t now = time(nullptr);
    struct tm ttime;
    localtime_s(&ttime, &now);
    char buf[32];
    sprintf_s(buf, "%04d-%02d-%02d", ttime.tm_year + 1900, ttime.tm_mon + 1, ttime.tm_mday);
    return std::string(buf);
}

void SaveLocalCache() {
    std::ofstream file(CACHE_FILE);
    if (file.is_open()) {
        file << "{\n";
        file << "  \"user_id\": \"" << g_config.userId << "\",\n";
        file << "  \"used_minutes_today\": " << g_config.usedMinutesToday << ",\n";
        file << "  \"daily_limit_minutes\": " << g_config.dailyLimitMinutes << ",\n";
        file << "  \"blocked_start_time\": \"" << g_config.blockedStart << "\",\n";
        file << "  \"blocked_end_time\": \"" << g_config.blockedEnd << "\",\n";
        file << "  \"last_active_date\": \"" << g_config.lastDate << "\"\n";
        file << "}\n";
        file.close();
    }
}

void LoadLocalCache() {
    std::ifstream file(CACHE_FILE);
    if (!file.is_open()) return;
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();

    auto extractVal = [&](const std::string& key, std::string& dest) {
        size_t pos = content.find("\"" + key + "\"");
        if (pos == std::string::npos) return;
        size_t colon = content.find(":", pos);
        size_t startQuote = content.find("\"", colon);
        size_t endQuote = content.find("\"", startQuote + 1);
        if (startQuote != std::string::npos && endQuote != std::string::npos) {
            dest = content.substr(startQuote + 1, endQuote - startQuote - 1);
        }
    };

    auto extractInt = [&](const std::string& key, int& dest) {
        size_t pos = content.find("\"" + key + "\"");
        if (pos == std::string::npos) return;
        size_t colon = content.find(":", pos);
        if (colon != std::string::npos) {
            dest = std::stoi(content.substr(colon + 1));
        }
    };

    extractVal("user_id", g_config.userId);
    extractVal("last_active_date", g_config.lastDate);
    extractVal("blocked_start_time", g_config.blockedStart);
    extractVal("blocked_end_time", g_config.blockedEnd);
    extractInt("used_minutes_today", g_config.usedMinutesToday);
    extractInt("daily_limit_minutes", g_config.dailyLimitMinutes);
}

void SaveConfig() {
    std::ofstream file(CONFIG_FILE);
    if (file.is_open()) {
        file << "{\n  \"user_id\": \"" << g_config.userId << "\",\n";
        file << "  \"email\": \"" << g_config.email << "\"\n}\n";
        file.close();
    }
}

bool LoadConfig() {
    std::ifstream file(CONFIG_FILE);
    if (!file.is_open()) return false;
    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();

    size_t pos = buffer.str().find("\"user_id\"");
    if (pos == std::string::npos) return false;
    size_t startQuote = buffer.str().find("\"", buffer.str().find(":", pos));
    size_t endQuote = buffer.str().find("\"", startQuote + 1);
    if (startQuote != std::string::npos && endQuote != std::string::npos) {
        g_config.userId = buffer.str().substr(startQuote + 1, endQuote - startQuote - 1);
        g_isLoggedIn = !g_config.userId.empty();
        return g_isLoggedIn;
    }
    return false;
}

std::string HttpPostOrPatch(const std::string& method, const std::string& endpoint, const std::string& jsonData) {
    std::string result = "";
    HINTERNET hSession = WinHttpOpen(L"MarksonScreenTime/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_BYPASS, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";

    std::wstring wsServer(SUPABASE_URL.begin(), SUPABASE_URL.end());
    HINTERNET hConnect = WinHttpConnect(hSession, wsServer.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return ""; }

    std::wstring wsEndpoint(endpoint.begin(), endpoint.end());
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, std::wstring(method.begin(), method.end()).c_str(), wsEndpoint.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return ""; }

    std::string headers = "apikey: " + SUPABASE_ANON_KEY + "\r\nAuthorization: Bearer " + SUPABASE_ANON_KEY + "\r\nContent-Type: application/json\r\nPrefer: return=representation";
    std::wstring wsHeaders(headers.begin(), headers.end());

    BOOL bResults = WinHttpSendRequest(hRequest, wsHeaders.c_str(), (DWORD)-1L, (LPVOID)jsonData.c_str(), (DWORD)jsonData.length(), (DWORD)jsonData.length(), 0);
    if (bResults) bResults = WinHttpReceiveResponse(hRequest, NULL);

    if (bResults) {
        DWORD dwSize = 0;
        WinHttpQueryDataAvailable(hRequest, &dwSize);
        while (dwSize > 0) {
            char* pszOutBuffer = new char[dwSize + 1];
            ZeroMemory(pszOutBuffer, dwSize + 1);
            DWORD dwDownloaded = 0;
            if (WinHttpReadData(hRequest, (LPVOID)pszOutBuffer, dwSize, &dwDownloaded)) {
                result += std::string(pszOutBuffer, dwDownloaded);
            }
            delete[] pszOutBuffer;
            WinHttpQueryDataAvailable(hRequest, &dwSize);
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
}

bool SyncWithSupabase() {
    std::string endpoint = "/rest/v1/screentime?user_id=eq." + g_config.userId;
    std::string response = HttpPostOrPatch("GET", endpoint, "");

    if (response.empty() || response == "[]") {
        std::string payload = "{\"user_id\":\"" + g_config.userId + "\",\"email\":\"" + g_config.email + "\",\"used_minutes_today\":0,\"daily_limit_minutes\":120,\"blocked_start_time\":\"17:00\",\"blocked_end_time\":\"06:00\",\"last_active_date\":\"" + GetCurrentDateStr() + "\"}";
        std::string createResp = HttpPostOrPatch("POST", "/rest/v1/screentime", payload);
        return !createResp.empty();
    } else {
        auto extractIntField = [&](const std::string& field) {
            size_t pos = response.find("\"" + field + "\":");
            if (pos != std::string::npos) {
                return std::stoi(response.substr(pos + field.length() + 3));
            }
            return -1;
        };

        auto extractStrField = [&](const std::string& field, std::string& dest) {
            size_t pos = response.find("\"" + field + "\":");
            if (pos != std::string::npos) {
                size_t startQ = response.find("\"", pos + field.length() + 2);
                size_t endQ = response.find("\"", startQ + 1);
                if (startQ != std::string::npos && endQ != std::string::npos) {
                    dest = response.substr(startQ + 1, endQ - startQ - 1);
                }
            }
        };

        int limit = extractIntField("daily_limit_minutes");
        if (limit > 0) g_config.dailyLimitMinutes = limit;
        extractStrField("blocked_start_time", g_config.blockedStart);
        extractStrField("blocked_end_time", g_config.blockedEnd);
    }
    return true;
}

void PushUpdateToSupabase() {
    std::string endpoint = "/rest/v1/screentime?user_id=eq." + g_config.userId;
    std::string payload = "{\"used_minutes_today\":" + std::to_string(g_config.usedMinutesToday) + ",\"last_active_date\":\"" + GetCurrentDateStr() + "\"}";
    HttpPostOrPatch("PATCH", endpoint, payload);
}

bool IsInRestrictedTime() {
    time_t now = time(nullptr);
    struct tm ttime;
    localtime_s(&ttime, &now);
    int current_minutes = ttime.tm_hour * 60 + ttime.tm_min;

    int start_h = std::stoi(g_config.blockedStart.substr(0, 2));
    int start_m = std::stoi(g_config.blockedStart.substr(3, 2));
    int start_minutes = start_h * 60 + start_m;

    int end_h = std::stoi(g_config.blockedEnd.substr(0, 2));
    int end_m = std::stoi(g_config.blockedEnd.substr(3, 2));
    int end_minutes = end_h * 60 + end_m;

    if (start_minutes < end_minutes) {
        return current_minutes >= start_minutes && current_minutes < end_minutes;
    } else {
        return current_minutes >= start_minutes || current_minutes < end_minutes;
    }
}

HWND hEmailEdit;

LRESULT CALLBACK LoginWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        CreateWindowW(L"STATIC", L"Welcome to Markson ScreenTime", WS_VISIBLE | WS_CHILD | SS_CENTER, 20, 20, 340, 25, hwnd, NULL, NULL, NULL);
        CreateWindowW(L"STATIC", L"Enter your email or create a profile:", WS_VISIBLE | WS_CHILD, 20, 60, 340, 20, hwnd, NULL, NULL, NULL);
        hEmailEdit = CreateWindowW(L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 20, 85, 340, 25, hwnd, (HMENU)1, NULL, NULL);
        CreateWindowW(L"BUTTON", L"Sign In / Register", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 20, 130, 160, 30, hwnd, (HMENU)2, NULL, NULL);
        CreateWindowW(L"BUTTON", L"Create Account Online", WS_VISIBLE | WS_CHILD, 200, 130, 160, 30, hwnd, (HMENU)3, NULL, NULL);
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == 2) {
            char emailBuf[256];
            GetWindowTextA(hEmailEdit, emailBuf, 256);
            g_config.email = std::string(emailBuf);
            if (g_config.email.empty()) {
                MessageBoxW(hwnd, L"Please enter an email or username.", L"Error", MB_OK | MB_ICONERROR);
                break;
            }
            g_config.userId = GenerateUserId();
            SaveConfig();
            SyncWithSupabase();
            g_isLoggedIn = true;
            DestroyWindow(hwnd);
        } else if (LOWORD(wp) == 3) {
            ShellExecuteA(NULL, "open", "https://markson.netlify.app/register", NULL, NULL, SW_SHOWNORMAL);
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, msg, wp, lp);
    }
    return 0;
}

void ShowLoginDialog() {
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = LoginWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"MarksonLoginClass";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowW(L"MarksonLoginClass", L"Markson ScreenTime - Setup", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 220, NULL, NULL, wc.hInstance, NULL);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    CoInitialize(NULL);
    HANDLE hMutex = CreateMutexA(NULL, TRUE, "MarksonScreenTimeMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CoUninitialize();
        return 0;
    }

    LoadLocalCache();
    if (!LoadConfig()) {
        ShowLoginDialog();
    }

    if (!g_isLoggedIn) {
        CoUninitialize();
        return 0;
    }

    bool lockSpawnedFlag = false;
    auto lastMinuteCheck = std::chrono::steady_clock::now();

    while (true) {
        std::string currentDate = GetCurrentDateStr();
        if (g_config.lastDate != currentDate) {
            g_config.lastDate = currentDate;
            g_config.usedMinutesToday = 0;
        }

        // Constantly poll Supabase every 10 seconds to fetch any changes made by parents
        SyncWithSupabase();

        // Increment actual used time every 60 seconds of real-world elapsed time
        auto nowClock = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(nowClock - lastMinuteCheck).count() >= 60) {
            g_config.usedMinutesToday++;
            lastMinuteCheck = nowClock;
            SaveLocalCache();
            PushUpdateToSupabase();
        }

        bool timeRestricted = IsInRestrictedTime();
        bool limitExceeded = (g_config.usedMinutesToday >= g_config.dailyLimitMinutes);

        if ((timeRestricted || limitExceeded) && !lockSpawnedFlag) {
            ShellExecuteA(NULL, "open", "LockOverlay.exe", NULL, NULL, SW_SHOW);
            lockSpawnedFlag = true;
        } else if (!timeRestricted && !limitExceeded && lockSpawnedFlag) {
            lockSpawnedFlag = false;
        }

        std::this_thread::sleep_for(std::chrono::seconds(10));
    }

    ReleaseMutex(hMutex);
    CloseHandle(hMutex);
    CoUninitialize();
    return 0;
}