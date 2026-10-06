#include <cstdio>
#include <cmath>
#include <cstdint>
#include <cstdlib>

static const int FILTER_TANH_LEN  = 2048;
static const int FILTER_TANH_HALF = FILTER_TANH_LEN / 2;
static const float FILTER_KNEE    = 1.0f;   // adapte si nécessaire

// Calcul d'une entrée de la table (même algo que ton fillFilterTanH)
static int32_t compute_entry(int i)
{
    float q = (float)(i - FILTER_TANH_HALF) / 256.0f;
    float a = fabsf(q);
    float d;

    if (a < 0.25f) {
        float q2 = a * a;
        d = a * q2 * (1.0f/3.0f
            - q2 * (2.0f/15.0f
            - q2 * (17.0f/315.0f
            - q2 * (62.0f/2835.0f
            - q2 * (1382.0f/155925.0f)))));
    } else {
        d = a - tanhf(a);
    }

    float dq8 = d * (FILTER_KNEE * 256.0f * 2.0f);
    return (int32_t)lroundf(q < 0 ? -dq8 : dq8);
}

int main()
{
    FILE* f = fopen("filterDTable.cpp", "w");
    if (!f) {
        printf("Impossible d'écrire filterDTable.cpp\n");
        return 1;
    }

    fprintf(f, "#include <stdint.h>\n\n");
    fprintf(f, "__attribute__((section(\".ram_d1\")))\n");
    fprintf(f, "const int32_t filterDTable[%d] = {\n", FILTER_TANH_LEN);

    for (int i = 0; i < FILTER_TANH_LEN; i++) {
        int32_t val = compute_entry(i);
        fprintf(f, "    %d,\n", val);
    }

    fprintf(f, "};\n");
    fclose(f);

    printf("filterDTable.cpp généré avec succès.\n");
    return 0;
}
