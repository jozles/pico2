#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define W 12
#define H 12

unsigned char ttf_buffer[1<<20];
unsigned char bitmap[W * H];

int main(void)
{
    FILE *f = fopen("DejaVuSansMono-Bold.ttf", "rb");
    if (!f) { printf("TTF introuvable\n"); return 1; }

    fread(ttf_buffer, 1, 1<<20, f);
    fclose(f);

    stbtt_fontinfo font;
    stbtt_InitFont(&font, ttf_buffer, stbtt_GetFontOffsetForIndex(ttf_buffer, 0));

    FILE *hout = fopen("font12x12.h", "w");
    FILE *cout = fopen("font12x12.c", "w");

    fprintf(hout,
        "#ifndef FONT12X12_H\n"
        "#define FONT12X12_H\n\n"
        "#include <stdint.h>\n\n"
        "#define FONT12X12_WIDTH 12\n"
        "#define FONT12X12_HEIGHT 12\n\n"
        "extern const uint16_t font12x12[128][12];\n\n"
        "#endif\n"
    );

    fprintf(cout, "#include \"font12x12.h\"\n\n");
    fprintf(cout, "const uint16_t font12x12[128][12] = {\n");

    for (int c = 0; c < 128; c++) {

        fprintf(cout, "  [%d] = {\n", c);

        for (int row = 0; row < H; row++) {
            for (int col = 0; col < W; col++)
                bitmap[row*W + col] = 0;

        }

        stbtt_MakeCodepointBitmap(&font,
            bitmap, W, H, W,
            12.0f, 12.0f, c);

        for (int y = 0; y < H; y++) {
            uint16_t line = 0;
            for (int x = 0; x < W; x++) {
                int v = bitmap[y*W + x] > 128;
                line |= (v << (W-1-x));
            }
            fprintf(cout, "    0x%03X,\n", line);
        }

        fprintf(cout, "  },\n");
    }

    fprintf(cout, "};\n");

    fclose(hout);
    fclose(cout);

    printf("Police 12x12 générée.\n");
    return 0;
}