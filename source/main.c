#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#define FB_WIDTH  640
#define FB_HEIGHT 480

// Available resolutions for cycling
static IrsImageTransferProcessorFormat formats[] = {
    IrsImageTransferProcessorFormat_20x15,
    IrsImageTransferProcessorFormat_40x30,
    IrsImageTransferProcessorFormat_80x60,
    IrsImageTransferProcessorFormat_160x120,
    IrsImageTransferProcessorFormat_320x240
};
static const int num_formats = sizeof(formats)/sizeof(formats[0]);
static int current_format_index = 3; // start at 160x120

// Toggles
static bool useThermal = false;
static bool ledsEnabled = true;   // IR LED toggle
static bool showUI = true;        // UI visibility toggle

// Thermal colour mapping
static u32 thermalColor(u8 intensity) {
    float t = intensity / 255.0f;
    u8 r, g, b;

    if (t < 0.25f) {          // black → blue
        r = 0; g = 0; b = (u8)(t / 0.25f * 255);
    } else if (t < 0.5f) {    // blue → green
        r = 0; g = (u8)((t - 0.25f) / 0.25f * 255); b = 255 - g;
    } else if (t < 0.75f) {   // green → yellow
        r = (u8)((t - 0.5f) / 0.25f * 255); g = 255; b = 0;
    } else {                  // yellow → red → white
        r = 255; g = (u8)(255 - ((t - 0.75f) / 0.25f * 255));
        b = (u8)((t - 0.75f) / 0.25f * 255);
    }
    return RGBA8_MAXALPHA(r, g, b);
}

// --- Font constants and helpers ---
static const int FONT_W = 5;
static const int FONT_H = 7;

typedef struct { char ch; uint8_t rows[7]; } Glyph;

static const Glyph kFont[] = {
    // A–Z
    {'A',{0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}}, {'B',{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
    {'C',{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}}, {'D',{0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}},
    {'E',{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}}, {'F',{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}},
    {'G',{0x0E,0x11,0x10,0x17,0x11,0x11,0x0E}}, {'H',{0x11,0x11,0x11,0x1F,0x11,0x11,0x11}},
    {'I',{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}}, {'J',{0x07,0x02,0x02,0x02,0x12,0x12,0x0C}},
    {'K',{0x11,0x12,0x14,0x18,0x14,0x12,0x11}}, {'L',{0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
    {'M',{0x11,0x1B,0x15,0x11,0x11,0x11,0x11}}, {'N',{0x11,0x19,0x15,0x13,0x11,0x11,0x11}},
    {'O',{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}}, {'P',{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}},
    {'Q',{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}}, {'R',{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}},
    {'S',{0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}}, {'T',{0x1F,0x04,0x04,0x04,0x04,0x04,0x04}},
    {'U',{0x11,0x11,0x11,0x11,0x11,0x11,0x0E}}, {'V',{0x11,0x11,0x11,0x11,0x0A,0x0A,0x04}},
    {'W',{0x11,0x11,0x11,0x11,0x15,0x1B,0x11}}, {'X',{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}},
    {'Y',{0x11,0x11,0x0A,0x04,0x04,0x04,0x04}}, {'Z',{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}},

    // digits
    {'0',{0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}}, {'1',{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}},
    {'2',{0x0E,0x11,0x01,0x06,0x08,0x10,0x1F}}, {'3',{0x1F,0x01,0x02,0x06,0x01,0x11,0x0E}},
    {'4',{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}}, {'5',{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}},
    {'6',{0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}}, {'7',{0x1F,0x01,0x02,0x04,0x08,0x08,0x08}},
    {'8',{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}}, {'9',{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}},

    // symbols
    {' ',{0x00,0x00,0x00,0x00,0x00,0x00,0x00}}, {'-',{0x00,0x00,0x00,0x1F,0x00,0x00,0x00}},
    {'/',{0x01,0x01,0x02,0x04,0x08,0x10,0x10}}
};

static const Glyph* findGlyph(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    for (size_t i = 0; i < sizeof(kFont)/sizeof(kFont[0]); i++) {
        if (kFont[i].ch == c) return &kFont[i];
    }
    return NULL;
}

static void drawChar(u32* fb, u32 stride_words, int x, int y, char c, u32 color, int scale) {
    const Glyph* g = findGlyph(c);
    if (!g) return;
    for (int ry = 0; ry < FONT_H; ry++) {
        uint8_t row = g->rows[ry];
        for (int rx = 0; rx < FONT_W; rx++) {
            int bit = (row >> (FONT_W - 1 - rx)) & 1;
            if (bit) {
                for (int dy = 0; dy < scale; dy++) {
                    for (int dx = 0; dx < scale; dx++) {
                        int px = x + rx*scale + dx;
                        int py = y + ry*scale + dy;
                        if (px < FB_WIDTH && py < FB_HEIGHT)
                            fb[py * stride_words + px] = color;
                    }
                }
            }
        }
    }
}

static void drawString(u32* fb, u32 stride_words, int x, int y, const char* s, u32 color, int scale, int spacing) {
    int cursor = x;
    while (*s) {
        drawChar(fb, stride_words, cursor, y, *s, color, scale);
        cursor += (FONT_W * scale) + spacing;
        s++;
    }
}

static void drawRect(u32* fb, u32 stride_words, int x, int y, int w, int h, u32 color) {
    for (int yy = 0; yy < h; yy++) {
        int row = (y + yy) * stride_words;
        for (int xx = 0; xx < w; xx++) {
            fb[row + x + xx] = color;
        }
    }
}

// Start image processor with current format and LED state via light_target
static Result start_ir_processor(IrsIrCameraHandle irhandle,
                                 IrsImageTransferProcessorFormat format) {
    IrsImageTransferProcessorConfig config;
    irsGetDefaultImageTransferProcessorConfig(&config);
    config.format = format;
    // 0 = all leds on, 3 = none (off)
    config.light_target = ledsEnabled ? 0 : 3;
    return irsRunImageTransferProcessor(irhandle, &config, 0x100000);
}

int main(int argc, char* argv[]) {
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    irsInitialize();

    const size_t ir_buffer_size = 0x12c00;
    u8 *ir_buffer = (u8*)malloc(ir_buffer_size);
    if (!ir_buffer) return 0;
    memset(ir_buffer, 0, ir_buffer_size);

    padUpdate(&pad);
    IrsIrCameraHandle irhandle;
    HidNpadIdType idType = padIsHandheld(&pad) ? HidNpadIdType_Handheld : HidNpadIdType_No1;

    Result rc = irsGetIrCameraHandle(&irhandle, idType);
    if (R_FAILED(rc)) { free(ir_buffer); return 0; }

    rc = start_ir_processor(irhandle, formats[current_format_index]);
    if (R_FAILED(rc)) { free(ir_buffer); return 0; }

    Framebuffer fb;
    framebufferCreate(&fb, nwindowGetDefault(), FB_WIDTH, FB_HEIGHT, PIXEL_FORMAT_RGBA_8888, 2);
    framebufferMakeLinear(&fb);

    u64 sampling_number = 0;

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);
        if (kDown & HidNpadButton_Plus) break;

        // Toggle IR LEDs (apply via light_target)
        if (kDown & HidNpadButton_Minus) {
            ledsEnabled = !ledsEnabled;
            irsStopImageProcessor(irhandle);
            rc = start_ir_processor(irhandle, formats[current_format_index]);
            sampling_number = 0;
        }

        // Toggle UI visibility
        if (kDown & HidNpadButton_StickL) {
            showUI = !showUI;
        }

        // Cycle resolutions (restart processor and keep LED state)
        if (kDown & HidNpadButton_Left) {
            if (current_format_index > 0) current_format_index--;
            irsStopImageProcessor(irhandle);
            rc = start_ir_processor(irhandle, formats[current_format_index]);
            sampling_number = 0;
        }
        if (kDown & HidNpadButton_Right) {
            if (current_format_index < num_formats-1) current_format_index++;
            irsStopImageProcessor(irhandle);
            rc = start_ir_processor(irhandle, formats[current_format_index]);
            sampling_number = 0;
        }

        // Toggle thermal overlay
        if (kDown & HidNpadButton_Up) useThermal = true;
        if (kDown & HidNpadButton_Down) useThermal = false;

        IrsImageTransferProcessorState state;
        rc = irsGetImageTransferProcessorState(irhandle, ir_buffer, ir_buffer_size, &state);

        u32 stride;
        u32* framebuf = (u32*)framebufferBegin(&fb, &stride);

        if (framebuf) {
            const u32 stride_words = (stride >> 2);

            if (R_SUCCEEDED(rc) && state.sampling_number != sampling_number) {
                sampling_number = state.sampling_number;

                // Determine IR resolution
                u32 ir_width=0, ir_height=0;
                switch (formats[current_format_index]) {
                    case IrsImageTransferProcessorFormat_20x15:   ir_width=20; ir_height=15; break;
                    case IrsImageTransferProcessorFormat_40x30:   ir_width=40; ir_height=30; break;
                    case IrsImageTransferProcessorFormat_80x60:   ir_width=80; ir_height=60; break;
                    case IrsImageTransferProcessorFormat_160x120: ir_width=160; ir_height=120; break;
                    case IrsImageTransferProcessorFormat_320x240: ir_width=320; ir_height=240; break;
                }

                // Dynamic scaling
                u32 scale = FB_HEIGHT / ir_height;
                if (scale < 1) scale = 1;
                u32 origin_x = (FB_WIDTH - ir_width*scale) / 2;
                u32 origin_y = (FB_HEIGHT - ir_height*scale) / 2;

                // Draw IR image
                for (u32 y = 0; y < ir_height; y++) {
                    for (u32 x = 0; x < ir_width; x++) {
                        u8 intensity = ir_buffer[y * ir_width + x];
                        u32 color = useThermal ? thermalColor(intensity)
                                               : RGBA8_MAXALPHA(intensity,intensity,intensity);

                        for (u32 dy = 0; dy < scale; dy++) {
                            u32 py = origin_y + y*scale + dy;
                            if (py >= FB_HEIGHT) break;
                            for (u32 dx = 0; dx < scale; dx++) {
                                u32 px = origin_x + x*scale + dx;
                                if (px >= FB_WIDTH) break;
                                framebuf[py * stride_words + px] = color;
                            }
                        }
                    }
                }
            }

            // UI overlay (conditionally drawn)
if (showUI) {
    // Top bar background (taller to fit two lines)
    drawRect(framebuf, stride_words, 0, 0, FB_WIDTH, 56, RGBA8_MAXALPHA(0,0,0));

    // First line: resolution + UI toggle
    const char* msg1 = "Left/Right: change resolution |Left Stick: toggle UI";
    drawString(framebuf, stride_words, 10, 8, msg1, RGBA8_MAXALPHA(255,255,255), 2, 2);

    // Second line: colour mode + LED toggle
    const char* msg2 = "Up/Down: colour mode     |     Minus: toggle IR LED";
    drawString(framebuf, stride_words, 10, 26, msg2, RGBA8_MAXALPHA(255,255,255), 2, 2);

    // Bottom-right: resolution
    const char* res_names[] = {"20x15","40x30","80x60","160x120","320x240"};
    char status[32];
    snprintf(status, sizeof(status), "Resolution: %s", res_names[current_format_index]);
    int status_width = (int)strlen(status) * (FONT_W*2 + 2);
    int status_height = FONT_H * 2;
    int status_x = FB_WIDTH - 10 - status_width;
    int status_y = FB_HEIGHT - 10 - status_height;
    drawRect(framebuf, stride_words, status_x-6, status_y-6,
             status_width+12, status_height+12, RGBA8_MAXALPHA(0,0,0));
    drawString(framebuf, stride_words, status_x, status_y,
               status, RGBA8_MAXALPHA(180,255,180), 2, 2);

    // Bottom-left: colour mode and LED state
    const char* mode_label = useThermal ? "Colour: Thermal" : "Colour: Grayscale";
    char left_label[64];
    snprintf(left_label, sizeof(left_label), "%s  | IR LED: %s",
             mode_label, ledsEnabled ? "On" : "Off");
    int left_width = (int)strlen(left_label) * (FONT_W*2 + 2);
    int left_height = FONT_H * 2;
    int left_x = 10;
    int left_y = FB_HEIGHT - 10 - left_height;
    drawRect(framebuf, stride_words, left_x-6, left_y-6,
             left_width+12, left_height+12, RGBA8_MAXALPHA(0,0,0));
    drawString(framebuf, stride_words, left_x, left_y,
               left_label, RGBA8_MAXALPHA(200,200,255), 2, 2);
}

            framebufferEnd(&fb);
        }
    }

    framebufferClose(&fb);
    irsStopImageProcessor(irhandle);
    free(ir_buffer);
    return 0;
}