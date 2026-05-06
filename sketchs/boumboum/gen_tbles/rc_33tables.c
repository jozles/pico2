#include <stdio.h>
#include <stdint.h>
#include <math.h>

#define RC_N_TABLES   33
#define RC_N_SAMPLES  1024
#define RC_N_VOICES   3

// Active/désactive les formes d’onde (génération uniquement)
#define SIN 1
#define TRI 1
#define SAW 1

static int16_t gen_sin(int t, int s)
{
    double rise_len = 1.0 + (double)t * (512.0 - 1.0) / 31.0;
    double fall_len = 1024.0 - rise_len;

    double ph;
    if (s < rise_len) ph = (s / rise_len) * (M_PI / 2.0);
    else ph = (M_PI / 2.0) + ((s - rise_len) / fall_len) * (M_PI / 2.0);

    return (int16_t)lrint(32767.0 * sin(ph));
}

static int16_t gen_tri(int t, int s)
{
    double rise_len = 1.0 + (double)t * (512.0 - 1.0) / 31.0;
    double fall_len = 1024.0 - rise_len;

    double ph;
    if (s < rise_len) ph = (s / rise_len) * (M_PI / 2.0);
    else ph = (M_PI / 2.0) + ((s - rise_len) / fall_len) * (M_PI / 2.0);

    return (int16_t)lrint(32767.0 * (asin(sin(ph)) * (2.0/M_PI)));
}

static int16_t gen_saw(int t, int s)
{
    double rise_len = 1.0 + (double)t * (512.0 - 1.0) / 31.0;
    double fall_len = 1024.0 - rise_len;

    double ph;
    if (s < rise_len) ph = (s / rise_len) * (M_PI / 2.0);
    else ph = (M_PI / 2.0) + ((s - rise_len) / fall_len) * (M_PI / 2.0);

    return (int16_t)lrint(32767.0 * (2.0*(ph/M_PI) - 1.0));
}

int main(void)
{
    printf("#pragma once\n#include <stdint.h>\n\n");
    printf("const int16_t rc_tables[%d][%d][%d] = {\n", RC_N_TABLES, RC_N_SAMPLES, RC_N_VOICES);

    // 32 tables classiques
    for (int t = 0; t < 32; ++t) {
        printf("  {\n");
        for (int s = 0; s < RC_N_SAMPLES; ++s) {
            int16_t sinv = SIN ? gen_sin(t,s) : 0;
            int16_t triv = TRI ? gen_tri(t,s) : 0;
            int16_t sawv = SAW ? gen_saw(t,s) : 0;
            printf("    {%d, %d, %d},\n", sinv, triv, sawv);
        }
        printf("  },\n");
    }

    // Table 32 miroir
    printf("  {\n");
    for (int s = 0; s < RC_N_SAMPLES; ++s) {
        int16_t sinv = SIN ? gen_sin(31, RC_N_SAMPLES-1-s) : 0;
        int16_t triv = TRI ? gen_tri(31, RC_N_SAMPLES-1-s) : 0;
        int16_t sawv = SAW ? gen_saw(31, RC_N_SAMPLES-1-s) : 0;
        printf("    {%d, %d, %d},\n", sinv, triv, sawv);
    }
    printf("  }\n");

    printf("};\n");
    return 0;
}
