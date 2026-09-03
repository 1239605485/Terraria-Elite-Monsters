#include "fw.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef UINT64_MAX
#define UINT64_MAX UINT64_C(0xFFFFFFFFFFFFFFFF)
#endif

static const float g_mode_chance[FW_MODE_COUNT] = {
    0.20f, /* classic */
    0.30f, /* expert */
    0.40f, /* master */
    0.50f, /* zenith */
    0.20f  /* journey */
};

static const float g_mode_life[FW_MODE_COUNT] = {
    1.00f, 1.15f, 1.35f, 1.60f, 1.00f
};

static const float g_mode_damage[FW_MODE_COUNT] = {
    1.00f, 1.10f, 1.25f, 1.45f, 1.00f
};

static const float g_mode_defense[FW_MODE_COUNT] = {
    1.00f, 1.05f, 1.10f, 1.15f, 1.00f
};

static const float g_mode_knockback_reduction[FW_MODE_COUNT] = {
    0.00f, 0.10f, 0.20f, 0.30f, 0.00f
};

static const float g_journey_money_extra = 1.00f;

static const float g_pre_weights[FW_MODE_COUNT][3] = {
    {0.75f, 0.25f, 0.00f},
    {0.75f, 0.25f, 0.00f},
    {0.75f, 0.25f, 0.00f},
    {0.75f, 0.25f, 0.00f},
    {0.75f, 0.25f, 0.00f}
};

static const float g_post_weights[FW_MODE_COUNT][3] = {
    {0.75f, 0.20f, 0.05f},
    {0.65f, 0.25f, 0.10f},
    {0.55f, 0.30f, 0.15f},
    {0.45f, 0.35f, 0.20f},
    {0.75f, 0.20f, 0.05f}
};

static const float g_stage_life[FW_PROGRESS_COUNT][3] = {
    {1.25f, 1.65f, 0.00f},
    {1.45f, 2.10f, 3.20f},
    {1.70f, 2.70f, 4.20f},
    {2.00f, 3.40f, 5.50f},
    {2.40f, 4.40f, 7.00f}
};

static const float g_stage_damage[FW_PROGRESS_COUNT][3] = {
    {1.10f, 1.30f, 0.00f},
    {1.25f, 1.60f, 2.00f},
    {1.45f, 2.00f, 2.50f},
    {1.70f, 2.50f, 3.10f},
    {2.00f, 3.10f, 3.80f}
};

static const int32_t g_stage_defense_flat[FW_PROGRESS_COUNT][3] = {
    {2, 4, 0},
    {4, 7, 12},
    {6, 10, 16},
    {8, 14, 22},
    {10, 18, 30}
};

static const float g_stage_money[FW_PROGRESS_COUNT] = {
    1.00f, 1.15f, 1.35f, 1.60f, 2.00f
};

static const float g_tier_defense[3] = {1.10f, 1.20f, 1.30f};
static const float g_tier_money[3] = {1.50f, 3.00f, 6.00f};
static const float g_tier_knockback[3] = {0.20f, 0.40f, 0.60f};
static const float g_tier_scale[3] = {1.05f, 1.10f, 1.15f};
static const float g_tier_slots[3] = {1.15f, 1.35f, 1.75f};

static fw_game_mode fw_effective_mode(fw_game_mode mode) {
    return mode == FW_MODE_JOURNEY ? FW_MODE_CLASSIC : mode;
}

static uint64_t fw_next_random(uint64_t *state) {
    uint64_t x = *state;
    if (x == 0u) x = UINT64_C(0x9E3779B97F4A7C15);
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

const char *fw_tier_name(fw_tier tier) {
    switch (tier) {
        case FW_TIER_ALTERED: return "altered";
        case FW_TIER_CALAMITY: return "calamity";
        case FW_TIER_APOCALYPSE: return "apocalypse";
        default: return "none";
    }
}

const char *fw_mode_name(fw_game_mode mode) {
    static const char *names[FW_MODE_COUNT] = {
        "classic", "expert", "master", "zenith", "journey"
    };
    return mode >= FW_MODE_CLASSIC && mode < FW_MODE_COUNT
        ? names[mode] : "unknown";
}

const char *fw_progress_name(fw_progress progress) {
    static const char *names[FW_PROGRESS_COUNT] = {
        "pre_hardmode", "hardmode_pre_mech", "pre_plantera",
        "post_plantera", "endgame"
    };
    return progress >= FW_PROGRESS_PRE_HARDMODE && progress < FW_PROGRESS_COUNT
        ? names[progress] : "unknown";
}

fw_roll_outcome fw_roll_elite(fw_game_mode mode, fw_progress progress,
                              uint64_t seed, fw_tier *out_tier) {
    const float (*weights)[3];
    fw_game_mode effective;
    uint64_t random;
    double total;
    double pick;
    int tier_index;

    if (mode < FW_MODE_CLASSIC || mode >= FW_MODE_COUNT ||
        progress < FW_PROGRESS_PRE_HARDMODE || progress >= FW_PROGRESS_COUNT ||
        !out_tier) {
        return FW_ROLL_TIER_DISABLED;
    }

    *out_tier = FW_TIER_NONE;
    random = fw_next_random(&seed);
    if ((double)random > (double)UINT64_MAX * g_mode_chance[mode]) {
        return FW_ROLL_NOT_ELITE;
    }

    weights = progress == FW_PROGRESS_PRE_HARDMODE
        ? g_pre_weights : g_post_weights;
    effective = fw_effective_mode(mode);
    total = (double)weights[effective][0] +
            (double)weights[effective][1] +
            (double)weights[effective][2];
    if (total <= 0.0) return FW_ROLL_TIER_DISABLED;

    random = fw_next_random(&seed);
    pick = ((double)random / (double)UINT64_MAX) * total;
    tier_index = -1;
    if (pick < (double)weights[effective][0]) {
        tier_index = FW_TIER_ALTERED;
    } else {
        pick -= (double)weights[effective][0];
        if (pick < (double)weights[effective][1]) {
            tier_index = FW_TIER_CALAMITY;
        } else {
            pick -= (double)weights[effective][1];
            if (pick < (double)weights[effective][2]) {
                tier_index = FW_TIER_APOCALYPSE;
            }
        }
    }

    if (tier_index < FW_TIER_ALTERED || tier_index > FW_TIER_APOCALYPSE ||
        weights[effective][tier_index - 1] <= 0.0f) {
        return FW_ROLL_TIER_DISABLED;
    }
    *out_tier = (fw_tier)tier_index;
    return FW_ROLL_ELITE;
}

static int32_t fw_clamp_i32(int64_t value) {
    if (value > INT32_MAX) return INT32_MAX;
    if (value < 0) return 0;
    return (int32_t)value;
}

static int32_t fw_round_f32(float value) {
    if (!isfinite(value)) return 0;
    if (value < 0.0f) return 0;
    return fw_clamp_i32((int64_t)llroundf(value));
}

bool fw_stats_build(const fw_spawn_input *input, fw_tier tier,
                    fw_final_stats *out_stats) {
    fw_game_mode effective;
    int idx;
    float life_mult;
    float damage_mult;
    float current_ratio;

    if (!input || !out_stats || tier < FW_TIER_ALTERED ||
        tier > FW_TIER_APOCALYPSE ||
        input->mode < FW_MODE_CLASSIC || input->mode >= FW_MODE_COUNT ||
        input->progress < FW_PROGRESS_PRE_HARDMODE ||
        input->progress >= FW_PROGRESS_COUNT) {
        return false;
    }

    memset(out_stats, 0, sizeof(*out_stats));
    idx = (int)tier - 1;
    effective = fw_effective_mode(input->mode);

    life_mult = g_stage_life[input->progress][idx] *
                g_mode_life[effective];
    damage_mult = g_stage_damage[input->progress][idx] *
                  g_mode_damage[effective];

    out_stats->life_max = fw_round_f32((float)input->vanilla.life_max *
                                       life_mult);
    if (input->vanilla.life_max > 0) {
        current_ratio = (float)input->vanilla.life /
                        (float)input->vanilla.life_max;
        if (!isfinite(current_ratio) || current_ratio < 0.0f) {
            current_ratio = 1.0f;
        }
        if (current_ratio > 1.0f) current_ratio = 1.0f;
        out_stats->life = fw_round_f32((float)out_stats->life_max *
                                       current_ratio);
    } else {
        out_stats->life = out_stats->life_max;
    }

    out_stats->damage = fw_round_f32((float)input->vanilla.damage *
                                     damage_mult);
    out_stats->defense = fw_round_f32(
        (float)input->vanilla.defense *
        g_tier_defense[idx] * g_mode_defense[effective]) +
        g_stage_defense_flat[input->progress][idx];

    out_stats->knockback_resist =
        input->vanilla.knockback_resist *
        (1.0f - g_tier_knockback[idx]) *
        (1.0f - g_mode_knockback_reduction[effective]);
    if (out_stats->knockback_resist < 0.0f) {
        out_stats->knockback_resist = 0.0f;
    }

    out_stats->scale = input->vanilla.scale * g_tier_scale[idx];
    out_stats->npc_slots = input->vanilla.npc_slots * g_tier_slots[idx];
    out_stats->money = input->vanilla.money *
                       g_tier_money[idx] *
                       g_stage_money[input->progress] *
                       g_journey_money_extra;
    return true;
}
