// gpio - v1 - simple wrapper around the sysfs gpio interface - pubic domain - by Patrick Gaskin

#define HIGH 1
#define LOW  0

#define INPUT  "in"
#define OUTPUT "out"

#define EDGE_RISING  "rising"
#define EDGE_FALLING "falling"
#define EDGE_BOTH    "both"
#define EDGE_NONE    "none"

// See https://www.kernel.org/doc/Documentation/gpio/sysfs.txt for documentation.
int gpio_export(int pin);
int gpio_unexport(int pin);
int gpio_read(int pin);
int gpio_write(int pin, int val);
int gpio_edge(int pin, char* edge);
int gpio_direction(int pin, char* dir);
int gpio_active_low(int pin, int val);

// gpio_interrupt waits indefinitely for an interrupt on the specified pin.
int gpio_interrupt(int pin);

// gpio_wait_debounced_until uses the algorithm described in
// https://www.embedded.com/electronics-blogs/break-points/4024981/My-favorite-software-debouncers
// to wait for a button to be pressed on the specified pin. It does not require
// interrupts. The pin must already be exported and set to input.
// Example: pullup on pin 2: gpio_wait_debounced_until(2, LOW, 3);
int gpio_wait_debounced_until(int pin, int pressed_val, int tick_millis);

#ifdef GPIO_IMPLEMENTATION
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>

// Note: these macros depend on GCC compiler extensions.
#define GPIO_PATH_R(F)  ({char p[255]; snprintf(p, 255, "/sys/class/gpio/%s", F); p;})
#define GPIO_PATH(N, F) ({char p[255]; snprintf(p, 255, "/sys/class/gpio/gpio%d/%s", N, F); p;})

// DO_READ reads N bytes of F into O, and returns -1 on error (errno will be set).
#define DO_READ(F, O, N) ({ \
    int tmp = 0, fd = open(F, O_RDONLY); \
    if (fd < 0) tmp = -1; \
    if (!tmp && read(fd, O, N) != N) tmp = -1; \
    if (!tmp) close(fd); \
    tmp; \
})

// DO_WRITE does dprintf on a file.
#define DO_WRITE(F, ...) ({ \
    int tmp = 0, fd = open(F, O_WRONLY); \
    if (fd < 0) tmp = -1; \
    if (!tmp && dprintf(fd, __VA_ARGS__) < 0) tmp = -1; \
    if (!tmp) close(fd); \
    tmp; \
})

int gpio_export(int pin) {
    return DO_WRITE(GPIO_PATH_R("export"), "%d", pin);
}

int gpio_unexport(int pin) {
    return DO_WRITE(GPIO_PATH_R("unexport"), "%d", pin);
}

int gpio_read(int pin) {
    char v;
    if (DO_READ(GPIO_PATH(pin, "value"), &v, 1))
        return -1;
    if (v != '0' && v != '1') {
        errno = EINVAL;
        return -1;
    }
    return v == '1' ? HIGH : LOW;
}

int gpio_wait(int pin) {
    struct pollfd p;
    p.events = POLLPRI | POLLERR;
    p.fd = open(GPIO_PATH(pin, "value"), O_RDONLY);
    if (p.fd < 0)
        return -1;
    if (poll(&p, 1, -1) < 0)
        return -1;
    if (!(p.revents & POLLIN)) {
        errno = -1;
        return -1;
    }
    return 0;
}

int gpio_write(int pin, int val) {
    return DO_WRITE(GPIO_PATH(pin, "value"), "%d", val);
}

int gpio_edge(int pin, char* edge) {
    return DO_WRITE(GPIO_PATH(pin, "edge"), "%s", edge);
}

int gpio_direction(int pin, char* dir) {
    return DO_WRITE(GPIO_PATH(pin, "direction"), "%s", dir);
}

int gpio_active_low(int pin, int val) {
    return DO_WRITE(GPIO_PATH(pin, "active_low"), "%d", val);
}

int gpio_read_debounced_until(int pin, int pressed_val, int tick_millis) {
    int v;
    uint16_t state = 0;
    while (state != 0xF000) {
        nanosleep((const struct timespec[]){{0, tick_millis * 1000000}}, NULL);
        if ((v = gpio_read(pin)) < 0)
            return -1;
        state = (state << 1) | (v != pressed_val ? 0 : 1) | 0xE000;
    }
    return 0;
}

#undef GPIO_PATH_R
#undef GPIO_PATH
#undef DO_READ
#undef DO_WRITE
#endif
