#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <png.h>
#include <dbus/dbus.h>
#include <immintrin.h>  // For SIMD

#ifdef USE_WLROOTS
#include <wayland-client.h>
#include <wlr-screencopy-unstable-v1-client-protocol.h>
#include <sys/mman.h>
#endif

void die(const char *msg) {
    fprintf(stderr, "Error: %s\n", msg);
    exit(1);
}

static inline int is_wayland() {
    return getenv("WAYLAND_DISPLAY") != NULL;
}

void write_png(const char *filename, unsigned char *data, int width, int height, int bytes_per_pixel) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) die("Cannot open output file");
    
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) die("png_create_write_struct failed");
    
    png_infop info = png_create_info_struct(png);
    if (!info) die("png_create_info_struct failed");
    
    if (setjmp(png_jmpbuf(png))) die("PNG write error");
    
    png_init_io(png, fp);
    
    // Fast compression level 1 - better balance of speed and size
    png_set_compression_level(png, 1);
    png_set_filter(png, 0, PNG_FILTER_NONE);
    
    png_set_IHDR(png, info, width, height, 8, 
                 bytes_per_pixel == 4 ? PNG_COLOR_TYPE_RGBA : PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    
    png_write_info(png, info);
    
    // Write directly without conversion if possible
    png_bytep row_pointers[height];
    for (int y = 0; y < height; y++) {
        row_pointers[y] = data + y * width * bytes_per_pixel;
    }
    png_write_image(png, row_pointers);
    
    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info);
    fclose(fp);
}

void screenshot_x11(const char *filename) {
    Display *display = XOpenDisplay(NULL);
    if (!display) die("Cannot open X11 display");
    
    Window root = DefaultRootWindow(display);
    XWindowAttributes attrs;
    XGetWindowAttributes(display, root, &attrs);
    
    int width = attrs.width;
    int height = attrs.height;
    
    XImage *image = XGetImage(display, root, 0, 0, width, height, AllPlanes, ZPixmap);
    if (!image) die("XGetImage failed");
    
    // Direct memory access for better performance
    int bytes_per_pixel = image->bits_per_pixel / 8;
    unsigned char *data = NULL;
    
    if (bytes_per_pixel == 4 && image->byte_order == LSBFirst &&
        image->red_mask == 0xff0000 && image->green_mask == 0xff00 && 
        image->blue_mask == 0xff) {
        // BGRA format - convert to RGBA using SIMD
        data = malloc(width * height * 4);
        if (!data) die("malloc failed");
        
        unsigned char *src = (unsigned char *)image->data;
        unsigned char *dst = data;
        
        int pixels = width * height;
        int i = 0;
        
#ifdef __AVX2__
        // AVX2 path - process 8 pixels at a time
        __m256i shuffle_mask = _mm256_setr_epi8(
            2,1,0,3, 6,5,4,7, 10,9,8,11, 14,13,12,15,
            2,1,0,3, 6,5,4,7, 10,9,8,11, 14,13,12,15
        );
        
        for (; i <= pixels - 8; i += 8) {
            __m256i bgra = _mm256_loadu_si256((__m256i*)src);
            __m256i rgba = _mm256_shuffle_epi8(bgra, shuffle_mask);
            _mm256_storeu_si256((__m256i*)dst, rgba);
            src += 32;
            dst += 32;
        }
#elif defined(__SSE3__)
        // SSE3 path - process 4 pixels at a time
        __m128i shuffle_mask = _mm_setr_epi8(2,1,0,3, 6,5,4,7, 10,9,8,11, 14,13,12,15);
        
        for (; i <= pixels - 4; i += 4) {
            __m128i bgra = _mm_loadu_si128((__m128i*)src);
            __m128i rgba = _mm_shuffle_epi8(bgra, shuffle_mask);
            _mm_storeu_si128((__m128i*)dst, rgba);
            src += 16;
            dst += 16;
        }
#endif
        
        // Handle remaining pixels
        for (; i < pixels; i++) {
            dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0]; dst[3] = src[3];
            src += 4;
            dst += 4;
        }
        write_png(filename, data, width, height, 4);
    } else if (bytes_per_pixel == 3 || bytes_per_pixel == 4) {
        // Generic conversion path
        data = malloc(width * height * 3);
        if (!data) die("malloc failed");
        
        // Calculate bit shifts more efficiently
        unsigned long red_mask = image->red_mask;
        unsigned long green_mask = image->green_mask;
        unsigned long blue_mask = image->blue_mask;
        
        int red_shift = __builtin_ctz(red_mask);
        int green_shift = __builtin_ctz(green_mask);
        int blue_shift = __builtin_ctz(blue_mask);
        
        unsigned char *src = (unsigned char *)image->data;
        unsigned char *dst = data;
        
        // Process row by row with better cache locality
        for (int y = 0; y < height; y++) {
            unsigned char *row = src + y * image->bytes_per_line;
            for (int x = 0; x < width; x++) {
                unsigned long pixel = *(unsigned long *)(row + x * bytes_per_pixel);
                
                *dst++ = (pixel >> red_shift) & 0xff;
                *dst++ = (pixel >> green_shift) & 0xff;
                *dst++ = (pixel >> blue_shift) & 0xff;
            }
        }
        write_png(filename, data, width, height, 3);
    } else {
        die("Unsupported pixel format");
    }
    
    free(data);
    XDestroyImage(image);
    XCloseDisplay(display);
}

void screenshot_wayland(const char *filename) {
    DBusError err;
    DBusConnection *conn;
    DBusMessage *msg, *reply;
    
    dbus_error_init(&err);
    conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (!conn) die("Cannot connect to DBus");
    
    // Try wlroots-based compositors (Sway, etc) via grim protocol
    msg = dbus_message_new_method_call("org.freedesktop.impl.portal.desktop.wlr",
                                       "/org/freedesktop/portal/desktop",
                                       "org.freedesktop.impl.portal.Screenshot",
                                       "Screenshot");
    
    if (msg) {
        DBusMessageIter args, options;
        dbus_message_iter_init_append(msg, &args);
        
        const char *parent_window = "";
        dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &parent_window);
        
        dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &options);
        dbus_message_iter_close_container(&args, &options);
        
        reply = dbus_connection_send_with_reply_and_block(conn, msg, 1000, &err);
        if (reply) {
            // Handle portal response
            dbus_message_unref(msg);
            dbus_message_unref(reply);
            dbus_connection_unref(conn);
            return;
        }
        dbus_message_unref(msg);
        dbus_error_free(&err);
    }
    
    // Try KDE first (fastest on KDE Plasma)
    msg = dbus_message_new_method_call("org.kde.KWin",
                                       "/Screenshot",
                                       "org.kde.kwin.Screenshot",
                                       "screenshotFullscreen");
    if (!msg) die("Cannot create DBus message");
    
    // Shorter timeout for faster fallback
    reply = dbus_connection_send_with_reply_and_block(conn, msg, 500, &err);
    
    if (!reply) {
        dbus_message_unref(msg);
        dbus_error_free(&err);
        
        // Try GNOME
        msg = dbus_message_new_method_call("org.gnome.Shell.Screenshot",
                                           "/org/gnome/Shell/Screenshot",
                                           "org.gnome.Shell.Screenshot",
                                           "Screenshot");
        if (!msg) die("Cannot create DBus message");
        
        DBusMessageIter args;
        dbus_message_iter_init_append(msg, &args);
        dbus_bool_t include_cursor = FALSE;
        dbus_bool_t flash = FALSE;
        dbus_message_iter_append_basic(&args, DBUS_TYPE_BOOLEAN, &include_cursor);
        dbus_message_iter_append_basic(&args, DBUS_TYPE_BOOLEAN, &flash);
        dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &filename);
        
        reply = dbus_connection_send_with_reply_and_block(conn, msg, 500, &err);
        
        if (!reply) {
            dbus_message_unref(msg);
            dbus_connection_unref(conn);
            die("No supported Wayland compositor");
        }
        
        dbus_message_unref(msg);
        dbus_message_unref(reply);
        dbus_connection_unref(conn);
        return;
    }
    
    // KDE path - copy file efficiently
    char *tmp_path;
    if (!dbus_message_get_args(reply, &err, DBUS_TYPE_STRING, &tmp_path, DBUS_TYPE_INVALID)) {
        dbus_message_unref(msg);
        dbus_message_unref(reply);
        dbus_connection_unref(conn);
        die("Cannot get screenshot path");
    }
    
    // Use sendfile for efficient copying
    int src_fd = open(tmp_path, O_RDONLY);
    if (src_fd < 0) die("Cannot open temp file");
    
    int dst_fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd < 0) {
        close(src_fd);
        die("Cannot create output file");
    }
    
    struct stat st;
    fstat(src_fd, &st);
    
    #ifdef __linux__
    #include <sys/sendfile.h>
    sendfile(dst_fd, src_fd, NULL, st.st_size);
    #else
    char buffer[65536];
    ssize_t bytes;
    while ((bytes = read(src_fd, buffer, sizeof(buffer))) > 0) {
        write(dst_fd, buffer, bytes);
    }
    #endif
    
    close(src_fd);
    close(dst_fd);
    unlink(tmp_path);
    
    dbus_message_unref(msg);
    dbus_message_unref(reply);
    dbus_connection_unref(conn);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s output.png\n", argv[0]);
        return 1;
    }
    
    if (is_wayland()) {
        screenshot_wayland(argv[1]);
    } else {
        screenshot_x11(argv[1]);
    }
    
    return 0;
}
