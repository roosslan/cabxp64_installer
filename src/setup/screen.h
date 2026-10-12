#pragma once

#include <cstdint>
#include <string>

// Текстовый экран 80x25 в стиле программы установки Windows XP.
// Рисование идёт в буфер в памяти; present() выводит его в консоль целиком.
// Все строки в UTF-8.
namespace scr {

const int W = 80;
const int H = 25;

// Атрибуты (цвет текста | цвет фона << 4) в палитре VGA.
enum : uint16_t
{
    Text      = 0x17,   // светло-серый на синем — основной текст
    Bright    = 0x1F,   // белый на синем — заголовки, рамки
    Status    = 0x70,   // чёрный на сером — строка состояния
    Yellow    = 0xE0,   // заливка индикатора копирования (жёлтый фон)
    Red       = 0xC0,   // заливка индикатора обратного отсчёта (красный фон)
    Blank     = 0x11,   // пустая часть индикатора
    Input     = 0x1F,   // поле ввода
    Black     = 0x07,   // светло-серый на чёрном — экран проверки оборудования
};

bool init(const std::string &title);
void restore();
bool active();

void clear(uint16_t attr = Text);
void put(int x, int y, uint16_t attr, const std::string &text);
void fill(int x, int y, int len, uint16_t attr, wchar_t ch = L' ');
void center(int y, uint16_t attr, const std::string &text);
void box(int x1, int y1, int x2, int y2, uint16_t attr, bool dbl = false);
// Заголовок в строке 1 с двойным подчёркиванием в строке 2.
void header(const std::string &title);
// Серая строка состояния во всю ширину; правая часть отделяется '│'.
void status(const std::string &left, const std::string &right = std::string());
// Абзац с переносом по словам. Возвращает число занятых строк.
int para(int x, int y, int width, uint16_t attr, const std::string &text);
int textWidth(const std::string &text);         // число символов UTF-8
void showCursor(int x, int y);                  // x < 0 — скрыть
void present();
// Ячейка буфера: символ и атрибут (для предпросмотра и проверок).
void cell(int x, int y, wchar_t &ch, uint16_t &attr);

// Клавиатура: виртуальный код клавиши и введённый символ.
struct Key
{
    uint16_t vk = 0;
    wchar_t ch = 0;
};

Key readKey();
// Ожидание клавиши не дольше timeoutMs; false — время вышло.
bool waitKey(Key &k, unsigned timeoutMs);
void flushKeys();

// Запрет закрытия окна и Ctrl+C на время копирования файлов.
void lockClose(bool lock);

} // namespace scr
