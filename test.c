#include <systemd/sd-bus.h>
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char **argv) {
        int fd = open("/dev/shm/shot.png", O_CREAT|O_WRONLY|O_TRUNC, 0600);
        sd_bus *bus = NULL;
        sd_bus_message *m = NULL;
        sd_bus_open_user(&bus);

        sd_bus_call_method(bus,
                "org.kde.KWin.ScreenShot2",
                "/org/kde/KWin/ScreenShot2",
                "org.kde.KWin.ScreenShot2",
                "CaptureScreen",
                NULL, NULL,
                "shaya{sv}h", "", 0, 0, 0, fd);

        sd_bus_message_unref(m);
        sd_bus_unref(bus);
        close(fd);
}

