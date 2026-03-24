// main.cpp
// Simple P2P bulletin board using UDP broadcast on LAN.
// Compile with MSVC; link Ws2_32.lib
// e.g. cl /EHsc /std:c++17 main.cpp ws2_32.lib

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <thread>
#include <atomic>
#include <fstream>
#include <sstream>
#include <vector>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <random>

#pragma comment(lib, "Ws2_32.lib")

const int PORT = 33333;
const wchar_t* WINDOW_CLASS_NAME = L"P2PBoardWnd";
const wchar_t* WINDOW_TITLE = L"P2P 掲示板 プロトタイプ (LAN broadcast)";

HWND hListBox, hEdit;
std::atomic<bool> running{ true };
SOCKET sock = INVALID_SOCKET;
std::mutex fileMutex;
std::string nodeId;

// utility: get current UTC timestamp ISO
std::string now_iso() {
    using namespace std::chrono;
    auto t = system_clock::now();
    std::time_t tt = system_clock::to_time_t(t);
    std::tm g;
    gmtime_s(&g, &tt);
    char buf[64];
    auto ms = duration_cast<milliseconds>(t.time_since_epoch()) % 1000;
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03lldZ",
        g.tm_year + 1900, g.tm_mon + 1, g.tm_mday,
        g.tm_hour, g.tm_min, g.tm_sec, (long long)ms.count());
    return std::string(buf);
}

// simple uuid-ish generator
std::string gen_node_id() {
    std::random_device rd;
    std::mt19937_64 eng(rd());
    std::uniform_int_distribution<unsigned long long> dist;
    unsigned long long a = dist(eng);
    unsigned long long b = dist(eng);
    char buf[64];
    sprintf_s(buf, "%016llx%016llx", (unsigned long long)a, (unsigned long long)b);
    return std::string(buf);
}

// append a line to local posts file (thread-safe)
void persist_post(const std::string& line) {
    std::lock_guard<std::mutex> lk(fileMutex);
    std::ofstream ofs("posts.txt", std::ios::app);
    if (ofs) {
        ofs << line << "\n";
    }
}

// helper to add to UI listbox from worker thread (via PostMessage)
void add_listbox_item_async(const std::string& text) {
    // convert to wide
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring wtext;
    wtext.resize(wlen);
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &wtext[0], wlen);
    // remove trailing null from resize
    if (!wtext.empty() && wtext.back() == L'\0') wtext.pop_back();
    // send custom message with pointer (allocated)
    std::wstring* p = new std::wstring(std::move(wtext));
    PostMessage(GetForegroundWindow(), WM_USER + 1, reinterpret_cast<WPARAM>(p), 0);
}

// format display: [timestamp] author: body
std::string format_display(const std::string& timestamp, const std::string& author, const std::string& body) {
    std::ostringstream ss;
    ss << "[" << timestamp << "] " << author << ": " << body;
    return ss.str();
}

// receive loop
void recv_loop() {
    char buf[65536];
    sockaddr_in from;
    int fromlen = sizeof(from);
    while (running) {
        int n = recvfrom(sock, buf, (int)sizeof(buf) - 1, 0, (sockaddr*)&from, &fromlen);
        if (n == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (!running) break;
            // Sleep a bit and continue
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        if (n <= 0) continue;
        buf[n] = 0;
        std::string msg(buf, n);
        // parse expected format: id|timestamp|nodeId|body
        size_t p1 = msg.find('|');
        size_t p2 = (p1 == std::string::npos) ? std::string::npos : msg.find('|', p1 + 1);
        size_t p3 = (p2 == std::string::npos) ? std::string::npos : msg.find('|', p2 + 1);
        if (p1 == std::string::npos || p2 == std::string::npos || p3 == std::string::npos) continue; // malformed
        std::string id = msg.substr(0, p1);
        std::string timestamp = msg.substr(p1 + 1, p2 - p1 - 1);
        std::string author = msg.substr(p2 + 1, p3 - p2 - 1);
        std::string body = msg.substr(p3 + 1);
        // ignore if this node sent it (optional)
        if (author == nodeId) continue;
        std::string display = format_display(timestamp, author.substr(0, 8), body);
        persist_post(msg);
        add_listbox_item_async(display);
    }
}

// load existing posts from file on startup
void load_posts_on_start(HWND hList) {
    std::lock_guard<std::mutex> lk(fileMutex);
    std::ifstream ifs("posts.txt");
    if (!ifs) return;
    std::string line;
    while (std::getline(ifs, line)) {
        // parse same as receive
        size_t p1 = line.find('|');
        size_t p2 = (p1 == std::string::npos) ? std::string::npos : line.find('|', p1 + 1);
        size_t p3 = (p2 == std::string::npos) ? std::string::npos : line.find('|', p2 + 1);
        if (p1 == std::string::npos || p2 == std::string::npos || p3 == std::string::npos) continue;
        std::string timestamp = line.substr(p1 + 1, p2 - p1 - 1);
        std::string author = line.substr(p2 + 1, p3 - p2 - 1);
        std::string body = line.substr(p3 + 1);
        std::string display = format_display(timestamp, author.substr(0, 8), body);
        int idx = (int)SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)std::wstring(std::wstring(display.begin(), display.end()).c_str()).c_str());
        // we used naive conversion above; UI will be basic
        SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)std::wstring(display.begin(), display.end()).c_str());
    }
}

// Win32 window procedure
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        CreateWindowW(L"STATIC", L"投稿:", WS_VISIBLE | WS_CHILD, 10, 10, 50, 20, hwnd, NULL, NULL, NULL);
        hEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 10, 30, 360, 24, hwnd, (HMENU)1, NULL, NULL);
        CreateWindowW(L"BUTTON", L"送信", WS_CHILD | WS_VISIBLE, 380, 30, 80, 24, hwnd, (HMENU)2, NULL, NULL);
        hListBox = CreateWindowW(L"LISTBOX", NULL, WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | WS_BORDER, 10, 70, 650, 380, hwnd, (HMENU)3, NULL, NULL);
        // load posts
        load_posts_on_start(hListBox);
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == 2) { // send button
            int len = (int)SendMessage(hEdit, WM_GETTEXTLENGTH, 0, 0);
            if (len == 0) break;
            std::wstring wbody;
            wbody.resize(len + 1);
            SendMessage(hEdit, WM_GETTEXT, (WPARAM)(len + 1), (LPARAM)&wbody[0]);
            // convert to utf8
            int needed = WideCharToMultiByte(CP_UTF8, 0, wbody.c_str(), -1, NULL, 0, NULL, NULL);
            std::string body;
            body.resize(needed);
            WideCharToMultiByte(CP_UTF8, 0, wbody.c_str(), -1, &body[0], needed, NULL, NULL);
            if (!body.empty() && body.back() == '\0') body.pop_back();
            // craft message
            std::string id = gen_node_id().substr(0, 16); // brief id for message
            std::string ts = now_iso();
            std::ostringstream os;
            os << id << "|" << ts << "|" << nodeId << "|" << body;
            std::string msg = os.str();
            // send via broadcast
            sockaddr_in to;
            to.sin_family = AF_INET;
            to.sin_port = htons(PORT);
            to.sin_addr.s_addr = inet_addr("255.255.255.255");
            int sent = sendto(sock, msg.data(), (int)msg.size(), 0, (sockaddr*)&to, sizeof(to));
            // persist locally and add to UI
            persist_post(msg);
            std::string display = format_display(ts, nodeId.substr(0, 8), body);
            // add to UI
            // convert to wide
            int wlen = MultiByteToWideChar(CP_UTF8, 0, display.c_str(), -1, nullptr, 0);
            std::wstring wdisplay;
            wdisplay.resize(wlen);
            MultiByteToWideChar(CP_UTF8, 0, display.c_str(), -1, &wdisplay[0], wlen);
            if (!wdisplay.empty() && wdisplay.back() == L'\0') wdisplay.pop_back();
            SendMessage(hListBox, LB_ADDSTRING, 0, (LPARAM)wdisplay.c_str());
            // clear edit
            SetWindowTextW(hEdit, L"");
        }
        break;
    case WM_SIZE: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        // reposition controls roughly
        MoveWindow(hEdit, 10, 30, rc.right - 220, 24, TRUE);
        MoveWindow(GetDlgItem(hwnd, 2), rc.right - 200, 30, 80, 24, TRUE);
        MoveWindow(hListBox, 10, 70, rc.right - 20, rc.bottom - 80, TRUE);
        break;
    }
    case WM_DESTROY:
        running = false;
        closesocket(sock);
        PostQuitMessage(0);
        break;
        // custom message to add listbox item (pointer passed)
    case WM_USER + 1: {
        std::wstring* p = reinterpret_cast<std::wstring*>(wParam);
        if (p) {
            SendMessage(hListBox, LB_ADDSTRING, 0, (LPARAM)p->c_str());
            delete p;
        }
        break;
    }
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    // load or create node id
    {
        std::ifstream ifs("nodeid.txt");
        if (ifs) {
            std::getline(ifs, nodeId);
            if (nodeId.empty()) nodeId = gen_node_id();
        }
        else {
            nodeId = gen_node_id();
            std::ofstream ofs("nodeid.txt");
            ofs << nodeId;
        }
    }

    // init winsock
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        MessageBoxA(NULL, "WSAStartup failed", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        MessageBoxA(NULL, "socket() failed", "Error", MB_OK | MB_ICONERROR);
        WSACleanup();
        return 1;
    }

    // enable broadcast
    BOOL yes = TRUE;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (char*)&yes, sizeof(yes));

    // bind to port to receive
    sockaddr_in local;
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = htons(PORT);
    if (bind(sock, (sockaddr*)&local, sizeof(local)) == SOCKET_ERROR) {
        MessageBoxA(NULL, "bind() failed", "Error", MB_OK | MB_ICONERROR);
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    // create receive thread
    std::thread recvTh(recv_loop);

    // register window class
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = WINDOW_CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(0, WINDOW_CLASS_NAME, WINDOW_TITLE,
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, hInstance, NULL);

    if (!hwnd) return 1;
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // basic message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // cleanup
    running = false;
    if (recvTh.joinable()) recvTh.join();
    if (sock != INVALID_SOCKET) closesocket(sock);
    WSACleanup();
    return 0;
}
