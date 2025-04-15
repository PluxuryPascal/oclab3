#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fstream>
#include <sstream>
#include <chrono>
#include <vector>
#include <shellapi.h>
#include <iostream>

#define DEFAULT_GRID_SIZE      3
#define DEFAULT_WIN_WIDTH      320
#define DEFAULT_WIN_HEIGHT     240

struct Config {
    int gridSize;
    int clientWidth;
    int clientHeight;
    COLORREF backgroundColor;
    COLORREF gridColor;
};

const TCHAR szWinClass[] = _T("Win32SampleApp");
const TCHAR szWinName[] = _T("Win32SampleWindow");
const TCHAR* configFileName = _T("config.txt");
const TCHAR* dataFileName = _T("data.bin");

enum ConfigMethod { METHOD_MAPPING, METHOD_FILEVARS, METHOD_FSTREAM, METHOD_WINAPI };
ConfigMethod configMethod = METHOD_MAPPING;

HINSTANCE hInst;
HWND hwnd;
HBRUSH hBackgroundBrush = NULL;
int* gridCells = NULL;
float hue = 0.0f;

Config currentConfig = {
    DEFAULT_GRID_SIZE,
    DEFAULT_WIN_WIDTH,
    DEFAULT_WIN_HEIGHT,
    RGB(0, 0, 255),
    RGB(255, 0, 0)
};

LRESULT CALLBACK WindowProcedure(HWND, UINT, WPARAM, LPARAM);
void UpdateBackgroundBrush(COLORREF newColor);
COLORREF HSVtoRGB(float H, float S, float V);
void ChangeGridLineColor(int delta);
void LaunchNotepad();
void InitializeGrid();
void CleanupGrid();
void ParseCommandLine(LPCTSTR lpCmdLine);

void LoadConfig();
void SaveConfig();
void LoadConfigMapping();
void SaveConfigMapping();
void LoadConfigFileVars();
void SaveConfigFileVars();
void LoadConfigFStream();
void SaveConfigFStream();
void LoadConfigWinAPI();
void SaveConfigWinAPI();

void LoadConfig() {
    switch (configMethod) {
    case METHOD_MAPPING: LoadConfigMapping(); break;
    case METHOD_FILEVARS: LoadConfigFileVars(); break;
    case METHOD_FSTREAM: LoadConfigFStream(); break;
    case METHOD_WINAPI: LoadConfigWinAPI(); break;
    }
}

void SaveConfig() {
    switch (configMethod) {
    case METHOD_MAPPING: SaveConfigMapping(); break;
    case METHOD_FILEVARS: SaveConfigFileVars(); break;
    case METHOD_FSTREAM: SaveConfigFStream(); break;
    case METHOD_WINAPI: SaveConfigWinAPI(); break;
    }
}

// 1. Memory Mapping
void LoadConfigMapping() {
    HANDLE hFile = CreateFile(configFileName, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        MessageBox(hwnd, _T("Ошибка открытия файла конфигурации (Memory Mapping)"), _T("Ошибка"), MB_ICONERROR);
        return;
    }

    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize)) {
        MessageBox(hwnd, _T("Ошибка получения размера файла конфигурации (Memory Mapping)"), _T("Ошибка"), MB_ICONERROR);
        CloseHandle(hFile);
        return;
    }

    HANDLE hMapping = CreateFileMapping(hFile, NULL, PAGE_READONLY,
        fileSize.HighPart, fileSize.LowPart, NULL);
    if (!hMapping) {
        MessageBox(hwnd, _T("Ошибка создания отображения файла (Memory Mapping)"), _T("Ошибка"), MB_ICONERROR);
        CloseHandle(hFile);
        return;
    }

    LPVOID pData = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, fileSize.LowPart);
    if (!pData) {
        MessageBox(hwnd, _T("Ошибка отображения файла в память (Memory Mapping)"), _T("Ошибка"), MB_ICONERROR);
        CloseHandle(hMapping);
        CloseHandle(hFile);
        return;
    }

    // Копирование данных и разбор конфигурации
    char* buffer = new char[fileSize.LowPart + 1];
    memcpy(buffer, pData, fileSize.LowPart);
    buffer[fileSize.LowPart] = '\0';

    int r1, g1, b1, r2, g2, b2;
    int parsed = sscanf_s(buffer,
        "GridSize=%d\nWindowWidth=%d\nWindowHeight=%d\n"
        "BackgroundColor=%d;%d;%d\nGridColor=%d;%d;%d",
        &currentConfig.gridSize,
        &currentConfig.clientWidth,
        &currentConfig.clientHeight,
        &r1, &g1, &b1,
        &r2, &g2, &b2
    );
    if (parsed == 9) {
        currentConfig.backgroundColor = RGB(r1, g1, b1);
        currentConfig.gridColor = RGB(r2, g2, b2);
    }
    else {
        MessageBox(hwnd, _T("Ошибка формата конфигурации (Memory Mapping)"), _T("Ошибка"), MB_ICONERROR);
    }

    delete[] buffer;
    UnmapViewOfFile(pData);
    CloseHandle(hMapping);
    CloseHandle(hFile);
}

void SaveConfigMapping() {
    const int bufferSize = 256;
    char buffer[bufferSize];
    int written = snprintf(buffer, bufferSize,
        "GridSize=%d\n"
        "WindowWidth=%d\n"
        "WindowHeight=%d\n"
        "BackgroundColor=%d;%d;%d\n"
        "GridColor=%d;%d;%d\n",
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
    if (written < 0 || written >= bufferSize) {
        MessageBox(hwnd, _T("Ошибка форматирования конфигурации (Memory Mapping)"), _T("Ошибка"), MB_ICONERROR);
        return;
    }

    HANDLE hFile = CreateFile(configFileName,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        MessageBox(hwnd, _T("Ошибка создания файла конфигурации (Memory Mapping)"), _T("Ошибка"), MB_ICONERROR);
        return;
    }

    LARGE_INTEGER fileSize;
    fileSize.QuadPart = written;
    if (!SetFilePointerEx(hFile, fileSize, NULL, FILE_BEGIN) ||
        !SetEndOfFile(hFile)) {
        MessageBox(hwnd, _T("Ошибка установки размера файла (Memory Mapping)"), _T("Ошибка"), MB_ICONERROR);
        CloseHandle(hFile);
        return;
    }

    HANDLE hMapping = CreateFileMapping(hFile, NULL, PAGE_READWRITE,
        fileSize.HighPart, fileSize.LowPart, NULL);
    if (!hMapping) {
        MessageBox(hwnd, _T("Ошибка создания отображения файла для записи (Memory Mapping)"), _T("Ошибка"), MB_ICONERROR);
        CloseHandle(hFile);
        return;
    }

    LPVOID pData = MapViewOfFile(hMapping, FILE_MAP_WRITE, 0, 0, written);
    if (!pData) {
        MessageBox(hwnd, _T("Ошибка отображения файла в память для записи (Memory Mapping)"), _T("Ошибка"), MB_ICONERROR);
        CloseHandle(hMapping);
        CloseHandle(hFile);
        return;
    }

    memcpy(pData, buffer, written);
    if (!FlushViewOfFile(pData, written)) {
        MessageBox(hwnd, _T("Ошибка сброса данных в файл (Memory Mapping)"), _T("Ошибка"), MB_ICONERROR);
    }

    UnmapViewOfFile(pData);
    CloseHandle(hMapping);
    CloseHandle(hFile);
}


void LoadConfigFileVars() {
    FILE* fp = fopen("config.txt", "r");
    if (!fp) {
        MessageBox(hwnd, _T("Ошибка открытия файла конфигурации (FILEVARS)"), _T("Ошибка"), MB_ICONERROR);
        return;
    }

    // Определяем размер файла
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);

    char* buffer = new char[size + 1];
    size_t bytesRead = fread(buffer, 1, size, fp);
    buffer[bytesRead] = '\0';
    fclose(fp);

    // Разбиваем содержимое на строки
    char* line = strtok(buffer, "\n");
    while (line != NULL) {
        if (strncmp(line, "GridSize=", 9) == 0) {
            currentConfig.gridSize = atoi(line + 9);
        }
        else if (strncmp(line, "WindowWidth=", 12) == 0) {
            currentConfig.clientWidth = atoi(line + 12);
        }
        else if (strncmp(line, "WindowHeight=", 13) == 0) {
            currentConfig.clientHeight = atoi(line + 13);
        }
        else if (strncmp(line, "BackgroundColor=", 16) == 0) {
            int r = 0, g = 0, b = 0;
            char temp[64];
            strcpy(temp, line + 16);
            char* token = strtok(temp, ";");
            if (token) { r = atoi(token); token = strtok(NULL, ";"); }
            if (token) { g = atoi(token); token = strtok(NULL, ";"); }
            if (token) { b = atoi(token); }
            currentConfig.backgroundColor = RGB(r, g, b);
        }
        else if (strncmp(line, "GridColor=", 10) == 0) {
            int r = 0, g = 0, b = 0;
            char temp[64];
            strcpy(temp, line + 10);
            char* token = strtok(temp, ";");
            if (token) { r = atoi(token); token = strtok(NULL, ";"); }
            if (token) { g = atoi(token); token = strtok(NULL, ";"); }
            if (token) { b = atoi(token); }
            currentConfig.gridColor = RGB(r, g, b);
        }
        line = strtok(NULL, "\n");
    }
    delete[] buffer;
}


void SaveConfigFileVars() {
    FILE* fp = fopen("config.txt", "w");
    if (!fp) {
        MessageBox(hwnd, _T("Ошибка создания файла конфигурации (FILEVARS)"), _T("Ошибка"), MB_ICONERROR);
        return;
    }

    char buffer[256];
    int len = sprintf(buffer,
        "GridSize=%d\n"
        "WindowWidth=%d\n"
        "WindowHeight=%d\n"
        "BackgroundColor=%d;%d;%d\n"
        "GridColor=%d;%d;%d\n",
        currentConfig.gridSize,
        currentConfig.clientWidth,
        currentConfig.clientHeight,
        GetRValue(currentConfig.backgroundColor),
        GetGValue(currentConfig.backgroundColor),
        GetBValue(currentConfig.backgroundColor),
        GetRValue(currentConfig.gridColor),
        GetGValue(currentConfig.gridColor),
        GetBValue(currentConfig.gridColor));

    fwrite(buffer, 1, len, fp);
    fclose(fp);
}

void LoadConfigFStream() {
    std::ifstream file(configFileName);
    if (!file) {
        MessageBox(hwnd, _T("Ошибка открытия файла конфигурации (FSTREAM)"), _T("Ошибка"), MB_ICONERROR);
        return;
    }
    std::string line;
    int r1, g1, b1, r2, g2, b2;
    bool parsedGridSize = false, parsedWinWidth = false, parsedWinHeight = false;
    bool parsedBG = false, parsedGrid = false;
    while (getline(file, line)) {
        if (!parsedGridSize && sscanf(line.c_str(), "GridSize=%d", &currentConfig.gridSize) == 1) { parsedGridSize = true; continue; }
        if (!parsedWinWidth && sscanf(line.c_str(), "WindowWidth=%d", &currentConfig.clientWidth) == 1) { parsedWinWidth = true; continue; }
        if (!parsedWinHeight && sscanf(line.c_str(), "WindowHeight=%d", &currentConfig.clientHeight) == 1) { parsedWinHeight = true; continue; }
        if (!parsedBG && sscanf(line.c_str(), "BackgroundColor=%d;%d;%d", &r1, &g1, &b1) == 3) { currentConfig.backgroundColor = RGB(r1, g1, b1); parsedBG = true; continue; }
        if (!parsedGrid && sscanf(line.c_str(), "GridColor=%d;%d;%d", &r2, &g2, &b2) == 3) { currentConfig.gridColor = RGB(r2, g2, b2); parsedGrid = true; continue; }
    }
    if (!(parsedGridSize && parsedWinWidth && parsedWinHeight && parsedBG && parsedGrid)) {
        MessageBox(hwnd, _T("Ошибка формата конфигурации (FSTREAM)"), _T("Ошибка"), MB_ICONERROR);
    }
}

void SaveConfigFStream() {
    std::ofstream file(configFileName);
    if (!file) {
        MessageBox(hwnd, _T("Ошибка создания файла конфигурации (FSTREAM)"), _T("Ошибка"), MB_ICONERROR);
        return;
    }
    file << "GridSize=" << currentConfig.gridSize << "\n";
    file << "WindowWidth=" << currentConfig.clientWidth << "\n";
    file << "WindowHeight=" << currentConfig.clientHeight << "\n";
    file << "BackgroundColor=" << (int)GetRValue(currentConfig.backgroundColor) << ";"
        << (int)GetGValue(currentConfig.backgroundColor) << ";"
        << (int)GetBValue(currentConfig.backgroundColor) << "\n";
    file << "GridColor=" << (int)GetRValue(currentConfig.gridColor) << ";"
        << (int)GetGValue(currentConfig.gridColor) << ";"
        << (int)GetBValue(currentConfig.gridColor) << "\n";
}

// 4. WinAPI
void LoadConfigWinAPI() {
    HANDLE hFile = CreateFile(configFileName, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        MessageBox(hwnd, _T("Ошибка открытия файла конфигурации (WINAPI)"), _T("Ошибка"), MB_ICONERROR);
        return;
    }

    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize)) {
        MessageBox(hwnd, _T("Ошибка получения размера файла (WINAPI)"), _T("Ошибка"), MB_ICONERROR);
        CloseHandle(hFile);
        return;
    }

    char* buffer = new char[fileSize.LowPart + 1];
    DWORD bytesRead;
    if (!ReadFile(hFile, buffer, fileSize.LowPart, &bytesRead, NULL)) {
        MessageBox(hwnd, _T("Ошибка чтения файла конфигурации (WINAPI)"), _T("Ошибка"), MB_ICONERROR);
        delete[] buffer;
        CloseHandle(hFile);
        return;
    }
    buffer[bytesRead] = '\0';

    int r1, g1, b1, r2, g2, b2;
    int parsed = sscanf_s(buffer,
        "GridSize=%d\nWindowWidth=%d\nWindowHeight=%d\n"
        "BackgroundColor=%d;%d;%d\nGridColor=%d;%d;%d",
        &currentConfig.gridSize,
        &currentConfig.clientWidth,
        &currentConfig.clientHeight,
        &r1, &g1, &b1,
        &r2, &g2, &b2);
    if (parsed == 9) {
        currentConfig.backgroundColor = RGB(r1, g1, b1);
        currentConfig.gridColor = RGB(r2, g2, b2);
    }
    else {
        MessageBox(hwnd, _T("Ошибка формата конфигурации (WINAPI)"), _T("Ошибка"), MB_ICONERROR);
    }

    delete[] buffer;
    CloseHandle(hFile);
}


void SaveConfigWinAPI() {
    HANDLE hFile = CreateFile(configFileName, GENERIC_WRITE, 0, NULL,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        MessageBox(hwnd, _T("Ошибка создания файла конфигурации (WINAPI)"), _T("Ошибка"), MB_ICONERROR);
        return;
    }

    DWORD bytesWritten;
    char buffer[256];
    int len = sprintf_s(buffer,
        "GridSize=%d\nWindowWidth=%d\nWindowHeight=%d\n"
        "BackgroundColor=%d;%d;%d\nGridColor=%d;%d;%d",
        currentConfig.gridSize, currentConfig.clientWidth,
        currentConfig.clientHeight,
        GetRValue(currentConfig.backgroundColor),
        GetGValue(currentConfig.backgroundColor),
        GetBValue(currentConfig.backgroundColor),
        GetRValue(currentConfig.gridColor),
        GetGValue(currentConfig.gridColor),
        GetBValue(currentConfig.gridColor));
    if (!WriteFile(hFile, buffer, len, &bytesWritten, NULL)) {
        MessageBox(hwnd, _T("Ошибка записи в файл конфигурации (WINAPI)"), _T("Ошибка"), MB_ICONERROR);
    }
    CloseHandle(hFile);
}

LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        srand((unsigned)time(NULL));
        InitializeGrid();
        break;

    case WM_SIZE:
        currentConfig.clientWidth = LOWORD(lParam);
        currentConfig.clientHeight = HIWORD(lParam);
        InvalidateRect(hwnd, NULL, TRUE);
        break;

    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rect;
        GetClientRect(hwnd, &rect);
        FillRect(hdc, &rect, hBackgroundBrush);
        return 1;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        int cellWidth = currentConfig.clientWidth / currentConfig.gridSize;
        int cellHeight = currentConfig.clientHeight / currentConfig.gridSize;

        HPEN hPen = CreatePen(PS_SOLID, 1, currentConfig.gridColor);
        HGDIOBJ oldPen = SelectObject(hdc, hPen);

        for (int i = 0; i <= currentConfig.gridSize; i++) {
            int x = i * cellWidth;
            MoveToEx(hdc, x, 0, NULL);
            LineTo(hdc, x, currentConfig.clientHeight);
        }
        for (int j = 0; j <= currentConfig.gridSize; j++) {
            int y = j * cellHeight;
            MoveToEx(hdc, 0, y, NULL);
            LineTo(hdc, currentConfig.clientWidth, y);
        }

        SelectObject(hdc, oldPen);
        DeleteObject(hPen);

        for (int row = 0; row < currentConfig.gridSize; row++) {
            for (int col = 0; col < currentConfig.gridSize; col++) {
                int cellState = gridCells[row * currentConfig.gridSize + col];
                if (cellState == 0) continue;

                int x0 = col * cellWidth;
                int y0 = row * cellHeight;
                int x1 = x0 + cellWidth;
                int y1 = y0 + cellHeight;

                if (cellState == 1) {
                    Ellipse(hdc, x0 + 5, y0 + 5, x1 - 5, y1 - 5);
                }
                else if (cellState == 2) {
                    MoveToEx(hdc, x0 + 5, y0 + 5, NULL);
                    LineTo(hdc, x1 - 5, y1 - 5);
                    MoveToEx(hdc, x0 + 5, y1 - 5, NULL);
                    LineTo(hdc, x1 - 5, y0 + 5);
                }
            }
        }
        EndPaint(hwnd, &ps);
        break;
    }

    case WM_LBUTTONDOWN: {
        int xPos = LOWORD(lParam);
        int yPos = HIWORD(lParam);
        int cellWidth = currentConfig.clientWidth / currentConfig.gridSize;
        int cellHeight = currentConfig.clientHeight / currentConfig.gridSize;
        int col = xPos / cellWidth;
        int row = yPos / cellHeight;
        if (row < currentConfig.gridSize && col < currentConfig.gridSize) {
            gridCells[row * currentConfig.gridSize + col] = 1;
            InvalidateRect(hwnd, NULL, TRUE);
        }
        break;
    }

    case WM_RBUTTONDOWN: {
        int xPos = LOWORD(lParam);
        int yPos = HIWORD(lParam);
        int cellWidth = currentConfig.clientWidth / currentConfig.gridSize;
        int cellHeight = currentConfig.clientHeight / currentConfig.gridSize;
        int col = xPos / cellWidth;
        int row = yPos / cellHeight;
        if (row < currentConfig.gridSize && col < currentConfig.gridSize) {
            gridCells[row * currentConfig.gridSize + col] = 2;
            InvalidateRect(hwnd, NULL, TRUE);
        }
        break;
    }

    case WM_MOUSEWHEEL: {
        int zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        ChangeGridLineColor(zDelta);
        InvalidateRect(hwnd, NULL, TRUE);
        break;
    }

    case WM_KEYDOWN: {
        if (wParam == VK_ESCAPE) DestroyWindow(hwnd);

        if (wParam == 'Q' && (GetAsyncKeyState(VK_CONTROL) < 0)) {
            DestroyWindow(hwnd);
        }

        if (wParam == VK_RETURN) {
            COLORREF newColor;
            do {
                newColor = RGB(rand() % 256, rand() % 256, rand() % 256);
            } while (newColor == RGB(255, 0, 0));

            currentConfig.backgroundColor = newColor;
            UpdateBackgroundBrush(newColor);
            InvalidateRect(hwnd, NULL, TRUE);
        }

        if (wParam == 'C' && (GetKeyState(VK_SHIFT) & 0x8000)) {
            LaunchNotepad();
        }
        break;
    }

    case WM_DESTROY:
        SaveConfig();
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, message, wParam, lParam);
    }
    return 0;
}

void UpdateBackgroundBrush(COLORREF newColor) {
    if (hBackgroundBrush) DeleteObject(hBackgroundBrush);
    hBackgroundBrush = CreateSolidBrush(newColor);
    SetClassLongPtr(hwnd, GCLP_HBRBACKGROUND, (LONG_PTR)hBackgroundBrush);
}

COLORREF HSVtoRGB(float H, float S, float V) {
    float r, g, b;
    int i = (int)(H / 60.0f) % 6;
    float f = (H / 60.0f) - i;
    float p = V * (1 - S);
    float q = V * (1 - f * S);
    float t = V * (1 - (1 - f) * S);
    switch (i) {
    case 0: r = V; g = t; b = p; break;
    case 1: r = q; g = V; b = p; break;
    case 2: r = p; g = V; b = t; break;
    case 3: r = p; g = q; b = V; break;
    case 4: r = t; g = p; b = V; break;
    case 5: r = V; g = p; b = q; break;
    default: r = g = b = 0; break;
    }
    return RGB((int)(r * 255), (int)(g * 255), (int)(b * 255));
}

void ChangeGridLineColor(int delta) {
    float step = ((float)delta / WHEEL_DELTA) * 5.0f;
    hue += step;
    if (hue < 0) hue += 360;
    if (hue >= 360) hue -= 360;
    currentConfig.gridColor = HSVtoRGB(hue, 1.0f, 1.0f);
}

void LaunchNotepad() {
    STARTUPINFO si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    TCHAR cmdLine[] = _T("notepad.exe");
    if (!CreateProcess(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        MessageBox(hwnd, _T("Ошибка запуска Блокнота"), _T("Ошибка"), MB_ICONERROR);
    }
    else {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

void InitializeGrid() {
    if (gridCells) free(gridCells);
    gridCells = (int*)malloc(currentConfig.gridSize * currentConfig.gridSize * sizeof(int));
    if (gridCells) memset(gridCells, 0, currentConfig.gridSize * currentConfig.gridSize * sizeof(int));
}

void CleanupGrid() {
    if (gridCells) {
        free(gridCells);
        gridCells = NULL;
    }
}

void ParseCommandLine(LPCTSTR lpCmdLine) {
    TCHAR** argv;
    int argc;
    argv = CommandLineToArgvW(lpCmdLine, &argc);

    for (int i = 0; i < argc; i++) {
        bool processed = false;

        if (_tcsicmp(argv[i], _T("-m1")) == 0) {
            configMethod = METHOD_MAPPING;
            processed = true;
        }
        else if (_tcsicmp(argv[i], _T("-m2")) == 0) {
            configMethod = METHOD_FILEVARS;
            processed = true;
        }
        else if (_tcsicmp(argv[i], _T("-m3")) == 0) {
            configMethod = METHOD_FSTREAM;
            processed = true;
        }
        else if (_tcsicmp(argv[i], _T("-m4")) == 0) {
            configMethod = METHOD_WINAPI;
            processed = true;
        }
        else if (_tcsicmp(argv[i], _T("-grid")) == 0) {
            if (i + 1 < argc) {
                currentConfig.gridSize = _ttoi(argv[i + 1]);
                i++;
                processed = true;
            }
        }
        else if (_tcsicmp(argv[i], _T("-width")) == 0) {
            if (i + 1 < argc) {
                currentConfig.clientWidth = _ttoi(argv[i + 1]);
                i++;
                processed = true;
            }
        }
        else if (_tcsicmp(argv[i], _T("-height")) == 0) {
            if (i + 1 < argc) {
                currentConfig.clientHeight = _ttoi(argv[i + 1]);
                i++;
                processed = true;
            }
        }

        if (!processed) {
            TCHAR errorMsg[256];
            _stprintf_s(errorMsg, 256, _T("Unknown parameter: %s\nValid methods are: -m1, -m2, -m3, -m4"), argv[i]);
            MessageBox(NULL, errorMsg, _T("Error"), MB_ICONERROR | MB_OK);
            LocalFree(argv);
            ExitProcess(1);
        }
    }
    LocalFree(argv);
}


int WINAPI _tWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nCmdShow) {

    ParseCommandLine(lpCmdLine);
    LoadConfig();

    if (lpCmdLine && _tcslen(lpCmdLine) > 0) {
        int argGrid = _ttoi(lpCmdLine);
        if (argGrid > 0) currentConfig.gridSize = argGrid;
    }

    InitializeGrid();

    hInst = hInstance;
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WindowProcedure;
    wc.hInstance = hInstance;
    wc.lpszClassName = szWinClass;
    hBackgroundBrush = CreateSolidBrush(currentConfig.backgroundColor);
    wc.hbrBackground = CreateSolidBrush(currentConfig.backgroundColor);

    if (!RegisterClass(&wc)) return 0;

    hwnd = CreateWindow(szWinClass, szWinName, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, currentConfig.clientWidth, currentConfig.clientHeight,
        NULL, NULL, hInstance, NULL);

    if (!hwnd) return 0;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CleanupGrid();

    if (IsWindow(hwnd)) DestroyWindow(hwnd);
    UnregisterClass(szWinClass, hInst);

    return (int)msg.wParam;
}