#define _GNU_SOURCE
#include <systemd/sd-bus.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <immintrin.h>
#include <png.h>

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

static void bgra_to_rgba(uint8_t *pixels, size_t w, size_t h, size_t stride)
{
    for (size_t y = 0; y < h; y++) {
        uint8_t *row = pixels + y * stride;
        
        // Process row with AVX2 if available and aligned
        #ifdef __AVX2__
        if (((uintptr_t)row & 31) == 0) {  // Check 32-byte alignment
            const __m256i shuf = _mm256_setr_epi8(
                2, 1, 0, 3,  6, 5, 4, 7,  10, 9, 8, 11,  14, 13, 12, 15,
                2, 1, 0, 3,  6, 5, 4, 7,  10, 9, 8, 11,  14, 13, 12, 15
            );
            
            size_t x = 0;
            for (; x + 8 <= w; x += 8) {
                __m256i bgra = _mm256_load_si256((__m256i*)(row + x * 4));
                __m256i rgba = _mm256_shuffle_epi8(bgra, shuf);
                _mm256_store_si256((__m256i*)(row + x * 4), rgba);
            }
            
            // Handle remaining pixels
            for (; x < w; x++) {
                uint8_t *p = row + x * 4;
                uint8_t b = p[0]; p[0] = p[2]; p[2] = b;
            }
        } else
        #endif
        {
            // Fallback to scalar for unaligned or non-AVX2
            for (size_t x = 0; x < w; x++) {
                uint8_t *p = row + x * 4;
                uint8_t b = p[0]; p[0] = p[2]; p[2] = b;
            }
        }
    }
}

typedef struct {
    uint8_t *data;
    size_t size;
    size_t capacity;
} png_memory;

static void png_write_memory(png_structp png, png_bytep data, png_size_t length)
{
    png_memory *mem = (png_memory *)png_get_io_ptr(png);
    if (mem->size + length > mem->capacity) {
        size_t new_cap = mem->capacity * 2;
        while (new_cap < mem->size + length)
            new_cap *= 2;
        uint8_t *new_data = realloc(mem->data, new_cap);
        if (!new_data) {
            png_error(png, "Out of memory");
            return;
        }
        mem->data = new_data;
        mem->capacity = new_cap;
    }
    memcpy(mem->data + mem->size, data, length);
    mem->size += length;
}

static int write_png_to_memory(uint8_t *pixels, uint32_t w, uint32_t h, uint32_t stride, 
                               void **out_data, size_t *out_size)
{
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) return -1;
    
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, NULL);
        return -1;
    }
    
    png_memory mem = {
        .data = malloc(1024 * 1024), // Start with 1MB
        .size = 0,
        .capacity = 1024 * 1024
    };
    
    if (!mem.data) {
        png_destroy_write_struct(&png, &info);
        return -1;
    }
    
    if (setjmp(png_jmpbuf(png))) {
        free(mem.data);
        png_destroy_write_struct(&png, &info);
        return -1;
    }
    
    png_set_write_fn(png, &mem, png_write_memory, NULL);
    
    // Set compression to fastest (1) instead of default (6)
    png_set_compression_level(png, 1);
    png_set_filter(png, 0, PNG_FILTER_NONE);  // No filtering for speed
    
    png_set_IHDR(png, info, w, h, 8, PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    
    png_write_info(png, info);
    
    // Write rows
    for (uint32_t y = 0; y < h; y++) {
        png_write_row(png, pixels + y * stride);
    }
    
    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info);
    
    *out_data = mem.data;
    *out_size = mem.size;
    
    return 0;
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
    void *png_data = NULL;
    size_t png_size = 0;

    if ((fd = open_out(argc > 1 ? argv[1] : NULL, &path)) < 0) {
        fprintf(stderr, "open: %s\n", strerror(-fd));
        goto finish;
    }

    if ((r = sd_bus_default_user(&bus)) < 0) {
        fprintf(stderr, "sd_bus_default_user(): %s\n", strerror(-r));
        goto finish;
    }

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
        sd_bus_message_exit_container(reply);
        sd_bus_message_exit_container(reply);
    }
    sd_bus_message_exit_container(reply);

    if (w == 0 || h == 0 || stride == 0) {
        fprintf(stderr, "kwin did not return geometry!\n");
        goto finish;
    }

    size_t bytes = (size_t)stride * h;
    if ((r = map_fd(fd, bytes, &pixels)) < 0) {
        fprintf(stderr, "mmap: %s\n", strerror(-r));
        goto finish;
    }

    bgra_to_rgba((uint8_t *)pixels, w, h, stride);

    if (write_png_to_memory(pixels, w, h, stride, &png_data, &png_size) < 0) {
        fprintf(stderr, "PNG encoding failed\n");
        goto finish;
    }

    // Write PNG data to file
    if (pwrite(fd, png_data, png_size, 0) != (ssize_t)png_size) {
        fprintf(stderr, "Failed to write PNG data\n");
        goto finish;
    }
    
    // Truncate to actual PNG size
    if (ftruncate(fd, png_size) < 0) {
        fprintf(stderr, "Failed to truncate file\n");
        goto finish;
    }

    printf("Screenshot saved as %s (%ux%u)\n", path, w, h);
    r = 0;

finish:
    free(png_data);
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

