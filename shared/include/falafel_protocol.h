#ifndef FALAFEL_PROTOCOL_H
#define FALAFEL_PROTOCOL_H
#include <stdint.h>
#include <stdbool.h>
//#include <string.h>
#include <stddef.h>

#define FALAFEL_MAX_PAYLOAD_SIZE 256
#define FALAFEL_HEADER_SIZE 19
#define FALAFEL_MAX_MESSAGE_SIZE (FALAFEL_HEADER_SIZE + FALAFEL_MAX_PAYLOAD_SIZE)
#define FALAFEL_EVENT_WIRE_HEADER_SIZE 40U
#define FALAFEL_MAX_EVENT_SIZE (FALAFEL_EVENT_WIRE_HEADER_SIZE + FALAFEL_MAX_PAYLOAD_SIZE)
#define FALAFEL_RELAY_ID UINT32_MAX

typedef enum 
{
    FALAFEL_MSG_TXT =0,
    FALAFEL_MSG_REMINDER,
    FALAFEL_MSG_ACK
} falafel_msg_type_t;

typedef struct
{
    uint64_t id; 
    uint64_t timestamp;
    falafel_msg_type_t type; 
    uint8_t payload[FALAFEL_MAX_PAYLOAD_SIZE]; 
    uint16_t payload_length;
} falafel_message_t;

typedef enum { FALAFEL_EVENT_TEXT = 1, FALAFEL_EVENT_PERSONALITY = 2,
               FALAFEL_EVENT_ACK = 3 } falafel_event_type_t;
typedef enum { FALAFEL_ACK_NONE = 0, FALAFEL_ACK_RELAY_ACCEPTED = 1,
               FALAFEL_ACK_DEVICE_RECEIVED = 2 } falafel_ack_phase_t;
typedef struct {
    uint32_t source_id, destination_id;
    uint64_t id, timestamp, revision;
    falafel_event_type_t type;
    uint8_t personality;
    falafel_ack_phase_t ack_phase;
    uint16_t payload_length;
    uint8_t payload[FALAFEL_MAX_PAYLOAD_SIZE];
} falafel_event_t;


bool falafel_message_init(
    falafel_message_t *message,
    uint64_t id,
    uint64_t timestamp,
    falafel_msg_type_t type,
    const uint8_t *payload,
    uint16_t payload_length
);

bool falafel_message_serialize(
    const falafel_message_t *message,
    uint8_t *buffer,
    size_t buffer_size,
    size_t *serialized_size
);

bool falafel_message_deserialize(
    const uint8_t *buffer,
    size_t buffer_size,
    falafel_message_t *message
);
bool falafel_event_serialize(const falafel_event_t *, uint8_t *, size_t, size_t *);
bool falafel_event_deserialize(const uint8_t *, size_t, falafel_event_t *);
bool falafel_event_is_valid(const falafel_event_t *event);
#endif // FALAFEL_PROTOCOL_H



