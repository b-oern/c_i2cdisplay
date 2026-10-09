/*
 * boot_display.c
 *
 * Zeigt das Wort "BOOT" zentriert auf einem SSD1306-OLED (128x64)
 * am I2C-Bus 1 eines Raspberry Pi an (/dev/i2c-1, Adresse 0x3C).
 *
 * Build:   gcc -O2 -Wall -o boot_display boot_display.c
 * Start:   ./boot_display            -> zeigt "BOOT"
 *          ./boot_display HALLO      -> zeigt beliebigen Text (A-Z, 0-9)
 *          ./boot_display BOOT 0x3D  -> andere I2C-Adresse
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define I2C_DEVICE   "/dev/i2c-1"
#define OLED_ADDR    0x3C
#define OLED_WIDTH   128
#define OLED_HEIGHT  64
#define OLED_PAGES   (OLED_HEIGHT / 8)

#define FONT_W       5
#define FONT_H       7
#define FONT_SPACING 1   /* Spalten Abstand zwischen Zeichen (unskaliert) */
#define MAX_SCALE    4

static uint8_t framebuffer[OLED_WIDTH * OLED_PAGES];

/* 5x7-Font, spaltenweise, Bit 0 = oberste Zeile */
static const uint8_t font_digits[10][FONT_W] = {
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, /* 0 */
    {0x00, 0x42, 0x7F, 0x40, 0x00}, /* 1 */
    {0x42, 0x61, 0x51, 0x49, 0x46}, /* 2 */
    {0x21, 0x41, 0x45, 0x4B, 0x31}, /* 3 */
    {0x18, 0x14, 0x12, 0x7F, 0x10}, /* 4 */
    {0x27, 0x45, 0x45, 0x45, 0x39}, /* 5 */
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, /* 6 */
    {0x01, 0x71, 0x09, 0x05, 0x03}, /* 7 */
    {0x36, 0x49, 0x49, 0x49, 0x36}, /* 8 */
    {0x06, 0x49, 0x49, 0x29, 0x1E}, /* 9 */
};

static const uint8_t font_upper[26][FONT_W] = {
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, /* A */
    {0x7F, 0x49, 0x49, 0x49, 0x36}, /* B */
    {0x3E, 0x41, 0x41, 0x41, 0x22}, /* C */
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, /* D */
    {0x7F, 0x49, 0x49, 0x49, 0x41}, /* E */
    {0x7F, 0x09, 0x09, 0x09, 0x01}, /* F */
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, /* G */
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, /* H */
    {0x00, 0x41, 0x7F, 0x41, 0x00}, /* I */
    {0x20, 0x40, 0x41, 0x3F, 0x01}, /* J */
    {0x7F, 0x08, 0x14, 0x22, 0x41}, /* K */
    {0x7F, 0x40, 0x40, 0x40, 0x40}, /* L */
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, /* M */
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, /* N */
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, /* O */
    {0x7F, 0x09, 0x09, 0x09, 0x06}, /* P */
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, /* Q */
    {0x7F, 0x09, 0x19, 0x29, 0x46}, /* R */
    {0x46, 0x49, 0x49, 0x49, 0x31}, /* S */
    {0x01, 0x01, 0x7F, 0x01, 0x01}, /* T */
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, /* U */
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, /* V */
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, /* W */
    {0x63, 0x14, 0x08, 0x14, 0x63}, /* X */
    {0x07, 0x08, 0x70, 0x08, 0x07}, /* Y */
    {0x61, 0x51, 0x49, 0x45, 0x43}, /* Z */
};

static const uint8_t *glyph_for(char c)
{
    static const uint8_t blank[FONT_W] = {0};
    c = (char)toupper((unsigned char)c);
    if (c >= 'A' && c <= 'Z')
        return font_upper[c - 'A'];
    if (c >= '0' && c <= '9')
        return font_digits[c - '0'];
    return blank;
}

/* ---------- I2C / SSD1306 ---------- */

static int oled_write(int fd, uint8_t control, const uint8_t *data, size_t len)
{
    uint8_t buf[33];

    /* in Bloecken von max. 32 Byte senden (kompatibel mit allen I2C-Treibern) */
    while (len > 0) {
        size_t n = len > 32 ? 32 : len;
        buf[0] = control;
        memcpy(&buf[1], data, n);
        if (write(fd, buf, n + 1) != (ssize_t)(n + 1)) {
            perror("I2C write");
            return -1;
        }
        data += n;
        len -= n;
    }
    return 0;
}

static int oled_cmds(int fd, const uint8_t *cmds, size_t len)
{
    return oled_write(fd, 0x00, cmds, len);
}

static int oled_init(int fd)
{
    static const uint8_t init_seq[] = {
        0xAE,       /* Display aus */
        0xD5, 0x80, /* Taktteiler */
        0xA8, 0x3F, /* Multiplex 64 */
        0xD3, 0x00, /* Display-Offset 0 */
        0x40,       /* Startzeile 0 */
        0x8D, 0x14, /* Ladungspumpe an */
        0x20, 0x00, /* Horizontaler Adressmodus */
        0xA1,       /* Segment-Remap */
        0xC8,       /* COM-Scan absteigend */
        0xDA, 0x12, /* COM-Pins (128x64) */
        0x81, 0xCF, /* Kontrast */
        0xD9, 0xF1, /* Pre-Charge */
        0xDB, 0x40, /* VCOMH */
        0xA4,       /* Anzeige aus RAM */
        0xA6,       /* normal (nicht invertiert) */
        0xAF,       /* Display an */
    };
    return oled_cmds(fd, init_seq, sizeof(init_seq));
}

static int oled_flush(int fd)
{
    static const uint8_t addr[] = {
        0x21, 0x00, OLED_WIDTH - 1, /* Spaltenbereich */
        0x22, 0x00, OLED_PAGES - 1, /* Seitenbereich */
    };
    if (oled_cmds(fd, addr, sizeof(addr)) < 0)
        return -1;
    return oled_write(fd, 0x40, framebuffer, sizeof(framebuffer));
}

/* ---------- Zeichnen ---------- */

static void set_pixel(int x, int y)
{
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT)
        return;
    framebuffer[(y / 8) * OLED_WIDTH + x] |= (uint8_t)(1u << (y % 8));
}

static void draw_char(int x0, int y0, char c, int scale)
{
    const uint8_t *g = glyph_for(c);
    for (int col = 0; col < FONT_W; col++)
        for (int row = 0; row < FONT_H; row++)
            if (g[col] & (1u << row))
                for (int dx = 0; dx < scale; dx++)
                    for (int dy = 0; dy < scale; dy++)
                        set_pixel(x0 + col * scale + dx, y0 + row * scale + dy);
}

static int text_width(size_t len, int scale)
{
    if (len == 0)
        return 0;
    return (int)len * (FONT_W + FONT_SPACING) * scale - FONT_SPACING * scale;
}

static void draw_text_centered(const char *text)
{
    size_t len = strlen(text);
    int scale = MAX_SCALE;

    /* groesste Skalierung waehlen, bei der der Text noch passt */
    while (scale > 1 && (text_width(len, scale) > OLED_WIDTH ||
                         FONT_H * scale > OLED_HEIGHT))
        scale--;

    int x = (OLED_WIDTH - text_width(len, scale)) / 2;
    int y = (OLED_HEIGHT - FONT_H * scale) / 2;

    for (size_t i = 0; i < len; i++) {
        draw_char(x, y, text[i], scale);
        x += (FONT_W + FONT_SPACING) * scale;
    }
}

int main(int argc, char **argv)
{
    const char *text = argc > 1 ? argv[1] : "BOOT";
    int addr = argc > 2 ? (int)strtol(argv[2], NULL, 0) : OLED_ADDR;

    int fd = open(I2C_DEVICE, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Kann %s nicht oeffnen: %s\n", I2C_DEVICE, strerror(errno));
        return 1;
    }
    if (ioctl(fd, I2C_SLAVE, addr) < 0) {
        fprintf(stderr, "Kann I2C-Adresse 0x%02X nicht setzen: %s\n", addr, strerror(errno));
        close(fd);
        return 1;
    }

    if (oled_init(fd) < 0) {
        close(fd);
        return 1;
    }

    memset(framebuffer, 0, sizeof(framebuffer));
    draw_text_centered(text);

    if (oled_flush(fd) < 0) {
        close(fd);
        return 1;
    }

    close(fd);
    return 0;
}
