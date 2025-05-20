
#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fstream>
#include <sstream>
#include <shellapi.h>
#include <iostream>
#include <winuser.h>

#define DEFAULT_GRID_SIZE      3
#define DEFAULT_WIN_WIDTH      320
#define DEFAULT_WIN_HEIGHT     240

// ———————————————————————————————————————————————————————————————————————————————
// Структуры и глобальные переменные

struct Config {
    int gridSize;
    int clientWidth;
    int clientHeight;
    COLORREF backgroundColor;
    COLORREF gridColor;
};

// Конфиг
Config currentConfig = {
    DEFAULT_GRID_SIZE,
    DEFAULT_WIN_WIDTH,
    DEFAULT_WIN_HEIGHT,
    RGB(0, 0, 255),
    RGB(255, 0, 0)
};

// Имена файлов
const TCHAR szWinClass[] = _T("Win32SampleApp");
const TCHAR szWinName[] = _T("Win32SampleWindow");
const TCHAR* configFileName = _T("config.txt");
const TCHAR* dataFileName = _T("data.bin");

// Механизмы чтения/записи конфига
enum ConfigMethod { METHOD_MAPPING, METHOD_FILEVARS, METHOD_FSTREAM, METHOD_WINAPI };
ConfigMethod configMethod = METHOD_MAPPING;

// Для хелперов GDI и WinAPI
HINSTANCE hInst;
HWND      hwnd;
HBRUSH    hBackgroundBrush = NULL;

// Для классического поля крестиков-ноликов
int* gridCells = nullptr;
void      InitializeGrid() {
    if (gridCells) free(gridCells);
    gridCells = (int*)calloc(currentConfig.gridSize * currentConfig.gridSize, sizeof(int));
}
void      CleanupGrid() {
    if (gridCells) { free(gridCells); gridCells = nullptr; }
}

// ———————————————————————————————————————————————————————————————————————————————
// IPC через именованную память

UINT WM_IPC_UPDATE;

// Named shared memory for grid state:
const TCHAR* SHARED_MEM_NAME = _T("Local\\GridSharedMemory");

struct SharedData {
    int      gridSize;
    COLORREF backgroundColor;
    COLORREF gridColor;
    int      cells[100];
};

SharedData* pShared = nullptr;
HANDLE      hMapFile = nullptr;

// ———————————————————————————————————————————————————————————————————————————————
// Прототипы
LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
void    UpdateBackgroundBrush(HWND hWnd, COLORREF c) {
    if (hBackgroundBrush)
        DeleteObject(hBackgroundBrush);
    hBackgroundBrush = CreateSolidBrush(c);
    SetClassLongPtr(hWnd, GCLP_HBRBACKGROUND, (LONG_PTR)hBackgroundBrush);
}
COLORREF HSVtoRGB(float H, float S, float V);
void    ChangeGridLineColor(int delta);
void    LaunchNotepad();
void    ParseCommandLine(LPCTSTR);
void    LoadConfig(), SaveConfig();
void    LoadConfigMapping(), SaveConfigMapping();
void    LoadConfigFileVars(), SaveConfigFileVars();
void    LoadConfigFStream(), SaveConfigFStream();
void    LoadConfigWinAPI(), SaveConfigWinAPI();
void    BroadcastUpdate();
void    UpdateFromShared(HWND hWnd);
void    PlaceWindowNonOverlapping(HWND hNew);

// ———————————————————————————————————————————————————————————————————————————————
// main
int WINAPI _tWinMain(HINSTANCE hInstance, HINSTANCE, LPTSTR lpCmdLine, int nCmdShow) {
    hInst = hInstance;

    // Параметры + конфиг
    ParseCommandLine(lpCmdLine);
    LoadConfig();

    // Поле
    InitializeGrid();

    // Shared Memory
    hMapFile = CreateFileMapping(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
        0, sizeof(SharedData), SHARED_MEM_NAME
    );
    if (!hMapFile) {
        MessageBox(NULL, _T("Cannot create/open shared memory"), _T("Error"), MB_ICONERROR);
        return 1;
    }
    pShared = (SharedData*)MapViewOfFile(
        hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedData)
    );
    if (!pShared) {
        MessageBox(NULL, _T("Cannot map shared memory"), _T("Error"), MB_ICONERROR);
        CloseHandle(hMapFile);
        return 1;
    }

    bool firstInstance = (GetLastError() != ERROR_ALREADY_EXISTS);
    if (firstInstance) {
        currentConfig.gridSize = DEFAULT_GRID_SIZE;
        pShared->backgroundColor = RGB(0, 0, 255);
        pShared->gridColor = RGB(255, 0, 0);
        memset(pShared->cells, 0, sizeof(pShared->cells));
    }

    // Window class
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInst;
    wc.lpszClassName = _T("IPCGridClass");
    wc.hbrBackground = CreateSolidBrush(pShared->backgroundColor);
    WM_IPC_UPDATE = RegisterWindowMessage(_T("IPCGRID_UPDATE"));
    RegisterClass(&wc);

    // Create window
    hwnd = CreateWindow(
        _T("IPCGridClass"), _T("IPC Grid"), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        DEFAULT_WIN_WIDTH, DEFAULT_WIN_HEIGHT,
        nullptr, nullptr, hInst, nullptr
    );
    if (!hwnd) return 0;

    // Размещаем без перекрытия
    PlaceWindowNonOverlapping(hwnd);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // Message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Clean up
    CleanupGrid();
    UnmapViewOfFile(pShared);
    CloseHandle(hMapFile);
    UnregisterClass(_T("IPCGridClass"), hInst);
    return (int)msg.wParam;
}


// ————————————————————————————— Работа с конфигом (4 способа) ——————————————————————————————————

void LoadConfig() {
    switch (configMethod) {
    case METHOD_MAPPING:   LoadConfigMapping(); break;
    case METHOD_FILEVARS:  LoadConfigFileVars(); break;
    case METHOD_FSTREAM:   LoadConfigFStream(); break;
    case METHOD_WINAPI:    LoadConfigWinAPI(); break;
    }
}
void SaveConfig() {
    switch (configMethod) {
    case METHOD_MAPPING:   SaveConfigMapping(); break;
    case METHOD_FILEVARS:  SaveConfigFileVars(); break;
    case METHOD_FSTREAM:   SaveConfigFStream(); break;
    case METHOD_WINAPI:    SaveConfigWinAPI(); break;
    }
}

// — Memory Mapping
void LoadConfigMapping() {
    HANDLE f = CreateFile(configFileName, GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER sz; GetFileSizeEx(f, &sz);
    HANDLE m = CreateFileMapping(f, nullptr, PAGE_READONLY, sz.HighPart, sz.LowPart, nullptr);
    LPVOID p = MapViewOfFile(m, FILE_MAP_READ, 0, 0, sz.LowPart);
    if (!p) { CloseHandle(m); CloseHandle(f); return; }
    char* buf = new char[sz.LowPart + 1];
    memcpy(buf, p, sz.LowPart); buf[sz.LowPart] = 0;
    int r1, g1, b1, r2, g2, b2;
    if (sscanf_s(buf,
        "GridSize=%d\nWindowWidth=%d\nWindowHeight=%d\n"
        "BackgroundColor=%d;%d;%d\nGridColor=%d;%d;%d",
        &currentConfig.gridSize,
        &currentConfig.clientWidth,
        &currentConfig.clientHeight,
        &r1, &g1, &b1,
        &r2, &g2, &b2) == 9)
    {
        currentConfig.backgroundColor = RGB(r1, g1, b1);
        currentConfig.gridColor = RGB(r2, g2, b2);
    }
    delete[] buf;
    UnmapViewOfFile(p);
    CloseHandle(m);
    CloseHandle(f);
}
void SaveConfigMapping() {
    char buf[512];
    int len = snprintf(buf, 512,
        "GridSize=%d\nWindowWidth=%d\nWindowHeight=%d\n"
        "BackgroundColor=%d;%d;%d\nGridColor=%d;%d;%d\n",
        currentConfig.gridSize,
        currentConfig.clientWidth,
        currentConfig.clientHeight,
        GetRValue(currentConfig.backgroundColor),
        GetGValue(currentConfig.backgroundColor),
        GetBValue(currentConfig.backgroundColor),
        GetRValue(currentConfig.gridColor),
        GetGValue(currentConfig.gridColor),
        GetBValue(currentConfig.gridColor)
    );
    HANDLE f = CreateFile(configFileName,
        GENERIC_READ | GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    // расширяем файл и мапим для записи
    LARGE_INTEGER size; size.QuadPart = len;
    SetFilePointerEx(f, size, nullptr, FILE_BEGIN);
    SetEndOfFile(f);
    HANDLE m = CreateFileMapping(f, nullptr, PAGE_READWRITE, 0, len, nullptr);
    LPVOID p = MapViewOfFile(m, FILE_MAP_WRITE, 0, 0, len);
    memcpy(p, buf, len);
    FlushViewOfFile(p, len);
    UnmapViewOfFile(p);
    CloseHandle(m);
    CloseHandle(f);
}

// — FILEVARS
void LoadConfigFileVars() {
    FILE* fp = fopen("config.txt", "r");
    if (!fp) return;
    fseek(fp, 0, SEEK_END);
    long s = ftell(fp); rewind(fp);
    char* buf = new char[s + 1];
    fread(buf, 1, s, fp); buf[s] = 0; fclose(fp);
    char* line = strtok(buf, "\n");
    while (line) {
        if (!strncmp(line, "GridSize=", 9))
            currentConfig.gridSize = atoi(line + 9);
        else if (!strncmp(line, "WindowWidth=", 12))
            currentConfig.clientWidth = atoi(line + 12);
        else if (!strncmp(line, "WindowHeight=", 13))
            currentConfig.clientHeight = atoi(line + 13);
        else if (!strncmp(line, "BackgroundColor=", 16)) {
            int r, g, b; sscanf_s(line + 16, "%d;%d;%d", &r, &g, &b);
            currentConfig.backgroundColor = RGB(r, g, b);
        }
        else if (!strncmp(line, "GridColor=", 10)) {
            int r, g, b; sscanf_s(line + 10, "%d;%d;%d", &r, &g, &b);
            currentConfig.gridColor = RGB(r, g, b);
        }
        line = strtok(nullptr, "\n");
    }
    delete[] buf;
}
void SaveConfigFileVars() {
    FILE* fp = fopen("config.txt", "w");
    if (!fp) return;
    fprintf(fp,
        "GridSize=%d\nWindowWidth=%d\nWindowHeight=%d\n"
        "BackgroundColor=%d;%d;%d\nGridColor=%d;%d;%d\n",
        currentConfig.gridSize,
        currentConfig.clientWidth,
        currentConfig.clientHeight,
        GetRValue(currentConfig.backgroundColor),
        GetGValue(currentConfig.backgroundColor),
        GetBValue(currentConfig.backgroundColor),
        GetRValue(currentConfig.gridColor),
        GetGValue(currentConfig.gridColor),
        GetBValue(currentConfig.gridColor)
    );
    fclose(fp);
}

// — FSTREAM
void LoadConfigFStream() {
    std::ifstream f("config.txt");
    if (!f) return;
    std::string line; int r, g, b;
    while (std::getline(f, line)) {
        if (sscanf(line.c_str(), "GridSize=%d", &currentConfig.gridSize) == 1) continue;
        if (sscanf(line.c_str(), "WindowWidth=%d", &currentConfig.clientWidth) == 1) continue;
        if (sscanf(line.c_str(), "WindowHeight=%d", &currentConfig.clientHeight) == 1) continue;
        if (sscanf(line.c_str(), "BackgroundColor=%d;%d;%d", &r, &g, &b) == 3)
            currentConfig.backgroundColor = RGB(r, g, b);
        if (sscanf(line.c_str(), "GridColor=%d;%d;%d", &r, &g, &b) == 3)
            currentConfig.gridColor = RGB(r, g, b);
    }
}
void SaveConfigFStream() {
    std::ofstream f("config.txt");
    f << "GridSize=" << currentConfig.gridSize << "\n"
        << "WindowWidth=" << currentConfig.clientWidth << "\n"
        << "WindowHeight=" << currentConfig.clientHeight << "\n"
        << "BackgroundColor="
        << GetRValue(currentConfig.backgroundColor) << ";"
        << GetGValue(currentConfig.backgroundColor) << ";"
        << GetBValue(currentConfig.backgroundColor) << "\n"
        << "GridColor="
        << GetRValue(currentConfig.gridColor) << ";"
        << GetGValue(currentConfig.gridColor) << ";"
        << GetBValue(currentConfig.gridColor) << "\n";
}

// — WinAPI
void LoadConfigWinAPI() {
    HANDLE f = CreateFile(configFileName, GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER sz; GetFileSizeEx(f, &sz);
    char* buf = new char[sz.LowPart + 1];
    DWORD rd;
    ReadFile(f, buf, sz.LowPart, &rd, nullptr);
    buf[rd] = 0;
    int r, g, b;
    if (sscanf_s(buf,
        "GridSize=%d\nWindowWidth=%d\nWindowHeight=%d\n"
        "BackgroundColor=%d;%d;%d\nGridColor=%d;%d;%d",
        &currentConfig.gridSize,
        &currentConfig.clientWidth,
        &currentConfig.clientHeight,
        &r, &g, &b,
        &r, &g, &b) == 9)
    {
        currentConfig.backgroundColor = RGB(r, g, b);
        currentConfig.gridColor = RGB(r, g, b);
    }
    delete[] buf;
    CloseHandle(f);
}
void SaveConfigWinAPI() {
    HANDLE f = CreateFile(configFileName, GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    char buf[512];
    int len = sprintf_s(buf,
        "GridSize=%d\nWindowWidth=%d\nWindowHeight=%d\n"
        "BackgroundColor=%d;%d;%d\nGridColor=%d;%d;%d\n",
        currentConfig.gridSize,
        currentConfig.clientWidth,
        currentConfig.clientHeight,
        GetRValue(currentConfig.backgroundColor),
        GetGValue(currentConfig.backgroundColor),
        GetBValue(currentConfig.backgroundColor),
        GetRValue(currentConfig.gridColor),
        GetGValue(currentConfig.gridColor),
        GetBValue(currentConfig.gridColor)
    );
    DWORD written;
    WriteFile(f, buf, len, &written, nullptr);
    CloseHandle(f);
}

// ———————————————————————————————— Парсинг аргументов —————————————————————————————————————
void ParseCommandLine(LPCTSTR lpCmdLine) {
    int argc; LPWSTR* argv = CommandLineToArgvW(lpCmdLine, &argc);
    for (int i = 0; i < argc; i++) {
        if (!_wcsicmp(argv[i], L"-m1")) configMethod = METHOD_MAPPING;
        if (!_wcsicmp(argv[i], L"-m2")) configMethod = METHOD_FILEVARS;
        if (!_wcsicmp(argv[i], L"-m3")) configMethod = METHOD_FSTREAM;
        if (!_wcsicmp(argv[i], L"-m4")) configMethod = METHOD_WINAPI;
        if (!_wcsicmp(argv[i], L"-grid") && i + 1 < argc)
            currentConfig.gridSize = _wtoi(argv[++i]);
        if (!_wcsicmp(argv[i], L"-width") && i + 1 < argc)
            currentConfig.clientWidth = _wtoi(argv[++i]);
        if (!_wcsicmp(argv[i], L"-height") && i + 1 < argc)
            currentConfig.clientHeight = _wtoi(argv[++i]);
    }
    LocalFree(argv);
}

// ——————————————————————————————— IPC helpers ——————————————————————————————————————

void BroadcastUpdate() {
    // 1) Local window: invalidate+immediate repaint
    InvalidateRect(hwnd, NULL, TRUE);
    UpdateWindow(hwnd);

    SendMessageTimeout(
        HWND_BROADCAST,
        WM_IPC_UPDATE,
        0, 0,
        SMTO_ABORTIFHUNG | SMTO_NOTIMEOUTIFNOTHUNG,
        100,
        nullptr
    );
}

void UpdateFromShared(HWND hWnd) {
    UpdateBackgroundBrush(hWnd, pShared->backgroundColor);
    InvalidateRect(hWnd, NULL, TRUE);
    UpdateWindow(hWnd);
}

void PlaceWindowNonOverlapping(HWND hNew) {
    int offsetX = 0;
    HWND cur = nullptr;
    while ((cur = FindWindowEx(
        nullptr, cur,
        _T("IPCGridClass"), nullptr)) != nullptr) 
    {
        if (cur == hNew) {
            continue;
        }
        if (IsWindowVisible(cur)) {
            RECT r;
            GetWindowRect(cur, &r);
            offsetX += (r.right - r.left) + 10;
        }
    }
    SetWindowPos(hNew, HWND_TOP, offsetX, 0, 0, 0,
        SWP_NOZORDER | SWP_NOSIZE);
}


// ——————————————————————————————— Графика и ввод ——————————————————————————————————————

LRESULT CALLBACK WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_IPC_UPDATE) {
        UpdateFromShared(hWnd);
        return 0;
    }
    switch (msg) {
    case WM_CREATE:
        // Фон из SHARED
        UpdateBackgroundBrush(hWnd, pShared->backgroundColor);
        break;

    case WM_SIZE:
        currentConfig.clientWidth = LOWORD(lParam);
        currentConfig.clientHeight = HIWORD(lParam);
        InvalidateRect(hWnd, nullptr, TRUE);
        return 0;

    case WM_ERASEBKGND: {
        HDC dc = (HDC)wParam;
        RECT rc;
        GetClientRect(hWnd, &rc);
        // paint with the *current* shared background color
        HBRUSH hbr = CreateSolidBrush(pShared->backgroundColor);
        FillRect(dc, &rc, hbr);
        DeleteObject(hbr);
        return 1;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(hWnd, &ps);
        int sz = currentConfig.gridSize;
        RECT rc; GetClientRect(hWnd, &rc);
        int cw = (rc.right - rc.left) / sz, ch = (rc.bottom - rc.top) / sz;

        // Сетка
        HPEN pen = CreatePen(PS_SOLID, 1, pShared->gridColor);
        SelectObject(dc, pen);
        for (int i = 0; i <= sz; i++) {
            MoveToEx(dc, i * cw, 0, nullptr);
            LineTo(dc, i * cw, rc.bottom);
            MoveToEx(dc, 0, i * ch, nullptr);
            LineTo(dc, rc.right, i * ch);
        }
        DeleteObject(pen);

        // Клетки
        for (int r = 0; r < sz; r++) {
            for (int c = 0; c < sz; c++) {
                int v = pShared->cells[r * sz + c];
                int x0 = c * cw, y0 = r * ch;
                if (v == 1) Ellipse(dc, x0 + 5, y0 + 5, x0 + cw - 5, y0 + ch - 5);
                else if (v == 2) {
                    MoveToEx(dc, x0 + 5, y0 + 5, nullptr);
                    LineTo(dc, x0 + cw - 5, y0 + ch - 5);
                    MoveToEx(dc, x0 + 5, y0 + ch - 5, nullptr);
                    LineTo(dc, x0 + cw - 5, y0 + 5);
                }
            }
        }
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN: {
        // Мышь в клетку
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        RECT rc; GetClientRect(hWnd, &rc);
        int sz = currentConfig.gridSize;
        int cw = (rc.right - rc.left) / sz, ch = (rc.bottom - rc.top) / sz;
        int col = pt.x / cw, row = pt.y / ch;
        pShared->cells[row * sz + col] = (msg == WM_LBUTTONDOWN ? 1 : 2);
        BroadcastUpdate();
        return 0;
    }

    case WM_MOUSEWHEEL: {
        ChangeGridLineColor(GET_WHEEL_DELTA_WPARAM(wParam));
        BroadcastUpdate();
        return 0;
    }

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) DestroyWindow(hWnd);
        if (wParam == 'Q' && (GetAsyncKeyState(VK_CONTROL) < 0)) DestroyWindow(hWnd);
        if (wParam == VK_RETURN) {
            COLORREF nc;
            do { nc = RGB(rand() % 256, rand() % 256, rand() % 256); } while (nc == RGB(255, 0, 0));

            // store it
            pShared->backgroundColor = nc;

            // repaint self + everyone else
            BroadcastUpdate();
        }
        if (wParam == 'C' && (GetKeyState(VK_SHIFT) & 0x8000)) LaunchNotepad();
        return 0;

    case WM_DESTROY:
        SaveConfig();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// Меняем кисть фона
void UpdateBackgroundBrush(COLORREF c) {
    if (hBackgroundBrush)
        DeleteObject(hBackgroundBrush);
    hBackgroundBrush = CreateSolidBrush(c);
    // используем глобальный hwnd
    SetClassLongPtr(hwnd, GCLP_HBRBACKGROUND, (LONG_PTR)hBackgroundBrush);
}

// HSV→RGB
COLORREF HSVtoRGB(float H, float S, float V) {
    float r, g, b;
    int i = (int)(H / 60) % 6;
    float f = (H / 60) - i, p = V * (1 - S), q = V * (1 - f * S), t = V * (1 - (1 - f) * S);
    switch (i) {
    case 0: r = V, g = t, b = p; break;
    case 1: r = q, g = V, b = p; break;
    case 2: r = p, g = V, b = t; break;
    case 3: r = p, g = q, b = V; break;
    case 4: r = t, g = p, b = V; break;
    default:r = V, g = p, b = q; break;
    }
    return RGB((int)(r * 255), (int)(g * 255), (int)(b * 255));
}

// Колёсико меняет цвет линии
void ChangeGridLineColor(int delta) {
    static float hue = 0;
    hue += (delta / 120) * 5;
    if (hue < 0) hue += 360; if (hue >= 360) hue -= 360;
    pShared->gridColor = HSVtoRGB(hue, 1, 1);
}

// Запуск блокнота
void LaunchNotepad() {
    STARTUPINFO si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    TCHAR cmd[] = _T("notepad.exe");
    if (!CreateProcess(nullptr, cmd,
        nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
    {
        MessageBox(hwnd, _T("Не удалось запустить Notepad"), _T("Ошибка"), MB_ICONERROR);
    }
    else {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}
