#ifndef PAIRING_IO_H
#define PAIRING_IO_H
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
static int io_all(int fd, void *data, size_t size, int writing)
{
    uint8_t *p = data;
    while (size) {
        ssize_t n = writing ? write(fd, p, size) : read(fd, p, size);
        if (n <= 0) return -1;
        p += (size_t)n; size -= (size_t)n;
    }
    return 0;
}
static int send_frame(int fd, const uint8_t *data, size_t size)
{
    uint8_t length[2] = {(uint8_t)(size >> 8), (uint8_t)size};
    return size > 65535u || io_all(fd, length, 2, 1) || io_all(fd, (void *)data, size, 1) ? -1 : 0;
}
static int recv_frame(int fd, uint8_t *data, size_t capacity, size_t *size)
{
    uint8_t length[2]; size_t value;
    if (io_all(fd, length, 2, 0)) return -1;
    value = ((size_t)length[0] << 8) | length[1];
    if (value > capacity || io_all(fd, data, value, 0)) return -1;
    *size = value; return 0;
}
#endif
