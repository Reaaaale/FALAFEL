#include "falafel_protocol.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

//Initialize the message with default values

bool falafel_message_init (
    falafel_message_t *message,
    uint64_t id,
    uint64_t timestamp,
    falafel_msg_type_t type,
    const uint8_t *payload,
    uint16_t payload_length
)
{
if (message == NULL) return false;

//check payload length
if (payload_length > FALAFEL_MAX_PAYLOAD_SIZE) return false;

//sanity check for payload pointer
if (payload_length > 0 && payload == NULL) return false;

//check for valid message type
if ( type != FALAFEL_MSG_TXT && type != FALAFEL_MSG_REMINDER && type != FALAFEL_MSG_ACK) return false;

memset(message, 0, sizeof(*message));

message->id = id;
message->timestamp = timestamp;
message->type = type;
message->payload_length = payload_length;

if (payload_length > 0) {
    memcpy(message->payload, payload, payload_length);
}

return true;
}

bool falafel_message_serialize (
    const falafel_message_t *message,
    uint8_t *buffer,
    size_t buffer_size,
    size_t *serialized_size
)
{
    if (message == NULL || buffer == NULL || serialized_size == NULL) return false;

    *serialized_size = 0;

    //check for valid message type
    if ( message->type != FALAFEL_MSG_TXT && message->type != FALAFEL_MSG_REMINDER && message->type != FALAFEL_MSG_ACK) return false;

    //check payload length
    if (message->payload_length > FALAFEL_MAX_PAYLOAD_SIZE) return false;

    //check buffer size
    if (buffer_size < FALAFEL_HEADER_SIZE + message->payload_length) return false;

    //serialize the message into the buffer
    size_t offset = 0;
    for ( int i =7; i>=0; i--) {
        buffer[offset ++]= (uint8_t)(message->id >> (i * 8));
    }
   for ( int i =7; i>=0; i--) {
        buffer[offset ++]= (uint8_t)(message->timestamp >> (i * 8));
    }
    buffer[offset ++]= (uint8_t)(message->type);

    buffer[offset ++]= (uint8_t)(message->payload_length >> 8);
    buffer[offset ++] = (uint8_t)(message->payload_length & 0xFF);

    memcpy(buffer + offset, message->payload, message->payload_length);
    offset += message->payload_length;

    *serialized_size = offset;
    return true;
}

bool falafel_message_deserialize(
    const uint8_t *buffer,
    size_t buffer_size,
    falafel_message_t *message
)
{
    // 1. Check pointers
    if (buffer == NULL || message == NULL)
        return false;

    // 2. Check minimum packet size
    if (buffer_size < FALAFEL_HEADER_SIZE)
        return false;

    // 3. Create temporary message
    falafel_message_t temp = {0};

    size_t offset = 0;

    // 4. Deserialize ID
    for (int i = 0; i < 8; i++) {
        temp.id = (temp.id << 8) | buffer[offset++];
    }

    // 5. Deserialize timestamp
    for (int i = 0; i < 8; i++) {
        temp.timestamp = (temp.timestamp << 8) | buffer[offset++];
    }

    // 6. Deserialize type
    temp.type = (falafel_msg_type_t)buffer[offset++];

    if (temp.type != FALAFEL_MSG_TXT &&
        temp.type != FALAFEL_MSG_REMINDER &&
        temp.type != FALAFEL_MSG_ACK)
    {
        return false;
    }

    // 7. Deserialize payload length
    temp.payload_length =
        ((uint16_t)buffer[offset] << 8) |
        buffer[offset + 1];

    offset += 2;

    // 8. Check maximum payload length
    if (temp.payload_length > FALAFEL_MAX_PAYLOAD_SIZE)
        return false;

    // 9. Check exact packet size
    if (buffer_size != FALAFEL_HEADER_SIZE + temp.payload_length)
        return false;

    // 10. Copy payload
    if (temp.payload_length > 0) {
        memcpy(temp.payload, buffer + offset, temp.payload_length);
    }

    // 11. Copy validated message to destination
    *message = temp;

    return true;
}