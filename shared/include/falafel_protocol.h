#ifndef FALAFEL_PROTOCOL_H
#define FALAFEL_PROTOCOL_H
#include <stdint.h>
#include <stdbool.h>
//#include <string.h>
#include <stddef.h>

#define FALAFEL_MAX_PAYLOAD_SIZE 256
#define FALAFEL_HEADER_SIZE 19
#define FALAFEL_MAX_MESSAGE_SIZE (FALAFEL_HEADER_SIZE + FALAFEL_MAX_PAYLOAD_SIZE)

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
#endif // FALAFEL_PROTOCOL_H



