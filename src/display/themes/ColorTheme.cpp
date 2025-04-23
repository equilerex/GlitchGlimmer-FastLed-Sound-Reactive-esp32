#include "ColorTheme.h"

const WidgetColorTheme& getTheme() {
    if (THEME == "BLADERUNNER") {
        return BladeRunnerTheme;
    } else if (THEME == "CYBERPUNK") {
        return CyberpunkTheme;
    } else {
        return CyberpunkTheme;
    }
}
