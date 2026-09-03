#ifndef ORIGINREWRITE_FW_H
#define ORIGINREWRITE_FW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FW_MAX_BINDINGS 1024u
#define FW_SETDEFAULTS_LIMIT 8u
#define FW_NAME_GETTER_LIMIT 4u
#define FW_INACTIVE_GRACE_TICKS 4u

typedef enum fw_tier {
    FW_TIER_NONE = 0,
    FW_TIER_ALTERED = 1,   /* 异化种（绿色） */
    FW_TIER_CALAMITY = 2,  /* 灾变种（蓝色） */
    FW_TIER_APOCALYPSE = 3 /* 终焉种（红色） */
} fw_tier;

typedef enum fw_game_mode {
    FW_MODE_CLASSIC = 0,
    FW_MODE_EXPERT = 1,
    FW_MODE_MASTER = 2,
    FW_MODE_ZENITH = 3,
    FW_MODE_JOURNEY = 4,
    FW_MODE_COUNT = 5
} fw_game_mode;

typedef enum fw_progress {
    FW_PROGRESS_PRE_HARDMODE = 0,
    FW_PROGRESS_HARDMODE_PRE_MECH = 1,
    FW_PROGRESS_PRE_PLANTERA = 2,
    FW_PROGRESS_POST_PLANTERA = 3,
    FW_PROGRESS_ENDGAME = 4,
    FW_PROGRESS_COUNT = 5
} fw_progress;

typedef struct fw_vanilla_stats {
    int32_t life_max;
    int32_t life;
    int32_t damage;
    int32_t defense;
    float knockback_resist;
    float scale;
    float npc_slots;
    float money;
} fw_vanilla_stats;

typedef struct fw_final_stats {
    int32_t life_max;
    int32_t life;
    int32_t damage;
    int32_t defense;
    float knockback_resist;
    float scale;
    float npc_slots;
    float money;
} fw_final_stats;

typedef struct fw_spawn_input {
    fw_game_mode mode;
    fw_progress progress;
    fw_vanilla_stats vanilla;
} fw_spawn_input;

typedef struct fw_spawn_result {
    bool committed;
    fw_tier tier;
    fw_final_stats final_stats;
    const char *reject_reason;
} fw_spawn_result;

typedef enum fw_roll_outcome {
    FW_ROLL_NOT_ELITE = 0,
    FW_ROLL_ELITE = 1,
    FW_ROLL_TIER_DISABLED = 2
} fw_roll_outcome;

/* 概率和属性按 v0.5 文档取值，先用内建默认，后续再迁移到配置。 */
const char *fw_tier_name(fw_tier tier);
const char *fw_mode_name(fw_game_mode mode);
const char *fw_progress_name(fw_progress progress);

/* 用一次性随机决定“是否精英”和“精英档位”；不会连续做三次抽奖。 */
fw_roll_outcome fw_roll_elite(fw_game_mode mode, fw_progress progress,
                              uint64_t seed, fw_tier *out_tier);

/* 从原版最终基准值计算一次最终属性；调用方保证只执行一次。 */
bool fw_stats_build(const fw_spawn_input *input, fw_tier tier,
                    fw_final_stats *out_stats);

/* 框架生命周期入口（由 mod.c 调用）。 */
void fw_core_set_log_file(const char *path);
void fw_core_add_log_file(const char *path);
bool fw_core_init(void);
void fw_core_shutdown(void);
bool fw_core_gameplay_ready(void);

#endif
