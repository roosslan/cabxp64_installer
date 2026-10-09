#pragma once

#include <string>
#include <vector>

#define Uses_TScroller
#include <tvision/tv.h>

// Прокручиваемый многострочный текст (строки в UTF-8).
class TTextScroller : public TScroller
{
public:
    TTextScroller(const TRect &bounds, TScrollBar *h, TScrollBar *v);

    void setText(const std::string &text);
    void draw() override;

private:
    std::vector<std::string> lines;
};

// Окно с прокручиваемым текстом и кнопкой OK.
void scrollTextDialog(const char *title, const std::string &text);
