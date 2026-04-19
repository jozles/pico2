#include <stdio.h>
#include <stdint.h>
#include <math.h>

//#define M_PI 3.141592636

#define RC_N_TABLES   32
#define RC_N_SAMPLES  1024   // 180°
#define RC_N_VOICES   3

static int16_t gen_value(int t, int s, int v) {
    double phase_table  = 2.0 * M_PI * t / RC_N_TABLES;
    double phase_sample = M_PI * s / RC_N_SAMPLES;      // 0..180°
    double phase_voice  = 2.0 * M_PI * v / RC_N_VOICES;

    double x = sin(phase_table + phase_sample + phase_voice);
    return (int16_t)lrint(32767.0 * x);
}

int main(void) {
    printf("#pragma once\n#include <stdint.h>\n\n");
    printf("#define RC_N_TABLES %d\n", RC_N_TABLES);
    printf("#define RC_N_SAMPLES %d\n", RC_N_SAMPLES);
    printf("#define RC_N_VOICES %d\n\n", RC_N_VOICES);

    printf("const int16_t rc_tables[RC_N_TABLES][RC_N_SAMPLES][RC_N_VOICES] = {\n");

    for (int t = 0; t < RC_N_TABLES; ++t) {
        printf("  {\n");
        for (int s = 0; s < RC_N_SAMPLES; ++s) {
            printf("    {");
            for (int v = 0; v < RC_N_VOICES; ++v) {
                printf("%d%s", gen_value(t, s, v), (v+1<RC_N_VOICES)?", ":"");
            }
            printf("},\n");
        }
        printf("  },\n");
    }

    printf("};\n");
    return 0;
}
