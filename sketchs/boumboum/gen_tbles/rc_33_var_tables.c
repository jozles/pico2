#include <stdio.h>
#include <stdint.h>
#include <math.h>

#define RC_N_SAMPLES 1024
#define RC_N_TABLES  33

// Choix des formes
#define SIN 1
#define TRI 1
#define SAW 0

#define RC_N_WAVES (SIN + TRI + SAW)

// sous MSYS2 MinGW64
// cd /d/data/pio2/sketchs/boumboum/gen_tables
// gcc rc_tables_gen.c -lm -o rc_tables_gen.exe
// ./rc_tables_gen.exe > rc_tables.h


static int16_t gen_sin(int t, int s)
{
    double rise_len = 1.0 + (double)t * (512.0 - 1.0) / 31.0;
    double fall_len = 1024.0 - rise_len;

    double ph = (s < rise_len)
        ? (s / rise_len) * (M_PI / 2.0)
        : (M_PI / 2.0) + ((s - rise_len) / fall_len) * (M_PI / 2.0);

    return (int16_t)lrint(32767.0 * sin(ph));
}

static int16_t gen_tri(int t, int s)
{
    double rise_len = 1.0 + (double)t * (512.0 - 1.0) / 31.0;
    double fall_len = 1024.0 - rise_len;

    double ph = (s < rise_len)
        ? (s / rise_len) * (M_PI / 2.0)
        : (M_PI / 2.0) + ((s - rise_len) / fall_len) * (M_PI / 2.0);

    return (int16_t)lrint(32767.0 * (asin(sin(ph)) * (2.0/M_PI)));
}

static int16_t gen_saw(int t, int s)
{
    double rise_len = 1.0 + (double)t * (512.0 - 1.0) / 31.0;
    double fall_len = 1024.0 - rise_len;

    double ph = (s < rise_len)
        ? (s / rise_len) * (M_PI / 2.0)
        : (M_PI / 2.0) + ((s - rise_len) / fall_len) * (M_PI / 2.0);

    return (int16_t)lrint(32767.0 * (2.0*(ph/M_PI) - 1.0));
}

int main(void)
{
    // En‑tête du .h
    printf("#pragma once\n");
    printf("#include <stdint.h>\n\n");
    printf("#define RC_N_SAMPLES %d\n", RC_N_SAMPLES);
    printf("#define RC_N_TABLES  %d\n", RC_N_TABLES);
    printf("#define SIN %d\n", SIN);
    printf("#define TRI %d\n", TRI);
    printf("#define SAW %d\n", SAW);
    printf("#define RC_N_WAVES  %d\n\n", RC_N_WAVES);

    printf("extern const int16_t rc_tables[RC_N_TABLES][RC_N_SAMPLES][RC_N_WAVES];\n\n");

    // Définition du tableau
    printf("const int16_t rc_tables[RC_N_TABLES][RC_N_SAMPLES][RC_N_WAVES] = {\n");

    // 32 tables normales
    for (int t = 0; t < 32; ++t) {
        printf("  {\n");
        for (int s = 0; s < RC_N_SAMPLES; ++s) {

            int16_t vals[RC_N_WAVES];
            int col = 0;

            if (SIN) vals[col++] = gen_sin(t,s);
            if (TRI) vals[col++] = gen_tri(t,s);
            if (SAW) vals[col++] = gen_saw(t,s);

            printf("    {");
            for (int i = 0; i < RC_N_WAVES; ++i)
                printf("%d%s", vals[i], (i+1<RC_N_WAVES)?", ":"");
            printf("},\n");
        }
        printf("  },\n");
    }

    // Table 32 miroir
    printf("  {\n");
    for (int s = 0; s < RC_N_SAMPLES; ++s) {

        int16_t vals[RC_N_WAVES];
        int col = 0;

        if (SIN) vals[col++] = gen_sin(31, RC_N_SAMPLES-1-s);
        if (TRI) vals[col++] = gen_tri(31, RC_N_SAMPLES-1-s);
        if (SAW) vals[col++] = gen_saw(31, RC_N_SAMPLES-1-s);

        printf("    {");
        for (int i = 0; i < RC_N_WAVES; ++i)
            printf("%d%s", vals[i], (i+1<RC_N_WAVES)?", ":"");
        printf("},\n");
    }
    printf("  }\n");

    printf("};\n");
    return 0;
}
