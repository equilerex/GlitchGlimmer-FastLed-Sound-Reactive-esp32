#pragma once

#include <vector>
#include <TFT_eSPI.h>
#include "widgets/Widget.h" 

class GridLayout {
private:
    int _width, _height;
    static constexpr size_t MAX_WIDGETS = 16;
    // Non-owning: widgets are owned by whichever class created them.
    std::vector<Widget*> widgets;

public:
    GridLayout(int screenWidth, int screenHeight)
        : _width(screenWidth), _height(screenHeight) {
        clear();
    }

    void clear() {
        widgets.clear();
    }

    void addWidget(Widget* widget) {
        if (widgets.size() < MAX_WIDGETS && widget != nullptr) {
            widgets.push_back(widget);
        }
    }

    void draw(TFT_eSPI& tft) {
        int x = 0, y = 0;
        int rowHeight = 0;
        const int margin = 2;

        // Every widget repaints its own rect opaquely, so no screen clear is
        // needed here. Draw all of them each frame; a partial pass leaves the
        // rest of the screen stale and reads as flicker.
        for (size_t i = 0; i < widgets.size(); i++) {
            Widget* widget = widgets[i];
            if (!widget) continue;

            int w = widget->getMinWidth();
            int h = widget->getMinHeight();

            // Start a new row if this widget won't fit
            if (x + w > _width) {
                x = 0;
                y += rowHeight + margin;
                rowHeight = 0;
            }

            widget->draw(tft, x, y, w, h);

            x += w + margin;
            if (h > rowHeight) rowHeight = h;

            // Start a new row if we've placed 4 widgets in this row
            if (i % 4 == 3) {
                x = 0;
                y += rowHeight + margin;
                rowHeight = 0;
            }
        }
    }

    void drawVerticalStack(TFT_eSPI& tft) {
        int y = 0;
        int widgetWidth = _width;
        const int margin = 2;

        for (size_t i = 0; i < widgets.size(); ++i) {
            Widget* widget = widgets[i];
            if (!widget) continue;

            int widgetHeight = widget->getMinHeight();
            widget->draw(tft, 0, y, widgetWidth, widgetHeight);
            y += widgetHeight + margin;
        }
    }

    void update(TFT_eSPI& tft) {
        draw(tft);
    }
};
