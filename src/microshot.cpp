#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <sys/shm.h>
#include <unistd.h>
#include <cstring>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static void die(const char *msg) {
    std::fprintf(stderr, "microshot: %s\n", msg);
    std::exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        die("usage: microshot <file.png>");
    }
    const char *filename = argv[1];

    /* 1. Open display ------------------------------------------------------- */
    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy) die("cannot open $DISPLAY");

    Window root = DefaultRootWindow(dpy);
    XWindowAttributes gwa;
    XGetWindowAttributes(dpy, root, &gwa);
    const int width  = gwa.width;
    const int height = gwa.height;
    const int depth  = DefaultDepth(dpy, DefaultScreen(dpy));
    if (depth != 24 && depth != 32) {
        die("unsupported root depth (need 24/32‑bit)");
    }

    /* 2. Attempt MIT‑SHM for zero‑copy capture ----------------------------- */
    bool use_shm = XShmQueryExtension(dpy);
    XImage *img  = nullptr;
    XShmSegmentInfo shminfo{};

    if (use_shm) {
        img = XShmCreateImage(dpy, DefaultVisual(dpy, DefaultScreen(dpy)),
                              depth, ZPixmap, nullptr, &shminfo,
                              width, height);
        if (!img) use_shm = false;
    }

    if (use_shm) {
        shminfo.shmid = shmget(IPC_PRIVATE,
                               img->bytes_per_line * img->height,
                               IPC_CREAT | 0600);
        if (shminfo.shmid < 0) use_shm = false;
    }

    if (use_shm) {
        shminfo.shmaddr = img->data =
            static_cast<char *>(shmat(shminfo.shmid, nullptr, 0));
        if (shminfo.shmaddr == (char *)-1) use_shm = false;
    }
    if (use_shm) {
        shminfo.readOnly = False;
        if (!XShmAttach(dpy, &shminfo)) use_shm = false;
    }

    if (use_shm) {
        XShmGetImage(dpy, root, img, 0, 0, AllPlanes);
    } else {
        if (img) XDestroyImage(img);
        img = XGetImage(dpy, root, 0, 0, width, height,
                        AllPlanes, ZPixmap);
        if (!img) die("XGetImage failed");
    }

    /* 3. Convert to 24‑bit RGB --------------------------------------------- */
    const int channels = 3;
    std::vector<unsigned char> rgb(width * height * channels);

    auto *src = reinterpret_cast<unsigned char *>(img->data);
    for (int y = 0; y < height; ++y) {
        unsigned char *dst_row = &rgb[y * width * channels];
        unsigned char *src_row = src + y * img->bytes_per_line;
        for (int x = 0; x < width; ++x) {
            unsigned long pixel;
            if (depth == 24) {
                const unsigned char *p = src_row + x * 3;
                pixel = p[2] | (p[1] << 8) | (p[0] << 16);
            } else { // 32
                pixel = reinterpret_cast<unsigned long *>(src_row)[x];
            }
            dst_row[x * 3 + 0] = (pixel & img->red_mask)   >> 16;
            dst_row[x * 3 + 1] = (pixel & img->green_mask) >> 8;
            dst_row[x * 3 + 2] = (pixel & img->blue_mask);
        }
    }

    /* 4. Write PNG ---------------------------------------------------------- */
    if (!stbi_write_png(filename, width, height, channels,
                        rgb.data(), width * channels)) {
        die("PNG write failed");
    }

    /* 5. Clean up ----------------------------------------------------------- */
    if (use_shm) {
        XShmDetach(dpy, &shminfo);
        XDestroyImage(img);
        shmdt(shminfo.shmaddr);
        shmctl(shminfo.shmid, IPC_RMID, nullptr);
    } else {
        XDestroyImage(img);
    }
    XCloseDisplay(dpy);
    return 0;
}

