#pragma once

#include <vector>
#include <memory>
#include <TFT_eSPI.h>
#include "widgets/Widget.h" 

class GridLayout {
private:
    int _width, _height;
    static constexpr size_t MAX_WIDGETS = 16;
    std::vector<std::unique_ptr<Widget>> widgets;

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
            widgets.push_back(std::unique_ptr<Widget>(widget));
        }
    }

    void draw(TFT_eSPI& tft) {
        static unsigned long lastFullRedraw = 0;
        static size_t lastDrawnWidget = 0;
        unsigned long now = millis();

        // Only redraw background every 500ms
        if (now - lastFullRedraw > 500) {
            tft.fillScreen(TFT_BLACK);
            lastFullRedraw = now;
            lastDrawnWidget = 0;
        }

        // Calculate widget positions
        int x = 0, y = 0;
        int rowHeight = 0;
        int margin = 2;
        
        // First pass: calculate positions for all widgets
        struct WidgetPosition {
            int x, y, width, height;
        };
        std::vector<WidgetPosition> positions;
        positions.reserve(widgets.size());
        
        for (size_t i = 0; i < widgets.size(); i++) {
            Widget* widget = widgets[i].get();
            if (!widget) continue;
            
            int w = widget->getMinWidth();
            int h = widget->getMinHeight();
            
            // Start a new row if this widget won't fit
            if (x + w > _width) {
                x = 0;
                y += rowHeight + margin;
                rowHeight = 0;
            }
            
            positions.push_back({x, y, w, h});
            
            x += w + margin;
            if (h > rowHeight) rowHeight = h;
            
            // Start a new row if we've placed 4 widgets in this row
            if (i % 4 == 3) {
                x = 0;
                y += rowHeight + margin;
                rowHeight = 0;
            }
        }
        
        // Draw a maximum of 4 widgets per frame
        size_t widgetsProcessed = 0;
        while (lastDrawnWidget < widgets.size() && widgetsProcessed < 4) {
            Widget* widget = widgets[lastDrawnWidget].get();
            if (widget && lastDrawnWidget < positions.size()) { 
                WidgetPosition pos = positions[lastDrawnWidget];
                widget->draw(tft, pos.x, pos.y, pos.width, pos.height);
                yield(); // Allow other tasks to run
            }
            lastDrawnWidget++;
            widgetsProcessed++;
        }
    
        // Reset widget counter when all widgets are drawn
        if (lastDrawnWidget >= widgets.size()) {
            lastDrawnWidget = 0;
        }
    }

    void drawVerticalStack(TFT_eSPI& tft) {
        int y = 0;
        int widgetWidth = _width;
        const int margin = 2;

        for (size_t i = 0; i < widgets.size(); ++i) {
            Widget* widget = widgets[i].get();
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
