#include <stdio.h>
#include <stdint.h>
#include <math.h>

//#define M_PI 3.141592636

#define RC_N_TABLES   32
#define RC_N_SAMPLES  1024   // 180°
#define RC_N_VOICES   3

static int16_t gen_value(int t, int s, int v)
{
    // RC = 0 → montée = 1 échantillon
    // RC = 31 → montée = 512 échantillons
    double rise_len = 1.0 + (double)t * (512.0 - 1.0) / 31.0;
    double fall_len = 1024.0 - rise_len;

    double ph;

    if (s < rise_len) {
        // montée compressée : sinus 0→90°
        double u = s / rise_len;       // 0..1
        ph = u * (M_PI / 2.0);         // 0..90°
    } else {
        // descente étirée : sinus 90→180°
        double u = (s - rise_len) / fall_len;  // 0..1
        ph = (M_PI / 2.0) + u * (M_PI / 2.0);  // 90..180°
    }

    // forme d’onde
    double x;
    switch (v) {
        case 0: x = sin(ph); break;                     // sinus
        case 1: x = asin(sin(ph)) * (2.0/M_PI); break;  // triangle
        case 2: x = 2.0*(ph/M_PI) - 1.0; break;         // saw
    }

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
