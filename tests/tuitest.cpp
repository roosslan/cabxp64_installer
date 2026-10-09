// Проверка текстового интерфейса через псевдоконсоль (ConPTY).
//
//   tuitest.exe <сценарий>
//
// Программа запускается в псевдоконсоли 80x25; её вывод (VT-последовательности)
// разбирается в модель экрана, клавиши отправляются VT-последовательностями.
// Команды сценария (файл UTF-8, по одной в строке, '#' — комментарий):
//
//   run <командная строка>         запуск программы
//   expect <текст> [мс]            ждать появления текста на экране (по умолчанию 10 с)
//   absent <текст>                 текста на экране быть не должно
//   key <клавиша> [повторов]       ENTER ESC F1..F10 PGDN PGUP UP DOWN LEFT RIGHT HOME END BS
//                                  DEL TAB, ALT-<буква> или одиночный символ
//   text <строка>                  ввод строки
//   sleep <мс>
//   dump <файл>                    снимок экрана: текст и коды цвета фона
//   exitcode <код> [мс]            ждать завершения программы с указанным кодом
//
// Код возврата: 0 — сценарий выполнен, 1 — ошибка (снимок экрана выводится).

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

const int W = 80, H = 25;

std::wstring widen(const std::string &s)
{
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int) s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int) s.size(), &w[0], n);
    return w;
}

std::string narrow(const std::wstring &w)
{
    if (w.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int) w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int) w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

void print(const std::string &s)
{
    std::wstring w = widen(s);
    DWORD n;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!WriteConsoleW(out, w.c_str(), (DWORD) w.size(), &n, nullptr))
        WriteFile(out, s.data(), (DWORD) s.size(), &n, nullptr);
}

// ---------------------------------------------------------------------------
// Модель экрана
// ---------------------------------------------------------------------------

struct Cell
{
    wchar_t ch = L' ';
    int fg = 7, bg = 0;
};

class Screen
{
public:
    void feed(const std::string &data)
    {
        std::lock_guard<std::mutex> lock(mtx);
        pending += data;
        size_t i = 0;
        while (i < pending.size())
        {
            unsigned char c = (unsigned char) pending[i];
            if (c == 0x1B)
            {
                size_t used = escape(i);
                if (used == 0)
                    break;      // последовательность ещё не пришла целиком
                i += used;
            }
            else if (c == '\r')
                x = 0, ++i;
            else if (c == '\n')
                newline(), ++i;
            else if (c == '\b')
            {
                if (x > 0)
                    --x;
                ++i;
            }
            else if (c < 0x20)
                ++i;
            else
            {
                int len = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
                if (i + len > pending.size())
                    break;
                std::wstring w = widen(pending.substr(i, len));
                for (wchar_t ch : w)
                    put(ch);
                i += len;
            }
        }
        pending.erase(0, i);
    }

    std::string text()
    {
        std::lock_guard<std::mutex> lock(mtx);
        std::string r;
        for (int y = 0; y < H; ++y)
        {
            std::wstring line;
            for (int xx = 0; xx < W; ++xx)
                line += cells[y][xx].ch;
            r += narrow(line) + "\n";
        }
        return r;
    }

    std::string dump()
    {
        std::lock_guard<std::mutex> lock(mtx);
        std::string r;
        for (int y = 0; y < H; ++y)
        {
            std::wstring line;
            std::string bgs;
            for (int xx = 0; xx < W; ++xx)
            {
                line += cells[y][xx].ch;
                bgs += "0123456789abcdef"[cells[y][xx].bg & 15];
            }
            char num[8];
            snprintf(num, sizeof(num), "%02d|", y);
            r += num + narrow(line) + "|\n   " + bgs + "\n";
        }
        return r;
    }

private:
    void newline()
    {
        if (y + 1 < H)
            ++y;
        else
        {
            for (int r = 0; r + 1 < H; ++r)
                for (int c = 0; c < W; ++c)
                    cells[r][c] = cells[r + 1][c];
            for (int c = 0; c < W; ++c)
                cells[H - 1][c] = Cell{L' ', fg, bg};
        }
    }

    void put(wchar_t ch)
    {
        if (x >= W)
        {
            x = 0;
            newline();
        }
        cells[y][x] = Cell{ch, fg, bg};
        ++x;
    }

    void sgr(const std::vector<int> &p)
    {
        if (p.empty())
        {
            fg = 7, bg = 0;
            return;
        }
        for (size_t i = 0; i < p.size(); ++i)
        {
            int v = p[i];
            if (v == 0)
                fg = 7, bg = 0;
            else if (v >= 30 && v <= 37)
                fg = v - 30;
            else if (v >= 90 && v <= 97)
                fg = v - 90 + 8;
            else if (v >= 40 && v <= 47)
                bg = v - 40;
            else if (v >= 100 && v <= 107)
                bg = v - 100 + 8;
            else if (v == 39)
                fg = 7;
            else if (v == 49)
                bg = 0;
            else if ((v == 38 || v == 48) && i + 2 < p.size() && p[i + 1] == 5)
            {
                (v == 38 ? fg : bg) = p[i + 2] & 15;
                i += 2;
            }
            else if ((v == 38 || v == 48) && i + 4 < p.size() && p[i + 1] == 2)
                i += 4;     // truecolor: индекс неизвестен
        }
    }

    // Разбор ESC-последовательности; 0 — нужна ещё порция данных.
    size_t escape(size_t i)
    {
        if (i + 1 >= pending.size())
            return 0;
        char k = pending[i + 1];
        if (k == ']')
        {
            // OSC ... BEL или ESC '\'
            for (size_t j = i + 2; j < pending.size(); ++j)
            {
                if (pending[j] == '\a')
                    return j - i + 1;
                if (pending[j] == 0x1B && j + 1 < pending.size() && pending[j + 1] == '\\')
                    return j - i + 2;
            }
            return 0;
        }
        if (k != '[')
            return 2;
        size_t j = i + 2;
        std::string params;
        while (j < pending.size() && (unsigned char) pending[j] >= 0x20 && (unsigned char) pending[j] < 0x40)
            params += pending[j++];
        if (j >= pending.size())
            return 0;
        char f = pending[j];
        bool priv = !params.empty() && params[0] == '?';
        std::vector<int> p;
        {
            std::string cur;
            for (char c : params)
            {
                if (c == ';')
                {
                    p.push_back(cur.empty() ? -1 : atoi(cur.c_str()));
                    cur.clear();
                }
                else if (c >= '0' && c <= '9')
                    cur += c;
            }
            if (!params.empty())
                p.push_back(cur.empty() ? -1 : atoi(cur.c_str()));
        }
        auto arg = [&](size_t n, int def) { return n < p.size() && p[n] > 0 ? p[n] : def; };
        if (priv)
            return j - i + 1;
        switch (f)
        {
            case 'H':
            case 'f':
                y = std::min(H - 1, arg(0, 1) - 1);
                x = std::min(W - 1, arg(1, 1) - 1);
                break;
            case 'A': y = std::max(0, y - arg(0, 1)); break;
            case 'B': y = std::min(H - 1, y + arg(0, 1)); break;
            case 'C': x = std::min(W - 1, x + arg(0, 1)); break;
            case 'D': x = std::max(0, x - arg(0, 1)); break;
            case 'G': x = std::min(W - 1, arg(0, 1) - 1); break;
            case 'd': y = std::min(H - 1, arg(0, 1) - 1); break;
            case 'X':
                for (int n = 0; n < arg(0, 1) && x + n < W; ++n)
                    cells[y][x + n] = Cell{L' ', fg, bg};
                break;
            case 'K':
            {
                int mode = p.empty() || p[0] < 0 ? 0 : p[0];
                int a = mode == 0 ? x : 0, b = mode == 1 ? x + 1 : W;
                for (int c = a; c < b && c < W; ++c)
                    cells[y][c] = Cell{L' ', fg, bg};
                break;
            }
            case 'J':
            {
                int mode = p.empty() || p[0] < 0 ? 0 : p[0];
                for (int r = 0; r < H; ++r)
                    for (int c = 0; c < W; ++c)
                    {
                        bool clear = mode == 2 || mode == 3 || (mode == 0 && (r > y || (r == y && c >= x))) ||
                                     (mode == 1 && (r < y || (r == y && c <= x)));
                        if (clear)
                            cells[r][c] = Cell{L' ', fg, bg};
                    }
                break;
            }
            case 'm':
            {
                std::vector<int> q;
                for (int v : p)
                    q.push_back(v < 0 ? 0 : v);
                sgr(q);
                break;
            }
        }
        return j - i + 1;
    }

    std::mutex mtx;
    std::string pending;
    Cell cells[H][W];
    int x = 0, y = 0, fg = 7, bg = 0;
};

// ---------------------------------------------------------------------------

std::string keySequence(const std::string &name)
{
    // ALT-<буква>: ESC и буква.
    if (name.size() == 5 && name.compare(0, 4, "ALT-") == 0)
        return std::string("\x1b") + (char) tolower((unsigned char) name[4]);
    static const struct { const char *name, *seq; } keys[] = {
        {"ENTER", "\r"}, {"ESC", "\x1b"}, {"F1", "\x1bOP"}, {"F2", "\x1bOQ"}, {"F3", "\x1bOR"},
        {"F4", "\x1bOS"}, {"F5", "\x1b[15~"}, {"F6", "\x1b[17~"}, {"F7", "\x1b[18~"},
        {"F8", "\x1b[19~"}, {"F9", "\x1b[20~"}, {"F10", "\x1b[21~"},
        {"PGDN", "\x1b[6~"}, {"PGUP", "\x1b[5~"}, {"UP", "\x1b[A"}, {"DOWN", "\x1b[B"},
        {"LEFT", "\x1b[D"}, {"RIGHT", "\x1b[C"}, {"HOME", "\x1b[H"}, {"END", "\x1b[F"},
        {"BS", "\x7f"}, {"DEL", "\x1b[3~"}, {"TAB", "\t"},
    };
    for (const auto &k : keys)
        if (name == k.name)
            return k.seq;
    return name;
}

std::string trim(const std::string &s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Аргумент в кавычках или до первого пробела; остаток — в rest.
std::string takeArg(std::string &rest)
{
    rest = trim(rest);
    std::string a;
    if (!rest.empty() && rest[0] == '"')
    {
        size_t e = rest.find('"', 1);
        a = rest.substr(1, e == std::string::npos ? std::string::npos : e - 1);
        rest = e == std::string::npos ? std::string() : rest.substr(e + 1);
    }
    else
    {
        size_t e = rest.find(' ');
        a = rest.substr(0, e);
        rest = e == std::string::npos ? std::string() : rest.substr(e + 1);
    }
    return a;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        print("tuitest.exe <сценарий>\n");
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f)
    {
        print(std::string("Не удалось открыть сценарий ") + argv[1] + "\n");
        return 2;
    }
    std::string script;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        script.append(buf, n);
    fclose(f);
    if (script.compare(0, 3, "\xEF\xBB\xBF") == 0)
        script.erase(0, 3);

    HANDLE inRead, inWrite, outRead, outWrite;
    CreatePipe(&inRead, &inWrite, nullptr, 0);
    CreatePipe(&outRead, &outWrite, nullptr, 0);
    HPCON pc = nullptr;
    if (FAILED(CreatePseudoConsole(COORD{W, H}, inRead, outWrite, 0, &pc)))
    {
        print("Не удалось создать псевдоконсоль\n");
        return 2;
    }
    // Псевдоконсоль держит свои копии этих концов каналов.
    CloseHandle(inRead);
    CloseHandle(outWrite);

    Screen screen;
    std::atomic<bool> stop(false);
    std::thread reader([&] {
        char rb[8192];
        DWORD got;
        while (!stop && ReadFile(outRead, rb, sizeof(rb), &got, nullptr) && got > 0)
            screen.feed(std::string(rb, got));
    });

    PROCESS_INFORMATION pi = {};
    bool started = false;
    int failures = 0;
    auto fail = [&](const std::string &msg) {
        print("ОШИБКА: " + msg + "\n" + screen.dump());
        ++failures;
    };
    auto send = [&](const std::string &s) {
        DWORD w;
        WriteFile(inWrite, s.data(), (DWORD) s.size(), &w, nullptr);
    };

    size_t pos = 0;
    int lineNo = 0;
    while (pos < script.size() && failures == 0)
    {
        size_t nl = script.find('\n', pos);
        std::string line = trim(script.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
        pos = nl == std::string::npos ? script.size() : nl + 1;
        ++lineNo;
        if (line.empty() || line[0] == '#')
            continue;
        std::string rest = line;
        std::string cmd = takeArg(rest);
        print("> " + line + "\n");

        if (cmd == "run")
        {
            std::wstring cl = widen(trim(rest));
            STARTUPINFOEXW si = {};
            si.StartupInfo.cb = sizeof(si);
            // Пустые стандартные дескрипторы: программа работает с консолью
            // напрямую (CONIN$/CONOUT$).
            si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
            SIZE_T size = 0;
            InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
            std::vector<char> attrs(size);
            si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST) attrs.data();
            InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &size);
            UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, pc,
                                      sizeof(pc), nullptr, nullptr);
            if (!CreateProcessW(nullptr, &cl[0], nullptr, nullptr, FALSE, EXTENDED_STARTUPINFO_PRESENT,
                                nullptr, nullptr, &si.StartupInfo, &pi))
                fail("не удалось запустить: " + narrow(cl));
            started = true;
        }
        else if (cmd == "expect" || cmd == "absent")
        {
            std::string what = takeArg(rest);
            int timeout = cmd == "absent" ? 0 : (trim(rest).empty() ? 10000 : atoi(trim(rest).c_str()));
            auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
            bool found = false;
            for (;;)
            {
                found = screen.text().find(what) != std::string::npos;
                if (found || std::chrono::steady_clock::now() >= until)
                    break;
                Sleep(50);
            }
            if (cmd == "expect" && !found)
                fail("строка " + std::to_string(lineNo) + ": не найден текст «" + what + "»");
            if (cmd == "absent" && found)
                fail("строка " + std::to_string(lineNo) + ": найден текст «" + what + "»");
        }
        else if (cmd == "key")
        {
            std::string k = takeArg(rest);
            int times = trim(rest).empty() ? 1 : atoi(trim(rest).c_str());
            for (int i = 0; i < times; ++i)
            {
                send(keySequence(k));
                Sleep(80);
            }
        }
        else if (cmd == "text")
            send(trim(rest));
        else if (cmd == "sleep")
            Sleep(atoi(trim(rest).c_str()));
        else if (cmd == "dump")
        {
            std::string file = takeArg(rest);
            FILE *o = _wfopen(widen(file).c_str(), L"wb");
            if (o)
            {
                std::string d = screen.dump();
                fwrite(d.data(), 1, d.size(), o);
                fclose(o);
            }
        }
        else if (cmd == "exitcode")
        {
            int want = atoi(takeArg(rest).c_str());
            int timeout = trim(rest).empty() ? 20000 : atoi(trim(rest).c_str());
            if (!started || WaitForSingleObject(pi.hProcess, timeout) != WAIT_OBJECT_0)
                fail("программа не завершилась");
            else
            {
                DWORD code = 0;
                GetExitCodeProcess(pi.hProcess, &code);
                if ((int) code != want)
                    fail("код завершения " + std::to_string(code) + ", ожидался " + std::to_string(want));
            }
        }
        else
            fail("строка " + std::to_string(lineNo) + ": неизвестная команда " + cmd);
    }

    if (started)
    {
        if (WaitForSingleObject(pi.hProcess, 0) == WAIT_TIMEOUT)
            TerminateProcess(pi.hProcess, 99);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    // ClosePseudoConsole ждёт, пока вывод будет прочитан: поток чтения
    // останавливается после неё.
    ClosePseudoConsole(pc);
    stop = true;
    CloseHandle(inWrite);
    reader.join();
    CloseHandle(outRead);
    print(failures ? "Сценарий не выполнен\n" : "Сценарий выполнен\n");
    return failures ? 1 : 0;
}
