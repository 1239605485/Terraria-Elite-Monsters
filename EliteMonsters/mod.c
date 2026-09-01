#include "mod_core.h"
#include "mod_logger.h"
#include "elite_core.h"
#include "biome_mutations.h"

void elite_ai_init(void);
void elite_ai_cleanup(void);
void elite_rewards_init(void);
void elite_rewards_cleanup(void);

void (*mod_logger_write)(mod_log_level_t level, const char* tag, const char* fmt, ...) = NULL;

static kernel_mod_info_t g_info = {
    .pkg_id = "eternal.future.elitemonsters",
    .version_code = 2026090201,
    .api_version = 1,
    .version = "1.3.0"
};

static void init_mod(kernel_mod_handle_t* handle) {
    (void)handle;
    elite_core_init();
    biome_mutations_init();
    elite_ai_init();
    elite_rewards_init();
    ELITEMONSTERS_LOG(MOD_LOG_LEVEL_INFO, "Loaded modular biome mutation system");
}

static void cleanup_mod(kernel_mod_handle_t* handle) {
    (void)handle;
    elite_rewards_cleanup();
    elite_ai_cleanup();
    biome_mutations_cleanup();
    elite_core_cleanup();
    ELITEMONSTERS_LOG(MOD_LOG_LEVEL_INFO, "Unloaded");
}

static kernel_mod_info_t* get_info(void) { return &g_info; }

static kernel_mod_ops_t g_ops = {
    .init_mod = init_mod,
    .cleanup_mod = cleanup_mod,
    .get_info = get_info
};

kernel_mod_ops_t* create_kernel_mod(void) { return &g_ops; }
