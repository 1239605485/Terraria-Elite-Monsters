#include "fw.h"

#include "mod_core.h"
#include "mod_logger.h"

#include <stdio.h>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

/* The loader fills this exported function-pointer variable when the module is
 * registered.  Keep the symbol visible: without it, the pointer remains NULL
 * and none of the module diagnostics reach the TEFKernel log stream. */
__attribute__((visibility("default"))) void (*mod_logger_write)(
    mod_log_level_t level, const char *tag, const char *fmt, ...) = NULL;

static kernel_mod_info_t g_mod_info = {
    .pkg_id = "li06.originrewrite",
    .version_code = 2026090422,
    .api_version = 1,
    .version = "1.0.21-visual-fix"
};

static void fw_init_mod(kernel_mod_handle_t *handle) {
    bool ready;
    char log_path[512];
    static const char *export_log_path =
        "/storage/emulated/0/Android/data/eternal.future.tefmanager/"
        "files/logs/tefkernel/runtime_originrewrite.log";
    log_path[0] = '\0';
    if (handle && handle->private_dir) {
        (void)snprintf(log_path, sizeof(log_path), "%s/%s",
                       handle->private_dir, "originrewrite_runtime.log");
        fw_core_set_log_file(log_path);
        {
            FILE *probe = fopen(log_path, "a");
            if (probe) {
                fputs("[MODULE_BEACON] version=1.0.21-visual-fix "
                      "versionCode=2026090422\n", probe);
                fflush(probe);
                fclose(probe);
            }
        }
    }
    fw_core_add_log_file(export_log_path);
    {
        FILE *probe = fopen(export_log_path, "a");
        if (probe) {
            fputs("[MODULE_BEACON] version=1.0.21-visual-fix "
                  "versionCode=2026090422\n", probe);
            fflush(probe);
            fclose(probe);
        }
    }
#if defined(__ANDROID__)
    /* This beacon is independent of the optional TEF module logger.  It lets
     * logcat prove which binary was loaded before any runtime probe runs. */
    __android_log_print(ANDROID_LOG_INFO, "OriginRewrite",
                        "[MODULE_BEACON] version=1.0.21-visual-fix "
                        "versionCode=2026090422");
#endif
    ready = fw_core_init();
    if (mod_logger_write) {
        mod_logger_write(MOD_LOG_LEVEL_INFO, "OriginRewrite",
                         "[MODULE_BEACON] version=1.0.21-visual-fix "
                         "versionCode=2026090422 initialized; "
                         "gameplay gate=%s",
                         ready ? "on" : "off");
    }
}

static void fw_cleanup_mod(kernel_mod_handle_t *handle) {
    (void)handle;
    fw_core_shutdown();
    fw_core_set_log_file(NULL);
    fw_core_add_log_file(NULL);
}

static kernel_mod_info_t *fw_get_info(void) {
    return &g_mod_info;
}

static kernel_mod_ops_t g_mod_ops = {
    .init_mod = fw_init_mod,
    .cleanup_mod = fw_cleanup_mod,
    .get_info = fw_get_info
};

kernel_mod_ops_t *create_kernel_mod(void) {
    return &g_mod_ops;
}
