/* Diagnostic consumer of an external, explicitly supplied decomp snapshot. */
#include "oot3d/mobiclip.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    if (argc != 6) return 2;
    unsigned width = (unsigned)strtoul(argv[1], NULL, 10);
    unsigned height = (unsigned)strtoul(argv[2], NULL, 10);
    if (!width || !height || width > 4096 || height > 4096 ||
        width % 16 || height % 16) return 2;
    FILE* packets = fopen(argv[3], "rb");
    FILE* sizes = fopen(argv[4], "r");
    FILE* output = fopen(argv[5], "wb");
    if (!packets || !sizes || !output) return 2;
    size_t planeBytes = (size_t)width * height;
    unsigned char* storage = calloc(9, planeBytes);
    Oot3dMobiclipMotion* motion = calloc(width / 16 + 3, sizeof(*motion));
    if (!storage || !motion) return 2;
    Oot3dMobiclipFrame frames[6];
    for (unsigned f = 0; f < 6; ++f) {
        unsigned char* base = storage + f * planeBytes * 3 / 2;
        frames[f].planes[0] = (Oot3dMobiclipPlane){base, width, height, width};
        frames[f].planes[1] = (Oot3dMobiclipPlane){base + planeBytes, width/2, height/2, width/2};
        frames[f].planes[2] = (Oot3dMobiclipPlane){base + planeBytes*5/4, width/2, height/2, width/2};
    }
    Oot3dMobiclipDecoder decoder;
    int status = Oot3dMobiclipDecoder_Init(&decoder, frames, motion, width/16+3);
    unsigned completed = 0;
    size_t size;
    while (!status && fscanf(sizes, "%zu", &size) == 1) {
        if (!size || size > 16*1024*1024) { status = -100; break; }
        unsigned char* packet = malloc(size);
        if (!packet || fread(packet, 1, size, packets) != size) { free(packet); status = -101; break; }
        u32 frame = 0;
        Oot3dMobiclipFrameType type;
        status = Oot3dMobiclipDecoder_DecodePacket(&decoder, packet, size, &frame, &type);
        free(packet);
        if (status) break;
        if (frame >= 6) { status = -102; break; }
        for (unsigned p = 0; p < 3; ++p) {
            const Oot3dMobiclipPlane* plane = &decoder.frames[frame].planes[p];
            for (int y = 0; y < plane->height; ++y) {
                if (fwrite(plane->pixels + y*plane->stride, 1, plane->width, output) != (size_t)plane->width)
                    status = -103;
            }
        }
        if (!status) ++completed;
    }
    if (fclose(output)) status = -104;
    fclose(packets);
    fclose(sizes);
    free(motion);
    free(storage);
    printf("{\"status\":%d,\"decoded_frames\":%u}\n", status, completed);
    return status ? 1 : 0;
}
