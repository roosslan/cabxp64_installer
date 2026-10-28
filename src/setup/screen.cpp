#include "screen.h"

#include <vector>

#include <windows.h>

#include "../common/fsutil.h"

namespace scr {

namespace {

HANDLE hOut = INVALID_HANDLE_VALUE;
HANDLE hIn = INVALID_HANDLE_VALUE;
bool g_active = false;

CHAR_INFO g_buf[W * H];
int g_cursorX = -1, g_cursorY = 0;

// Сохранённое состояние консоли.
CONSOLE_SCREEN_BUFFER_INFOEX g_savedInfo = {};
CONSOLE_FONT_INFOEX g_savedFont = {};
CONSOLE_CURSOR_INFO g_savedCursor = {};
DWORD g_savedInMode = 0;
UINT g_savedCp = 0;
bool g_haveInfo = false, g_haveFont = false;

// Палитра VGA: так выглядел текстовый режим программы установки.
const COLORREF kVga[16] = {
    RGB(0, 0, 0),       RGB(0, 0, 170),     RGB(0, 170, 0),     RGB(0, 170, 170),
    RGB(170, 0, 0),     RGB(170, 0, 170),   RGB(170, 85, 0),    RGB(170, 170, 170),
    RGB(85, 85, 85),    RGB(85, 85, 255),   RGB(85, 255, 85),   RGB(85, 255, 255),
    RGB(255, 85, 85),   RGB(255, 85, 255),  RGB(255, 255, 85),  RGB(255, 255, 255),
};

BOOL WINAPI ctrlHandler(DWORD type)
{
    // Ctrl+C и Ctrl+Break не прерывают установку.
    return type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT;
}

void setFont()
{
    CONSOLE_FONT_INFOEX f = {};
    f.cbSize = sizeof(f);
    f.FontWeight = FW_NORMAL;
    // Растровый шрифт «Terminal» содержит только символы OEM-кодировки
    // системы: кириллица есть лишь при кодовой странице 866.
    if (GetOEMCP() == 866)
    {
        f.dwFontSize.X = 8;
        f.dwFontSize.Y = 12;
        f.FontFamily = FF_MODERN | FIXED_PITCH;
        lstrcpynW(f.FaceName, L"Terminal", LF_FACESIZE);
        if (SetCurrentConsoleFontEx(hOut, FALSE, &f))
        {
            CONSOLE_FONT_INFOEX now = {};
            now.cbSize = sizeof(now);
            if (GetCurrentConsoleFontEx(hOut, FALSE, &now) && lstrcmpiW(now.FaceName, L"Terminal") == 0)
                return;
        }
    }
    f.dwFontSize.X = 0;
    f.dwFontSize.Y = 16;
    f.FontFamily = FF_MODERN | TMPF_TRUETYPE;
    lstrcpynW(f.FaceName, L"Lucida Console", LF_FACESIZE);
    SetCurrentConsoleFontEx(hOut, FALSE, &f);
}

void setSize()
{
    SMALL_RECT tiny = {0, 0, 1, 1};
    SetConsoleWindowInfo(hOut, TRUE, &tiny);
    COORD size = {W, H};
    SetConsoleScreenBufferSize(hOut, size);
    SMALL_RECT win = {0, 0, W - 1, H - 1};
    SetConsoleWindowInfo(hOut, TRUE, &win);
}

void centerWindow()
{
    HWND wnd = GetConsoleWindow();
    RECT r;
    if (!wnd || !GetWindowRect(wnd, &r))
        return;
    HMONITOR mon = MonitorFromWindow(wnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {sizeof(mi)};
    if (!GetMonitorInfoW(mon, &mi))
        return;
    int w = r.right - r.left, h = r.bottom - r.top;
    int x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - w) / 2;
    int y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - h) / 2;
    SetWindowPos(wnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

std::wstring wide(const std::string &s)
{
    return fsu::widen(s);
}

} // namespace

bool init(const std::string &title)
{
    hOut = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                       nullptr, OPEN_EXISTING, 0, nullptr);
    hIn = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                      nullptr, OPEN_EXISTING, 0, nullptr);
    if (hOut == INVALID_HANDLE_VALUE || hIn == INVALID_HANDLE_VALUE)
        return false;

    fsu::setConsoleTitleAndIcon(title);

    g_savedInfo.cbSize = sizeof(g_savedInfo);
    g_haveInfo = GetConsoleScreenBufferInfoEx(hOut, &g_savedInfo) != FALSE;
    g_savedFont.cbSize = sizeof(g_savedFont);
    g_haveFont = GetCurrentConsoleFontEx(hOut, FALSE, &g_savedFont) != FALSE;
    GetConsoleCursorInfo(hOut, &g_savedCursor);
    GetConsoleMode(hIn, &g_savedInMode);
    g_savedCp = GetConsoleOutputCP();

    // Выделение мышью разрешено, пока программа ждёт клавишу; на время
    // копирования оно выключается (см. lockClose). Ctrl+C обрабатывает программа.
    SetConsoleMode(hIn, ENABLE_EXTENDED_FLAGS | ENABLE_QUICK_EDIT_MODE);
    SetConsoleOutputCP(866);
    SetConsoleCtrlHandler(ctrlHandler, TRUE);

    setFont();
    setSize();

    CONSOLE_SCREEN_BUFFER_INFOEX info = {};
    info.cbSize = sizeof(info);
    if (GetConsoleScreenBufferInfoEx(hOut, &info))
    {
        for (int i = 0; i < 16; ++i)
            info.ColorTable[i] = kVga[i];
        info.wAttributes = Text;
        SetConsoleScreenBufferInfoEx(hOut, &info);
        // Вызов уменьшает окно на одну строку и столбец — восстанавливаем.
        setSize();
    }
    centerWindow();

    CONSOLE_CURSOR_INFO ci = {25, FALSE};
    SetConsoleCursorInfo(hOut, &ci);

    g_active = true;
    clear();
    present();
    return true;
}

void restore()
{
    if (!g_active)
        return;
    g_active = false;
    lockClose(false);
    // Экран очищается в исходных цветах, чтобы в окне не осталось синего фона.
    if (g_haveInfo)
        SetConsoleScreenBufferInfoEx(hOut, &g_savedInfo);
    if (g_haveFont)
        SetCurrentConsoleFontEx(hOut, FALSE, &g_savedFont);
    SetConsoleCursorInfo(hOut, &g_savedCursor);
    SetConsoleMode(hIn, g_savedInMode);
    if (g_savedCp)
        SetConsoleOutputCP(g_savedCp);
    SetConsoleCtrlHandler(ctrlHandler, FALSE);
    CloseHandle(hOut);
    CloseHandle(hIn);
    hOut = hIn = INVALID_HANDLE_VALUE;
}

bool active()
{
    return g_active;
}

void clear(uint16_t attr)
{
    for (CHAR_INFO &c : g_buf)
    {
        c.Char.UnicodeChar = L' ';
        c.Attributes = attr;
    }
}

void put(int x, int y, uint16_t attr, const std::string &text)
{
    if (y < 0 || y >= H)
        return;
    std::wstring w = wide(text);
    for (wchar_t ch : w)
    {
        if (x >= W)
            break;
        if (x >= 0)
        {
            g_buf[y * W + x].Char.UnicodeChar = ch;
            g_buf[y * W + x].Attributes = attr;
        }
        ++x;
    }
}

void fill(int x, int y, int len, uint16_t attr, wchar_t ch)
{
    if (y < 0 || y >= H)
        return;
    for (int i = 0; i < len; ++i, ++x)
        if (x >= 0 && x < W)
        {
            g_buf[y * W + x].Char.UnicodeChar = ch;
            g_buf[y * W + x].Attributes = attr;
        }
}

int textWidth(const std::string &text)
{
    return (int) wide(text).size();
}

void center(int y, uint16_t attr, const std::string &text)
{
    put((W - textWidth(text)) / 2, y, attr, text);
}

void box(int x1, int y1, int x2, int y2, uint16_t attr, bool dbl)
{
    wchar_t h = dbl ? L'\x2550' : L'\x2500';
    wchar_t v = dbl ? L'\x2551' : L'\x2502';
    fill(x1, y1, 1, attr, dbl ? L'\x2554' : L'\x250C');
    fill(x2, y1, 1, attr, dbl ? L'\x2557' : L'\x2510');
    fill(x1, y2, 1, attr, dbl ? L'\x255A' : L'\x2514');
    fill(x2, y2, 1, attr, dbl ? L'\x255D' : L'\x2518');
    fill(x1 + 1, y1, x2 - x1 - 1, attr, h);
    fill(x1 + 1, y2, x2 - x1 - 1, attr, h);
    for (int y = y1 + 1; y < y2; ++y)
    {
        fill(x1, y, 1, attr, v);
        fill(x2, y, 1, attr, v);
    }
}

void header(const std::string &title)
{
    put(1, 1, Text, title);
    fill(0, 2, textWidth(title) + 2, Text, L'\x2550');
}

void status(const std::string &left, const std::string &right)
{
    const int sep = 50;     // начало правой части, как у оригинала
    fill(0, H - 1, W, Status);
    put(2, H - 1, Status, left);
    if (!right.empty())
    {
        fill(sep, H - 1, 1, Status, L'\x2502');
        put(sep + 1, H - 1, Status, right);
    }
}

int para(int x, int y, int width, uint16_t attr, const std::string &text)
{
    std::wstring w = wide(text);
    int lines = 0;
    size_t pos = 0;
    while (pos <= w.size())
    {
        size_t nl = w.find(L'\n', pos);
        std::wstring line = w.substr(pos, nl == std::wstring::npos ? std::wstring::npos : nl - pos);
        pos = nl == std::wstring::npos ? w.size() + 1 : nl + 1;
        // Отступ первой строки абзаца сохраняется для перенесённых строк.
        size_t indent = line.find_first_not_of(L' ');
        if (indent == std::wstring::npos)
        {
            ++lines;
            continue;
        }
        std::wstring pad(indent, L' ');
        std::wstring rest = line.substr(indent);
        bool first = true;
        while (!rest.empty())
        {
            int avail = width - (int) indent;
            size_t cut = rest.size();
            if ((int) cut > avail)
            {
                cut = rest.rfind(L' ', avail);
                if (cut == std::wstring::npos || cut == 0)
                    cut = avail;
            }
            std::wstring part = rest.substr(0, cut);
            put(x, y + lines, attr, fsu::narrow(pad + part));
            ++lines;
            rest = rest.substr(cut);
            size_t skip = rest.find_first_not_of(L' ');
            rest = skip == std::wstring::npos ? std::wstring() : rest.substr(skip);
            // Продолжение строки пункта списка выравнивается по тексту пункта.
            if (first)
            {
                size_t bullet = part.find(L'\x2022');
                if (bullet != std::wstring::npos)
                {
                    size_t t = part.find_first_not_of(L' ', bullet + 1);
                    if (t != std::wstring::npos)
                        pad = std::wstring(indent + t, L' '), indent += t;
                }
                first = false;
            }
        }
    }
    return lines;
}

void showCursor(int x, int y)
{
    g_cursorX = x;
    g_cursorY = y;
}

void present()
{
    if (!g_active)
        return;
    COORD size = {W, H}, origin = {0, 0};
    SMALL_RECT rect = {0, 0, W - 1, H - 1};
    WriteConsoleOutputW(hOut, g_buf, size, origin, &rect);
    CONSOLE_CURSOR_INFO ci = {25, g_cursorX >= 0};
    if (g_cursorX >= 0)
    {
        COORD c = {(SHORT) g_cursorX, (SHORT) g_cursorY};
        SetConsoleCursorPosition(hOut, c);
    }
    SetConsoleCursorInfo(hOut, &ci);
}

void cell(int x, int y, wchar_t &ch, uint16_t &attr)
{
    const CHAR_INFO &c = g_buf[y * W + x];
    ch = c.Char.UnicodeChar;
    attr = c.Attributes;
}

bool waitKey(Key &k, unsigned timeoutMs)
{
    ULONGLONG deadline = GetTickCount64() + timeoutMs;
    for (;;)
    {
        ULONGLONG now = GetTickCount64();
        DWORD left = timeoutMs == INFINITE ? INFINITE : (DWORD) (now >= deadline ? 0 : deadline - now);
        if (WaitForSingleObject(hIn, left) != WAIT_OBJECT_0)
            return false;
        INPUT_RECORD rec;
        DWORD n = 0;
        if (!ReadConsoleInputW(hIn, &rec, 1, &n) || n == 0)
            continue;
        if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown)
        {
            WORD vk = rec.Event.KeyEvent.wVirtualKeyCode;
            if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_CAPITAL)
                continue;
            k.vk = vk;
            k.ch = rec.Event.KeyEvent.uChar.UnicodeChar;
            return true;
        }
        if (rec.EventType == WINDOW_BUFFER_SIZE_EVENT)
            present();
    }
}

Key readKey()
{
    Key k;
    waitKey(k, INFINITE);
    return k;
}

void flushKeys()
{
    FlushConsoleInputBuffer(hIn);
}

void lockClose(bool lock)
{
    // Выделение текста мышью приостанавливает вывод в консоль, а значит и
    // копирование файлов: на время работы оно выключается.
    if (hIn != INVALID_HANDLE_VALUE)
        SetConsoleMode(hIn, ENABLE_EXTENDED_FLAGS | (lock ? 0 : ENABLE_QUICK_EDIT_MODE));
    HWND wnd = GetConsoleWindow();
    if (!wnd)
        return;
    HMENU menu = GetSystemMenu(wnd, FALSE);
    if (menu)
        EnableMenuItem(menu, SC_CLOSE, MF_BYCOMMAND | (lock ? MF_GRAYED : MF_ENABLED));
}

} // namespace scr
