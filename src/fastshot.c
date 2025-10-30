#define _GNU_SOURCE
#include <systemd/sd-bus.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <immintrin.h>
#include <png.h>
#include <time.h>
#include <signal.h>
#include <getopt.h>
#include <pthread.h>
#include <stdatomic.h>
#include "image-compare.h"

#define DEFAULT_INTERVAL 45
#define DEFAULT_THRESHOLD 0.99f
#define DEFAULT_DIRECTORY "desktop-record"
#define BGRA_CHANNELS 4

typedef struct {
    uint8_t *data;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    size_t size;
} screenshot_t;

typedef struct {
    const char *directory;
    int interval;
    float threshold;
    int verbose;
    int loop_mode;
    const char *output_file;
} config_t;

static volatile sig_atomic_t running = 1;
static config_t config = {
    .directory = NULL,
    .interval = DEFAULT_INTERVAL,
    .threshold = DEFAULT_THRESHOLD,
    .verbose = 0,
    .loop_mode = 0,
    .output_file = NULL
};

static void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [OPTIONS] [output_file]\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --loop                 Run in loop mode (continuous screenshots)\n");
    fprintf(stderr, "  -d, --directory DIR    Target directory for loop mode (default: ~/desktop-record)\n");
    fprintf(stderr, "  -i, --interval SECS    Screenshot interval for loop mode (default: 45)\n");
    fprintf(stderr, "  -t, --threshold FLOAT  Similarity threshold 0-1 for loop mode (default: 0.99)\n");
    fprintf(stderr, "  -v, --verbose          Enable verbose logging\n");
    fprintf(stderr, "  -h, --help             Show this help\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Single shot mode: %s [output_file]\n", prog);
    fprintf(stderr, "Loop mode:        %s --loop [options]\n", prog);
}

static int parse_args(int argc, char **argv) {
    static struct option long_options[] = {
        {"loop", no_argument, 0, 'l'},
        {"directory", required_argument, 0, 'd'},
        {"interval", required_argument, 0, 'i'},
        {"threshold", required_argument, 0, 't'},
        {"verbose", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "d:i:t:vDh", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'l':
                config.loop_mode = 1;
                break;
            case 'd':
                config.directory = optarg;
                break;
            case 'i':
                config.interval = atoi(optarg);
                if (config.interval <= 0) {
                    fprintf(stderr, "Invalid interval: %s\n", optarg);
                    return -1;
                }
                break;
            case 't':
                config.threshold = atof(optarg);
                if (config.threshold < 0.0 || config.threshold > 1.0) {
                    fprintf(stderr, "Invalid threshold: %s (must be 0-1)\n", optarg);
                    return -1;
                }
                break;
            case 'v':
                config.verbose = 1;
                break;
            case 'h':
                print_usage(argv[0]);
                exit(0);
            default:
                print_usage(argv[0]);
                return -1;
        }
    }

    // Handle output file for single shot mode
    if (!config.loop_mode && optind < argc) {
        config.output_file = argv[optind];
    }

    // Set default directory if not specified for loop mode
    if (config.loop_mode && !config.directory) {
        const char *home = getenv("HOME");
        if (!home) {
            fprintf(stderr, "HOME environment variable not set\n");
            return -1;
        }
        static char default_dir[4096];
        snprintf(default_dir, sizeof(default_dir), "%s/%s", home, DEFAULT_DIRECTORY);
        config.directory = default_dir;
    }

    return 0;
}

static int ensure_directory(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        if (!S_ISDIR(st.st_mode)) {
            fprintf(stderr, "%s exists but is not a directory\n", path);
            return -1;
        }
        return 0;
    }

    // Create directory recursively
    char tmp[4096];
    char *p = NULL;
    size_t len = 0;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/')
        tmp[len - 1] = 0;

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);

    return 0;
}

static int capture_screenshot(sd_bus *bus, screenshot_t *shot) {
    sd_bus_message *reply = NULL;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    int memfd = -1;
    int r = 0;
    
    // Create memory fd for zero-copy capture
    memfd = memfd_create("screenshot", MFD_CLOEXEC);
    if (memfd < 0) {
        return -errno;
    }
    
    // Try CaptureInteractive first (might work better in systemd context)
    r = sd_bus_call_method(bus,
        "org.kde.KWin.ScreenShot2", "/org/kde/KWin/ScreenShot2",
        "org.kde.KWin.ScreenShot2", "CaptureInteractive",
        &err, &reply,
        "uuuuh",
        0,  // include_cursor (0 = no cursor)
        0,  // x
        0,  // y  
        0,  // width (0 = full screen)
        0,  // height (0 = full screen)
        memfd
    );
    
    if (r < 0) {
        // Fall back to CaptureActiveScreen
        sd_bus_error_free(&err);
        sd_bus_message_unref(reply);
        reply = NULL;
        
        r = sd_bus_call_method(bus,
            "org.kde.KWin.ScreenShot2", "/org/kde/KWin/ScreenShot2",
            "org.kde.KWin.ScreenShot2", "CaptureActiveScreen",
            &err, &reply,
            "a{sv}h",
            0,
            memfd
        );
        
        if (r < 0) {
            if (config.verbose) {
                fprintf(stderr, "D-Bus error: %s: %s\n", 
                        err.name ? err.name : "unknown",
                        err.message ? err.message : "no message");
            }
            close(memfd);
            sd_bus_error_free(&err);
            return r;
        }
    }
    
    // Parse response
    uint32_t w = 0, h = 0, stride = 0;
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
    sd_bus_message_unref(reply);
    sd_bus_error_free(&err);
    
    if (w == 0 || h == 0 || stride == 0) {
        close(memfd);
        return -EINVAL;
    }
    
    size_t size = (size_t)stride * h;
    
    // Map the data
    void *data = mmap(NULL, size, PROT_READ, MAP_PRIVATE, memfd, 0);
    close(memfd);
    
    if (data == MAP_FAILED) {
        return -errno;
    }
    
    // Update screenshot structure
    if (shot->data && shot->size > 0) {
        munmap(shot->data, shot->size);
    }
    
    shot->data = data;
    shot->width = w;
    shot->height = h;
    shot->stride = stride;
    shot->size = size;
    
    return 0;
}

// Async PNG writer thread data
typedef struct {
    uint8_t *data;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    char filename[4096];
} png_write_task_t;

static void *png_writer_thread(void *arg) {
    png_write_task_t *task = (png_write_task_t *)arg;
    FILE *fp = fopen(task->filename, "wb");
    if (!fp) {
        fprintf(stderr, "Failed to open %s for writing\n", task->filename);
        free(task->data);
        free(task);
        return NULL;
    }
    
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) {
        fclose(fp);
        free(task->data);
        free(task);
        return NULL;
    }
    
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, NULL);
        fclose(fp);
        free(task->data);
        free(task);
        return NULL;
    }
    
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        free(task->data);
        free(task);
        return NULL;
    }
    
    png_init_io(png, fp);
    
    // Fast compression settings
    png_set_compression_level(png, 1);
    png_set_filter(png, 0, PNG_FILTER_NONE);
    
    png_set_IHDR(png, info, task->width, task->height, 8, PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    
    png_write_info(png, info);
    png_set_bgr(png);
    
    // Write rows
    for (uint32_t y = 0; y < task->height; y++) {
        png_write_row(png, task->data + y * task->stride);
    }
    
    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info);
    fclose(fp);
    
    if (config.verbose) {
        printf("Saved: %s\n", task->filename);
        fflush(stdout);
    }
    
    free(task->data);
    free(task);
    return NULL;
}

static void save_screenshot_async(const screenshot_t *shot, const char *filename) {
    png_write_task_t *task = malloc(sizeof(png_write_task_t));
    if (!task) {
        fprintf(stderr, "Failed to allocate PNG write task\n");
        return;
    }

    // Copy image data - use aligned allocation for AVX compatibility
    // Align to 32 bytes for AVX2 instructions
    size_t aligned_size = (shot->size + 31) & ~31;  // Round up to nearest 32-byte boundary
    task->data = aligned_alloc(32, aligned_size);
    if (!task->data) {
        fprintf(stderr, "Failed to allocate image buffer\n");
        free(task);
        return;
    }

    memcpy(task->data, shot->data, shot->size);
    task->width = shot->width;
    task->height = shot->height;
    task->stride = shot->stride;
    strncpy(task->filename, filename, sizeof(task->filename) - 1);
    task->filename[sizeof(task->filename) - 1] = '\0'; // Ensure null termination
    
    // Create detached thread for async writing
    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    
    if (pthread_create(&thread, &attr, png_writer_thread, task) != 0) {
        fprintf(stderr, "Failed to create PNG writer thread\n");
        free(task->data);
        free(task);
    }
    
    pthread_attr_destroy(&attr);
}

static float compare_screenshots(const screenshot_t *shot1, const screenshot_t *shot2) {
    if (shot1->width != shot2->width || shot1->height != shot2->height || shot1->stride != shot2->stride) {
        return 0.0f; // Different dimensions = not similar
    }
    
    float mse = calculate_mse_bgra(shot1->data, shot2->data, 
                                   shot1->width, shot1->height, 
                                   shot1->stride, shot2->stride);
    
    if (mse < 0) {
        // Error in calculation
        return 0.0f;
    }
    
    return mse_to_similarity(mse);
}

static int check_compositor_ready(sd_bus *bus) {
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    int r = 0;
    
    // Try to check if the compositor is running
    r = sd_bus_call_method(bus,
        "org.kde.KWin", "/Compositor",
        "org.freedesktop.DBus.Properties", "Get",
        &err, &reply,
        "ss",
        "org.kde.kwin.Compositing", "active"
    );
    
    if (r < 0) {
        sd_bus_error_free(&err);
        sd_bus_message_unref(reply);
        return 0; // Assume not ready if we can't check
    }
    
    sd_bus_message_unref(reply);
    sd_bus_error_free(&err);
    return 1;
}

static int run_loop_mode(sd_bus *bus) {
    screenshot_t current = {
        .data = NULL,
        .width = 0,
        .height = 0,
        .stride = 0,
        .size = 0
    };
    screenshot_t last_saved = {
        .data = NULL,
        .width = 0,
        .height = 0,
        .stride = 0,
        .size = 0
    };
    int first_shot = 1;
    
    if (config.verbose) {
        printf("Starting screenshot loop:\n");
        printf("  Directory: %s\n", config.directory);
        printf("  Interval: %d seconds\n", config.interval);
        printf("  Threshold: %.2f\n", config.threshold);
    }
    
    // Wait for compositor to be ready
    int compositor_wait_count = 0;
    while (running && !check_compositor_ready(bus)) {
        if (compositor_wait_count == 0) {
            if (config.verbose) {
                printf("Waiting for compositor to be ready...\n");
            }
        }
        sleep(5);
        compositor_wait_count++;
        if (compositor_wait_count > 12) { // Give up after 60 seconds
            fprintf(stderr, "Warning: Compositor check timed out, proceeding anyway\n");
            break;
        }
    }
    
    while (running) {
        // Capture screenshot
        int r = capture_screenshot(bus, &current);
        if (r < 0) {
            fprintf(stderr, "Failed to capture screenshot: %s\n", strerror(-r));
            
            // If it's a NoOutput error, wait longer before retrying
            if (r == -EIO || r == -ENOENT) {
                if (config.verbose) {
                    fprintf(stderr, "No screen output available, waiting...\n");
                }
                sleep(30); // Wait 30 seconds for screen to become available
            } else {
                sleep(config.interval);
            }
            continue;
        }
        
        int should_save = first_shot;
        
        if (!first_shot && last_saved.data != NULL) {
            // Compare with last saved screenshot
            float similarity = compare_screenshots(&current, &last_saved);
            
            if (config.verbose) {
                printf("Similarity to last saved: %.4f\n", similarity);
                fflush(stdout);
            }
            
            if (similarity < config.threshold) {
                should_save = 1;
            }
        }
        
        if (should_save) {
            // Generate filename
            time_t t = time(NULL);
            struct tm tm = {0};
            localtime_r(&t, &tm);
            char filename[4096];
            snprintf(filename, sizeof(filename), "%s/%04d.%02d.%02d-%02d.%02d.%02d.png",
                     config.directory,
                     tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                     tm.tm_hour, tm.tm_min, tm.tm_sec);
            
            // Save asynchronously
            save_screenshot_async(&current, filename);
            
            // Update last_saved to current screenshot
            if (last_saved.data) {
                munmap(last_saved.data, last_saved.size);
            }
            
            // Transfer ownership to last_saved
            last_saved.data = current.data;
            last_saved.width = current.width;
            last_saved.height = current.height;
            last_saved.stride = current.stride;
            last_saved.size = current.size;
            
            // Clear current to prepare for next capture
            current.data = NULL;
            current.width = 0;
            current.height = 0;
            current.stride = 0;
            current.size = 0;
            
            first_shot = 0;
        } else {
            // Not saving, so free current screenshot
            if (current.data) {
                munmap(current.data, current.size);
                current.data = NULL;
                current.width = 0;
                current.height = 0;
                current.stride = 0;
                current.size = 0;
            }
        }
        
        sleep(config.interval);
    }
    
    // Cleanup
    if (last_saved.data) munmap(last_saved.data, last_saved.size);
    
    if (config.verbose) {
        printf("Shutting down\n");
    }
    
    return 0;
}

static int run_single_shot(sd_bus *bus) {
    screenshot_t shot = {0};
    char *path = NULL;
    int fd = -1;
    int r = 0;
    
    // Generate filename if not provided
    if (config.output_file && *config.output_file) {
        path = strdup(config.output_file);
    } else {
        time_t t = time(NULL);
        struct tm tm = {0};
        localtime_r(&t, &tm);
        char buf[32];
        strftime(buf, sizeof buf, "%Y.%m.%d-%H.%M.%S.png", &tm);
        path = strdup(buf);
    }
    
    if (!path) {
        fprintf(stderr, "Out of memory\n");
        return 1;
    }
    
    // Capture screenshot
    r = capture_screenshot(bus, &shot);
    if (r < 0) {
        fprintf(stderr, "Failed to capture screenshot: %s\n", strerror(-r));
        free(path);
        return 1;
    }
    
    // Open output file
    fd = open(path, O_CREAT|O_RDWR|O_TRUNC|O_CLOEXEC, 0600);
    if (fd < 0) {
        fprintf(stderr, "Failed to open %s: %s\n", path, strerror(errno));
        munmap(shot.data, shot.size);
        free(path);
        return 1;
    }
    
    // Write PNG
    FILE *fp = fdopen(fd, "wb");
    if (!fp) {
        fprintf(stderr, "fdopen failed\n");
        close(fd);
        munmap(shot.data, shot.size);
        free(path);
        return 1;
    }
    
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) {
        fclose(fp);
        munmap(shot.data, shot.size);
        free(path);
        return 1;
    }
    
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, NULL);
        fclose(fp);
        munmap(shot.data, shot.size);
        free(path);
        return 1;
    }
    
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        munmap(shot.data, shot.size);
        free(path);
        return 1;
    }
    
    png_init_io(png, fp);
    png_set_compression_level(png, 1);
    png_set_filter(png, 0, PNG_FILTER_NONE);
    
    png_set_IHDR(png, info, shot.width, shot.height, 8, PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    
    png_write_info(png, info);
    png_set_bgr(png);
    
    // Write rows
    for (uint32_t y = 0; y < shot.height; y++) {
        png_write_row(png, shot.data + y * shot.stride);
    }
    
    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info);
    fclose(fp);
    
    printf("Screenshot saved as %s (%ux%u)\n", path, shot.width, shot.height);
    fflush(stdout);
    
    munmap(shot.data, shot.size);
    free(path);
    return 0;
}

int main(int argc, char **argv) {
    if (parse_args(argc, argv) < 0) {
        return 1;
    }
    
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Ensure output directory exists for loop mode
    if (config.loop_mode && ensure_directory(config.directory) < 0) {
        return 1;
    }
    
    // Initialize D-Bus connection
    sd_bus *bus = NULL;
    int r = sd_bus_default_user(&bus);
    if (r < 0) {
        fprintf(stderr, "Failed to connect to session bus: %s\n", strerror(-r));
        return 1;
    }
    
    if (config.loop_mode) {
        r = run_loop_mode(bus);
    } else {
        r = run_single_shot(bus);
    }
    
    sd_bus_unref(bus);
    return r;
}
