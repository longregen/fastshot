#define _GNU_SOURCE
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <systemd/sd-bus.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "stb_image_write.h"

static int open_out(const char *arg, char **out_path)
{
    if (arg && *arg) {
        *out_path = strdup(arg);
    } else {
        time_t t = time(NULL);
        struct tm tm;
        localtime_r(&t, &tm);
        char buf[32];
        strftime(buf, sizeof buf, "%Y.%m.%d-%H.%M.%S.png", &tm);
        *out_path = strdup(buf);
    }
    if (!*out_path)
        return -ENOMEM;
    return open(*out_path, O_CREAT|O_RDWR|O_TRUNC|O_CLOEXEC, 0600);
}

static int map_fd(int fd, size_t len, void **data)
{
    if (ftruncate(fd, (off_t)len) < 0)
        return -errno;
    *data = mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    return *data == MAP_FAILED ? -errno : 0;
}

static void bgra_to_rgba(uint8_t *p, size_t px)
{
    for (size_t i = 0; i < px; ++i, p += 4) {
        uint8_t b = p[0]; p[0] = p[2]; p[2] = b;
    }
}

int main(int argc, char **argv)
{
    sd_bus *bus = NULL;
    sd_bus_message *reply = NULL;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    char *path = NULL;
    int fd = -1, r = 1;
    uint32_t w = 0, h = 0, stride = 0;
    void *pixels = NULL;

    /* 1. open output file */
    if ((fd = open_out(argc > 1 ? argv[1] : NULL, &path)) < 0) {
        fprintf(stderr, "open: %s\n", strerror(-fd));
        goto finish;
    }

    /* 2. talk to the session bus */
    if ((r = sd_bus_default_user(&bus)) < 0) {
        fprintf(stderr, "sd_bus_default_user(): %s\n", strerror(-r));
        goto finish;
    }

    /* 3. capture raw BGRA */
    r = sd_bus_call_method(bus,
        "org.kde.KWin.ScreenShot2", "/org/kde/KWin/ScreenShot2",
        "org.kde.KWin.ScreenShot2", "CaptureActiveScreen",
        &err, &reply,
        "a{sv}h",
        0,
        fd
    );

    if (r < 0) {
        fprintf(stderr, "CaptureActiveScreen failed: %s: %s\n",
                err.name ?: "<no name>", err.message ?: strerror(-r));
        goto finish;
    }

    /* 4. parse the reply for width/height/stride */
    sd_bus_message_enter_container(reply, 'a', "{sv}");
    while ((r = sd_bus_message_enter_container(reply, 'e', NULL)) > 0) {
        const char *key;
        sd_bus_message_read(reply, "s", &key);
        if (strcmp(key, "width") == 0) {
            sd_bus_message_read(reply, "v", "u", &w);
        } else if (strcmp(key, "height") == 0) {
            sd_bus_message_read(reply, "v", "u", &h);
        } else if (strcmp(key, "stride") == 0) {
            sd_bus_message_read(reply, "v", "u", &stride);
        } else {
            sd_bus_message_skip(reply, "v");
        }
        sd_bus_message_exit_container(reply);   /* variant */
        sd_bus_message_exit_container(reply);   /* dict‑entry */
    }
    sd_bus_message_exit_container(reply);       /* a{sv} */

    if (w == 0 || h == 0 || stride == 0) {
        fprintf(stderr, "kwin did not return geometry!\n");
        goto finish;
    }

    /* 5. mmap the raw buffer, convert & encode */
    size_t bytes = (size_t)stride * h;
    if ((r = map_fd(fd, bytes, &pixels)) < 0) {
        fprintf(stderr, "mmap: %s\n", strerror(-r));
        goto finish;
    }

    bgra_to_rgba((uint8_t *)pixels, (size_t)w * h);

    /*  PNG encode directly over the same file  */
    if (!stbi_write_png(path, w, h, 4, pixels, stride)) {
        fprintf(stderr, "stbi_write_png failed\n");
        goto finish;
    }

    printf("Screenshot saved as %s (%ux%u)\n", path, w, h);
    r = 0;

finish:
    if (pixels)
        munmap(pixels, (size_t)stride * h);
    if (fd >= 0)
        close(fd);
    sd_bus_message_unref(reply);
    sd_bus_unref(bus);
    sd_bus_error_free(&err);
    free(path);
    return r;
}

