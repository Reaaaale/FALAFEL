#include "desktop_transport.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CONNECT_TIMEOUT_MS 5000U
#define RECONNECT_DELAY_MS 1000U
#define STATE_MAGIC "FALSTV2\0"
#define STATE_VERSION 2U
#define STATE_BUFFER_CAPACITY 16384U
#define ID_RESERVATION_SIZE 64U

static uint64_t monotonic_ms(void)
{
    struct timespec time_value;
    clock_gettime(CLOCK_MONOTONIC, &time_value);
    return (uint64_t)time_value.tv_sec * 1000U +
           (uint64_t)time_value.tv_nsec / 1000000U;
}

static void disconnect_transport(desktop_transport_t *transport)
{
    if (transport->fd >= 0) {
        close(transport->fd);
    }
    transport->fd = -1;
    transport->input_length = 0U;
    transport->output_offset = 0U;
    transport->output_length = 0U;
    transport->state = DESKTOP_TRANSPORT_DISCONNECTED;
    transport->reconnect_after_ms = monotonic_ms() + RECONNECT_DELAY_MS;
}

void desktop_transport_init(desktop_transport_t *transport, const char *host,
                            uint16_t port, uint32_t device_id,
                            uint32_t peer_id, desktop_event_fn callback,
                            void *user_data)
{
    memset(transport, 0, sizeof(*transport));
    transport->fd = -1;
    snprintf(transport->host, sizeof(transport->host), "%s", host);
    transport->port = port;
    transport->device_id = device_id;
    transport->peer_id = peer_id;
    transport->on_event = callback;
    transport->user_data = user_data;
    pthread_mutex_init(&transport->resolver_mutex, NULL);
}

void desktop_transport_deinit(desktop_transport_t *transport)
{
    disconnect_transport(transport);
    if (transport->resolver_started) {
        pthread_join(transport->resolver_thread, NULL);
    }
    pthread_mutex_destroy(&transport->resolver_mutex);
}

static void *resolve_host(void *context)
{
    desktop_transport_t *transport = context;
    struct addrinfo hints = {0};
    struct addrinfo *addresses = NULL;
    char port[8];
    int result;

    snprintf(port, sizeof(port), "%u", transport->port);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    result = getaddrinfo(transport->host, port, &hints, &addresses);

    pthread_mutex_lock(&transport->resolver_mutex);
    transport->resolver_error = result;
    if (result == 0 && addresses != NULL &&
        addresses->ai_addrlen <= sizeof(transport->resolved_address)) {
        memcpy(&transport->resolved_address, addresses->ai_addr,
               addresses->ai_addrlen);
        transport->resolved_length = (socklen_t)addresses->ai_addrlen;
    } else if (result == 0) {
        transport->resolver_error = EAI_FAIL;
    }
    transport->resolver_done = true;
    pthread_mutex_unlock(&transport->resolver_mutex);
    freeaddrinfo(addresses);
    return NULL;
}

static void start_resolution(desktop_transport_t *transport)
{
    if (monotonic_ms() < transport->reconnect_after_ms) {
        return;
    }
    transport->resolver_done = false;
    transport->resolver_error = 0;
    transport->resolver_started =
        pthread_create(&transport->resolver_thread, NULL, resolve_host,
                       transport) == 0;
    if (!transport->resolver_started) {
        transport->reconnect_after_ms = monotonic_ms() + RECONNECT_DELAY_MS;
        return;
    }
    transport->state = DESKTOP_TRANSPORT_RESOLVING;
}

static void begin_connection(desktop_transport_t *transport)
{
    int flags;
    int result;

    transport->fd = socket(transport->resolved_address.ss_family,
                           SOCK_STREAM, 0);
    if (transport->fd < 0) {
        disconnect_transport(transport);
        return;
    }
    flags = fcntl(transport->fd, F_GETFL, 0);
    if (flags < 0 || fcntl(transport->fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        disconnect_transport(transport);
        return;
    }
    result = connect(transport->fd,
                     (struct sockaddr *)&transport->resolved_address,
                     transport->resolved_length);
    if (result == 0) {
        transport->state = DESKTOP_TRANSPORT_HANDSHAKE;
    } else if (errno == EINPROGRESS) {
        transport->state = DESKTOP_TRANSPORT_CONNECTING;
    } else {
        disconnect_transport(transport);
        return;
    }
    transport->connect_deadline_ms = monotonic_ms() + CONNECT_TIMEOUT_MS;
}

static void finish_resolution(desktop_transport_t *transport)
{
    bool done;
    int error;

    pthread_mutex_lock(&transport->resolver_mutex);
    done = transport->resolver_done;
    error = transport->resolver_error;
    pthread_mutex_unlock(&transport->resolver_mutex);
    if (!done) {
        return;
    }
    pthread_join(transport->resolver_thread, NULL);
    transport->resolver_started = false;
    if (error != 0) {
        disconnect_transport(transport);
        return;
    }
    begin_connection(transport);
}

static void finish_connection(desktop_transport_t *transport)
{
    struct pollfd descriptor = {.fd = transport->fd, .events = POLLOUT};
    int socket_error = 0;
    socklen_t error_length = sizeof(socket_error);
    int ready = poll(&descriptor, 1, 0);

    if (monotonic_ms() >= transport->connect_deadline_ms) {
        disconnect_transport(transport);
        return;
    }
    if (ready <= 0) {
        return;
    }
    if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 ||
        getsockopt(transport->fd, SOL_SOCKET, SO_ERROR, &socket_error,
                   &error_length) < 0 || socket_error != 0) {
        disconnect_transport(transport);
        return;
    }
    transport->state = DESKTOP_TRANSPORT_HANDSHAKE;
}

static bool queue_bytes(desktop_transport_t *transport, const uint8_t *data,
                        size_t length)
{
    if (transport->output_length != 0U ||
        length > sizeof(transport->output)) {
        return false;
    }
    memcpy(transport->output, data, length);
    transport->output_offset = 0U;
    transport->output_length = length;
    return true;
}

static void queue_handshake(desktop_transport_t *transport)
{
    char hello[80];
    int length = snprintf(hello, sizeof(hello), "HELLO %u %u\n",
                          transport->device_id, transport->peer_id);

    if (length <= 0 || (size_t)length >= sizeof(hello) ||
        !queue_bytes(transport, (const uint8_t *)hello, (size_t)length)) {
        disconnect_transport(transport);
    }
}

static void write_pending(desktop_transport_t *transport)
{
    while (transport->output_offset < transport->output_length) {
        ssize_t count = send(transport->fd,
                             transport->output + transport->output_offset,
                             transport->output_length - transport->output_offset,
                             MSG_NOSIGNAL);
        if (count > 0) {
            transport->output_offset += (size_t)count;
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;
        }
        disconnect_transport(transport);
        return;
    }
    transport->output_offset = 0U;
    transport->output_length = 0U;
    if (transport->state == DESKTOP_TRANSPORT_HANDSHAKE) {
        transport->state = DESKTOP_TRANSPORT_CONNECTED;
    }
}

static void read_available(desktop_transport_t *transport)
{
    for (;;) {
        size_t available = sizeof(transport->input) - transport->input_length;
        ssize_t count;

        if (available == 0U) {
            disconnect_transport(transport);
            return;
        }
        count = recv(transport->fd,
                     transport->input + transport->input_length,
                     available, 0);
        if (count > 0) {
            transport->input_length += (size_t)count;
            continue;
        }
        if (count == 0) {
            disconnect_transport(transport);
            return;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        disconnect_transport(transport);
        return;
    }

    while (transport->input_length >= 4U) {
        uint32_t network_size;
        uint32_t size;
        falafel_event_t event;

        memcpy(&network_size, transport->input, sizeof(network_size));
        size = ntohl(network_size);
        if (size < FALAFEL_EVENT_WIRE_HEADER_SIZE ||
            size > FALAFEL_MAX_EVENT_SIZE) {
            disconnect_transport(transport);
            return;
        }
        if (transport->input_length < (size_t)size + 4U) {
            return;
        }
        if (!falafel_event_deserialize(transport->input + 4U, size, &event)) {
            disconnect_transport(transport);
            return;
        }
        if (transport->on_event != NULL) {
            transport->on_event(transport->user_data, &event);
        }
        memmove(transport->input, transport->input + 4U + size,
                transport->input_length - 4U - size);
        transport->input_length -= 4U + size;
    }
}

void desktop_transport_poll(desktop_transport_t *transport)
{
    if (transport->state == DESKTOP_TRANSPORT_DISCONNECTED) {
        start_resolution(transport);
    }
    if (transport->state == DESKTOP_TRANSPORT_RESOLVING) {
        finish_resolution(transport);
    }
    if (transport->state == DESKTOP_TRANSPORT_CONNECTING) {
        finish_connection(transport);
    }
    if (transport->state == DESKTOP_TRANSPORT_HANDSHAKE &&
        transport->output_length == 0U) {
        queue_handshake(transport);
    }
    if ((transport->state == DESKTOP_TRANSPORT_HANDSHAKE ||
         transport->state == DESKTOP_TRANSPORT_CONNECTED) &&
        transport->output_length != 0U) {
        write_pending(transport);
    }
    if (transport->state == DESKTOP_TRANSPORT_CONNECTED) {
        read_available(transport);
    }
}

bool desktop_transport_send(void *context, const falafel_event_t *event)
{
    desktop_transport_t *transport = context;
    uint8_t frame[FALAFEL_MAX_EVENT_SIZE + 4U];
    size_t serialized_size = 0U;
    uint32_t network_size;

    if (transport->state != DESKTOP_TRANSPORT_CONNECTED ||
        transport->output_length != 0U ||
        !falafel_event_serialize(event, frame + 4U,
                                 sizeof(frame) - 4U, &serialized_size)) {
        return false;
    }
    network_size = htonl((uint32_t)serialized_size);
    memcpy(frame, &network_size, sizeof(network_size));
    return queue_bytes(transport, frame, serialized_size + 4U);
}

bool desktop_transport_is_connected(const desktop_transport_t *transport)
{
    return transport->state == DESKTOP_TRANSPORT_CONNECTED;
}

void desktop_transport_force_reconnect(desktop_transport_t *transport)
{
    disconnect_transport(transport);
}

static uint32_t crc32_bytes(const uint8_t *data, size_t length)
{
    uint32_t crc = UINT32_C(0xffffffff);
    for (size_t index = 0; index < length; ++index) {
        crc ^= data[index];
        for (unsigned bit = 0; bit < 8U; ++bit) {
            crc = (crc >> 1U) ^ (UINT32_C(0xedb88320) &
                  (uint32_t)-(int32_t)(crc & 1U));
        }
    }
    return ~crc;
}

static void put_u16(uint8_t **cursor, uint16_t value)
{
    *(*cursor)++ = (uint8_t)(value >> 8);
    *(*cursor)++ = (uint8_t)value;
}

static void put_u32(uint8_t **cursor, uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8) {
        *(*cursor)++ = (uint8_t)(value >> shift);
    }
}

static void put_u64(uint8_t **cursor, uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8) {
        *(*cursor)++ = (uint8_t)(value >> shift);
    }
}

static bool take_u8(const uint8_t **cursor, const uint8_t *end, uint8_t *value)
{
    if (*cursor >= end) return false;
    *value = *(*cursor)++;
    return true;
}

static bool take_u16(const uint8_t **cursor, const uint8_t *end, uint16_t *value)
{
    if ((size_t)(end - *cursor) < 2U) return false;
    *value = (uint16_t)((uint16_t)(*cursor)[0] << 8 | (*cursor)[1]);
    *cursor += 2;
    return true;
}

static bool take_u32(const uint8_t **cursor, const uint8_t *end, uint32_t *value)
{
    if ((size_t)(end - *cursor) < 4U) return false;
    *value = 0U;
    for (unsigned index = 0; index < 4U; ++index) {
        *value = (*value << 8) | *(*cursor)++;
    }
    return true;
}

static bool take_u64(const uint8_t **cursor, const uint8_t *end, uint64_t *value)
{
    if ((size_t)(end - *cursor) < 8U) return false;
    *value = 0U;
    for (unsigned index = 0; index < 8U; ++index) {
        *value = (*value << 8) | *(*cursor)++;
    }
    return true;
}

static bool put_event(uint8_t **cursor, const uint8_t *end,
                      const falafel_event_t *event)
{
    uint8_t wire[FALAFEL_MAX_EVENT_SIZE];
    size_t size = 0U;
    if (!falafel_event_serialize(event, wire, sizeof(wire), &size) ||
        size > UINT16_MAX || (size_t)(end - *cursor) < size + 2U) {
        return false;
    }
    put_u16(cursor, (uint16_t)size);
    memcpy(*cursor, wire, size);
    *cursor += size;
    return true;
}

static bool take_event(const uint8_t **cursor, const uint8_t *end,
                       falafel_event_t *event)
{
    uint16_t size;
    if (!take_u16(cursor, end, &size) || (size_t)(end - *cursor) < size ||
        !falafel_event_deserialize(*cursor, size, event)) {
        return false;
    }
    *cursor += size;
    return true;
}

static bool encode_state(const falafel_app_t *app, uint32_t reserved_sequence,
                         uint8_t *buffer, size_t capacity, size_t *size)
{
    uint8_t *cursor = buffer + 18U;
    const uint8_t *end = buffer + capacity;

    if (capacity < 18U || app->queue_count > FALAFEL_APP_QUEUE_CAPACITY ||
        app->seen_count > FALAFEL_APP_SEEN_CAPACITY ||
        app->received_count > FALAFEL_APP_RECEIVED_CAPACITY) {
        return false;
    }
    *cursor++ = app->personality;
    put_u64(&cursor, app->personality_revision);
    put_u32(&cursor, app->personality_author);
    put_u32(&cursor, reserved_sequence);
    put_u16(&cursor, (uint16_t)app->queue_count);
    for (size_t index = 0; index < app->queue_count; ++index) {
        if (cursor >= end) return false;
        *cursor++ = (uint8_t)app->queue[index].state;
        if (cursor >= end) return false;
        *cursor++ = app->queue[index].submitted ? 1U : 0U;
        if (!put_event(&cursor, end, &app->queue[index].event)) return false;
    }
    put_u16(&cursor, (uint16_t)app->seen_count);
    for (size_t index = 0; index < app->seen_count; ++index) {
        put_u32(&cursor, app->seen_sources[index]);
        put_u64(&cursor, app->seen_ids[index]);
    }
    put_u16(&cursor, (uint16_t)app->received_count);
    for (size_t index = 0; index < app->received_count; ++index) {
        if (!put_event(&cursor, end, &app->received[index])) return false;
    }

    memcpy(buffer, STATE_MAGIC, 8U);
    uint8_t *header = buffer + 8U;
    put_u16(&header, STATE_VERSION);
    put_u32(&header, (uint32_t)(cursor - (buffer + 18U)));
    put_u32(&header, crc32_bytes(buffer + 18U,
                                (size_t)(cursor - (buffer + 18U))));
    *size = (size_t)(cursor - buffer);
    return true;
}

static desktop_state_result_t decode_state(const uint8_t *buffer, size_t size,
                                           falafel_app_t *app,
                                           uint32_t *reserved_sequence)
{
    const uint8_t *header = buffer + 8U;
    const uint8_t *cursor;
    const uint8_t *end;
    uint16_t version;
    uint32_t payload_size;
    uint32_t expected_crc;
    uint16_t count;
    falafel_app_t decoded = *app;

    decoded.queue_count = 0U;
    decoded.seen_count = 0U;
    decoded.received_count = 0U;
    if (size < 18U || memcmp(buffer, STATE_MAGIC, 8U) != 0 ||
        !take_u16(&header, buffer + size, &version)) {
        return DESKTOP_STATE_CORRUPT;
    }
    if (version != STATE_VERSION) return DESKTOP_STATE_UNSUPPORTED;
    if (!take_u32(&header, buffer + size, &payload_size) ||
        !take_u32(&header, buffer + size, &expected_crc) ||
        payload_size != size - 18U ||
        crc32_bytes(buffer + 18U, payload_size) != expected_crc) {
        return DESKTOP_STATE_CORRUPT;
    }
    cursor = buffer + 18U;
    end = buffer + size;
    if (!take_u8(&cursor, end, &decoded.personality) ||
        decoded.personality >= 3U ||
        !take_u64(&cursor, end, &decoded.personality_revision) ||
        !take_u32(&cursor, end, &decoded.personality_author) ||
        !take_u32(&cursor, end, reserved_sequence) ||
        !take_u16(&cursor, end, &count) ||
        count > FALAFEL_APP_QUEUE_CAPACITY) return DESKTOP_STATE_CORRUPT;
    decoded.queue_count = count;
    for (size_t index = 0; index < decoded.queue_count; ++index) {
        uint8_t state;
        uint8_t submitted;
        if (!take_u8(&cursor, end, &state) ||
            state < FALAFEL_MESSAGE_QUEUED ||
            state > FALAFEL_MESSAGE_DEVICE_RECEIVED ||
            !take_u8(&cursor, end, &submitted) || submitted > 1U ||
            !take_event(&cursor, end, &decoded.queue[index].event)) {
            return DESKTOP_STATE_CORRUPT;
        }
        decoded.queue[index].state = (falafel_message_state_t)state;
        decoded.queue[index].submitted = submitted != 0U;
    }
    if (!take_u16(&cursor, end, &count) || count > FALAFEL_APP_SEEN_CAPACITY)
        return DESKTOP_STATE_CORRUPT;
    decoded.seen_count = count;
    for (size_t index = 0; index < decoded.seen_count; ++index) {
        if (!take_u32(&cursor, end, &decoded.seen_sources[index]) ||
            !take_u64(&cursor, end, &decoded.seen_ids[index]))
            return DESKTOP_STATE_CORRUPT;
    }
    if (!take_u16(&cursor, end, &count) || count > FALAFEL_APP_RECEIVED_CAPACITY)
        return DESKTOP_STATE_CORRUPT;
    decoded.received_count = count;
    for (size_t index = 0; index < decoded.received_count; ++index) {
        if (!take_event(&cursor, end, &decoded.received[index]) ||
            decoded.received[index].type != FALAFEL_EVENT_TEXT)
            return DESKTOP_STATE_CORRUPT;
    }
    if (cursor != end) return DESKTOP_STATE_CORRUPT;
    *app = decoded;
    return DESKTOP_STATE_OK;
}

desktop_state_result_t desktop_state_load(const char *path, falafel_app_t *app,
                                          uint32_t *reserved_sequence)
{
    uint8_t buffer[STATE_BUFFER_CAPACITY];
    FILE *stream = fopen(path, "rb");
    size_t size;

    if (stream == NULL) {
        return errno == ENOENT ? DESKTOP_STATE_NOT_FOUND : DESKTOP_STATE_IO_ERROR;
    }
    size = fread(buffer, 1U, sizeof(buffer), stream);
    if (ferror(stream) || !feof(stream) || fclose(stream) != 0) {
        return DESKTOP_STATE_IO_ERROR;
    }
    return decode_state(buffer, size, app, reserved_sequence);
}

bool desktop_state_save(const char *path, const falafel_app_t *app,
                        uint32_t reserved_sequence)
{
    uint8_t buffer[STATE_BUFFER_CAPACITY];
    size_t size;
    char temporary[512];
    FILE *stream;

    if (!encode_state(app, reserved_sequence, buffer, sizeof(buffer), &size) ||
        snprintf(temporary, sizeof(temporary), "%s.tmp", path) >=
            (int)sizeof(temporary)) {
        return false;
    }
    stream = fopen(temporary, "wb");
    if (stream == NULL) return false;
    bool written = fwrite(buffer, 1U, size, stream) == size &&
                   fflush(stream) == 0 && fsync(fileno(stream)) == 0;
    if (fclose(stream) != 0) written = false;
    if (!written || rename(temporary, path) != 0) {
        unlink(temporary);
        return false;
    }
    return true;
}

bool desktop_allocate_event_id(const char *path, const falafel_app_t *app,
                               uint32_t device_id, uint32_t *next_sequence,
                               uint32_t *reserved_sequence,
                               uint64_t *event_id)
{
    if (path == NULL || app == NULL || device_id == 0U ||
        next_sequence == NULL || reserved_sequence == NULL || event_id == NULL ||
        *next_sequence > *reserved_sequence) {
        return false;
    }
    if (*next_sequence == *reserved_sequence) {
        if (*reserved_sequence > UINT32_MAX - ID_RESERVATION_SIZE) {
            return false;
        }
        uint32_t new_reservation = *reserved_sequence + ID_RESERVATION_SIZE;
        if (!desktop_state_save(path, app, new_reservation)) {
            return false;
        }
        *reserved_sequence = new_reservation;
    }
    ++*next_sequence;
    *event_id = ((uint64_t)device_id << 32) | *next_sequence;
    return true;
}
