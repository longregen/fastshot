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
    if (!argv) {
        fprintf(stderr, "Error: argv is null\n");
        return -1;
    }
    
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
    while ((opt = getopt_long(argc, argv, "ld:i:t:vh", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'l':
                config.loop_mode = 1;
                break;
            case 'd':
                if (!optarg) {
                    fprintf(stderr, "Directory argument is null\n");
                    return -1;
                }
                config.directory = optarg;
                break;
            case 'i':
                if (!optarg) {
                    fprintf(stderr, "Interval argument is null\n");
                    return -1;
                }
                config.interval = atoi(optarg);
                if (config.interval <= 0) {
                    fprintf(stderr, "Invalid interval: %s\n", optarg);
                    return -1;
                }
                break;
            case 't':
                if (!optarg) {
                    fprintf(stderr, "Threshold argument is null\n");
                    return -1;
                }
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
                print_usage(argv[0] ? argv[0] : "fastshot");
                exit(0);
            default:
                print_usage(argv[0]);
                return -1;
        }
    }

    // Handle output file for single shot mode
    if (!config.loop_mode && optind < argc && argv[optind]) {
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
    if (!path) {
        fprintf(stderr, "Error: path is null\n");
        return -1;
    }
    
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

    int ret = snprintf(tmp, sizeof(tmp), "%s", path);
    if (ret < 0 || ret >= (int)sizeof(tmp)) {
        fprintf(stderr, "Path too long: %s\n", path);
        return -1;
    }
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
    if (!bus || !shot) {
        return -EINVAL;
    }

    // Retry up to 3 times for empty/invalid captures
    for (int attempt = 0; attempt < 3; attempt++) {
        sd_bus_message *reply = NULL;
        sd_bus_error err = SD_BUS_ERROR_NULL;
        int pipefd[2] = {-1, -1};
        int r = 0;

        // Create a pipe - KWin writes asynchronously, pipe blocks until data ready
        if (pipe(pipefd) < 0) {
            return -errno;
        }

        // Set read end to non-blocking for timeout handling
        int flags = fcntl(pipefd[0], F_GETFL, 0);
        fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

        // Pass write end to KWin
        r = sd_bus_call_method(bus,
            "org.kde.KWin.ScreenShot2", "/org/kde/KWin/ScreenShot2",
            "org.kde.KWin.ScreenShot2", "CaptureActiveScreen",
            &err, &reply,
            "a{sv}h",
            0,  // empty options dictionary
            pipefd[1]
        );

        // Close write end - KWin has its own copy now
        close(pipefd[1]);
        pipefd[1] = -1;

        if (r < 0) {
            if (config.verbose && attempt == 0) {
                fprintf(stderr, "D-Bus error: %s: %s\n",
                        err.name ? err.name : "unknown",
                        err.message ? err.message : "no message");
            }
            close(pipefd[0]);
            sd_bus_error_free(&err);
            if (attempt == 2) return r;
            usleep(100000);
            continue;
        }

        // Parse response to get dimensions
        uint32_t w = 0, h = 0, stride = 0;
        if (!reply) {
            close(pipefd[0]);
            if (attempt == 2) return -EINVAL;
            usleep(100000);
            continue;
        }

        r = sd_bus_message_enter_container(reply, 'a', "{sv}");
        if (r >= 0) {
            while ((r = sd_bus_message_enter_container(reply, 'e', "sv")) > 0) {
                const char *key = NULL;
                r = sd_bus_message_read(reply, "s", &key);
                if (r >= 0 && key) {
                    if (strcmp(key, "width") == 0) {
                        sd_bus_message_read(reply, "v", "u", &w);
                    } else if (strcmp(key, "height") == 0) {
                        sd_bus_message_read(reply, "v", "u", &h);
                    } else if (strcmp(key, "stride") == 0) {
                        sd_bus_message_read(reply, "v", "u", &stride);
                    } else {
                        sd_bus_message_skip(reply, "v");
                    }
                }
                sd_bus_message_exit_container(reply);
            }
            sd_bus_message_exit_container(reply);
        }
        sd_bus_message_unref(reply);
        sd_bus_error_free(&err);

        if (config.verbose) {
            fprintf(stderr, "D-Bus returned: w=%u h=%u stride=%u\n", w, h, stride);
        }

        // Validate dimensions
        if (w == 0 || h == 0 || stride == 0) {
            if (config.verbose) {
                fprintf(stderr, "Invalid dimensions from D-Bus\n");
            }
            close(pipefd[0]);
            if (attempt == 2) return -EINVAL;
            usleep(100000);
            continue;
        }

        // Calculate expected size
        size_t expected_size = (size_t)stride * h;

        if (config.verbose) {
            fprintf(stderr, "Screenshot params: %ux%u, stride=%u, expected size=%zu\n",
                    w, h, stride, expected_size);
        }

        // Allocate buffer for image data
        uint8_t *buffer = malloc(expected_size);
        if (!buffer) {
            close(pipefd[0]);
            if (attempt == 2) return -ENOMEM;
            usleep(100000);
            continue;
        }

        // Read from pipe until we have all data or timeout
        size_t total_read = 0;
        int timeout_ms = 5000;  // 5 second timeout
        int poll_interval_ms = 10;

        while (total_read < expected_size && timeout_ms > 0) {
            ssize_t n = read(pipefd[0], buffer + total_read, expected_size - total_read);
            if (n > 0) {
                total_read += n;
            } else if (n == 0) {
                // EOF - pipe closed
                break;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No data available yet, wait a bit
                usleep(poll_interval_ms * 1000);
                timeout_ms -= poll_interval_ms;
            } else {
                // Real error
                if (config.verbose) {
                    fprintf(stderr, "Read error: %s\n", strerror(errno));
                }
                break;
            }
        }

        close(pipefd[0]);

        if (config.verbose) {
            fprintf(stderr, "Read %zu of %zu bytes\n", total_read, expected_size);
        }

        if (total_read != expected_size) {
            if (config.verbose) {
                fprintf(stderr, "Incomplete read: got %zu, expected %zu\n",
                        total_read, expected_size);
            }
            free(buffer);
            if (attempt == 2) return -EIO;
            usleep(100000);
            continue;
        }

        // Success! Update screenshot structure
        if (shot->data && shot->size > 0) {
            free(shot->data);
            shot->data = NULL;
            shot->size = 0;
        }

        shot->data = buffer;
        shot->width = w;
        shot->height = h;
        shot->stride = stride;
        shot->size = expected_size;

        return 0;  // Success
    }

    return -EIO;  // All attempts failed
}

// Async PNG writer thread data
typedef struct {
    uint8_t *data;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    char filename[4096];
} png_write_task_t;

// Thread tracking for graceful shutdown - circular buffer for O(1) operations
#define MAX_WRITER_THREADS 16
static pthread_t writer_threads[MAX_WRITER_THREADS];
static int writer_head = 0;  // Next slot to write
static int writer_tail = 0;  // Next slot to read/join
static int writer_count = 0; // Current thread count
static pthread_mutex_t writer_mutex = PTHREAD_MUTEX_INITIALIZER;

// Shared PNG writing function - returns 0 on success, -1 on failure
static int write_png(const char *filename, const uint8_t *data,
                     uint32_t width, uint32_t height, uint32_t stride) {
    if (!filename || !data || width == 0 || height == 0) {
        return -1;
    }

    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        fprintf(stderr, "Failed to open %s for writing\n", filename);
        return -1;
    }

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) {
        fclose(fp);
        return -1;
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, NULL);
        fclose(fp);
        return -1;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        return -1;
    }

    png_init_io(png, fp);
    png_set_compression_level(png, 1);
    png_set_filter(png, 0, PNG_FILTER_NONE);

    png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);

    png_write_info(png, info);
    png_set_bgr(png);

    for (uint32_t y = 0; y < height; y++) {
        png_write_row(png, data + (size_t)y * stride);
    }

    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info);
    fclose(fp);
    return 0;
}

static void wait_for_writer_threads(void) {
    pthread_mutex_lock(&writer_mutex);
    while (writer_count > 0) {
        pthread_t thread = writer_threads[writer_tail];
        writer_tail = (writer_tail + 1) % MAX_WRITER_THREADS;
        writer_count--;
        pthread_mutex_unlock(&writer_mutex);
        pthread_join(thread, NULL);
        pthread_mutex_lock(&writer_mutex);
    }
    pthread_mutex_unlock(&writer_mutex);
}

static void *png_writer_thread(void *arg) {
    png_write_task_t *task = (png_write_task_t *)arg;
    if (!task) {
        return NULL;
    }

    if (write_png(task->filename, task->data, task->width, task->height, task->stride) == 0) {
        if (config.verbose) {
            printf("Saved: %s\n", task->filename);
            fflush(stdout);
        }
    }

    free(task->data);
    free(task);
    return NULL;
}

static void save_screenshot_async(const screenshot_t *shot, const char *filename) {
    if (!shot || !filename || !shot->data) {
        fprintf(stderr, "Invalid arguments to save_screenshot_async\n");
        return;
    }
    
    png_write_task_t *task = malloc(sizeof(png_write_task_t));
    if (!task) {
        fprintf(stderr, "Failed to allocate PNG write task\n");
        return;
    }
    
    memset(task, 0, sizeof(png_write_task_t));
    
    // Copy image data with validation
    if (shot->size == 0 || shot->width == 0 || shot->height == 0) {
        fprintf(stderr, "Invalid image dimensions\n");
        free(task);
        return;
    }
    
    // Validate that we have reasonable data - use actual stride for validation
    size_t min_required_size = (size_t)shot->stride * shot->height;
    if (shot->size < min_required_size) {
        fprintf(stderr, "Image data too small: %zu < %zu (stride*height)\n", shot->size, min_required_size);
        free(task);
        return;
    }
    
    // Calculate correct stride if the actual size doesn't match calculated size
    uint32_t actual_stride = shot->stride;
    size_t expected_size = (size_t)shot->stride * shot->height;
    if (shot->size != expected_size && shot->height > 0) {
        actual_stride = shot->size / shot->height;
        if (config.verbose) {
            fprintf(stderr, "Adjusting stride from %u to %u based on actual size\n", 
                    shot->stride, actual_stride);
        }
    }
    
    task->data = malloc(shot->size);
    if (!task->data) {
        fprintf(stderr, "Failed to allocate image buffer\n");
        free(task);
        return;
    }
    
    // Use memmove for safety in case of overlapping memory
    memmove(task->data, shot->data, shot->size);
    task->width = shot->width;
    task->height = shot->height;
    task->stride = actual_stride;
    strncpy(task->filename, filename, sizeof(task->filename) - 1);
    task->filename[sizeof(task->filename) - 1] = '\0'; // Ensure null termination
    
    pthread_t thread;
    pthread_mutex_lock(&writer_mutex);

    // If buffer full, wait for oldest thread to finish (O(1) circular buffer)
    if (writer_count >= MAX_WRITER_THREADS) {
        pthread_t oldest = writer_threads[writer_tail];
        writer_tail = (writer_tail + 1) % MAX_WRITER_THREADS;
        writer_count--;
        pthread_mutex_unlock(&writer_mutex);
        pthread_join(oldest, NULL);
        pthread_mutex_lock(&writer_mutex);
    }

    if (pthread_create(&thread, NULL, png_writer_thread, task) != 0) {
        fprintf(stderr, "Failed to create PNG writer thread\n");
        pthread_mutex_unlock(&writer_mutex);
        free(task->data);
        free(task);
        return;
    }

    writer_threads[writer_head] = thread;
    writer_head = (writer_head + 1) % MAX_WRITER_THREADS;
    writer_count++;
    pthread_mutex_unlock(&writer_mutex);
}

static float compare_screenshots(const screenshot_t *shot1, const screenshot_t *shot2) {
    if (!shot1 || !shot2 || !shot1->data || !shot2->data) {
        return 0.0f; // Invalid screenshots
    }
    
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
    if (!bus) {
        return 0;
    }
    
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
    
    fprintf(stderr, "LOOP MODE: interval=%d threshold=%.2f dir=%s\n",
            config.interval, config.threshold, config.directory ? config.directory : "(null)");
    fflush(stderr);
    if (config.verbose) {
        printf("Starting screenshot loop:\n");
        printf("  Directory: %s\n", config.directory);
        printf("  Interval: %d seconds\n", config.interval);
        printf("  Threshold: %.2f\n", config.threshold);
        fflush(stdout);
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
            if (last_saved.data && last_saved.size > 0) {
                free(last_saved.data);
                last_saved.data = NULL;
                last_saved.size = 0;
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
            if (current.data && current.size > 0) {
                free(current.data);
                current.data = NULL;
                current.width = 0;
                current.height = 0;
                current.stride = 0;
                current.size = 0;
            }
        }
        
        sleep(config.interval);
    }
    
    // Wait for all pending PNG writes to complete
    if (config.verbose) {
        fprintf(stderr, "Waiting for pending writes to complete...\n");
    }
    wait_for_writer_threads();

    // Cleanup
    if (last_saved.data && last_saved.size > 0) {
        free(last_saved.data);
    }

    if (config.verbose) {
        fprintf(stderr, "Shutting down\n");
    }

    return 0;
}

static int run_single_shot(sd_bus *bus) {
    if (!bus) {
        fprintf(stderr, "Invalid bus connection\n");
        return 1;
    }

    screenshot_t shot = {0};
    char path[64];
    int ret = 1;

    // Generate filename if not provided
    if (config.output_file && *config.output_file) {
        snprintf(path, sizeof(path), "%s", config.output_file);
    } else {
        time_t t = time(NULL);
        struct tm tm = {0};
        localtime_r(&t, &tm);
        strftime(path, sizeof(path), "%Y.%m.%d-%H.%M.%S.png", &tm);
    }

    if (capture_screenshot(bus, &shot) < 0) {
        fprintf(stderr, "Failed to capture screenshot\n");
        return 1;
    }

    if (write_png(path, shot.data, shot.width, shot.height, shot.stride) == 0) {
        printf("Screenshot saved as %s (%ux%u)\n", path, shot.width, shot.height);
        ret = 0;
    }

    free(shot.data);
    return ret;
}

int main(int argc, char **argv) {
    if (argc < 1 || !argv) {
        fprintf(stderr, "Invalid arguments\n");
        return 1;
    }
    
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
