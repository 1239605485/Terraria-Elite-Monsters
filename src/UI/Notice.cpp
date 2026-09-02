#include "../Core/ModuleRegistry.h"

static bool g_enabled = false;

void em_notice_initialize(const em_game_api_t *api) {
    (void)api;
    g_enabled = em_main_text_available();
}

void em_notice_shutdown(void) { g_enabled = false; }

bool em_notice_enabled(void) { return g_enabled; }

bool em_notice_show(const char *text) {
    if (!g_enabled) return false;
    return em_main_text_show(text);
}
