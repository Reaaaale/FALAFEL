#include "falafel_protocol.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define EVENT_MAGIC_0 0x46U
#define EVENT_MAGIC_1 0x4CU
#define EVENT_VERSION 1U
static void write_u32(uint8_t *p, uint32_t v) { for (int i=3;i>=0;--i) *p++=(uint8_t)(v>>(i*8)); }
static void write_u64(uint8_t *p, uint64_t v) { for (int i=7;i>=0;--i) *p++=(uint8_t)(v>>(i*8)); }
static uint32_t read_u32(const uint8_t *p) { uint32_t v=0; for(int i=0;i<4;++i)v=(v<<8)|p[i]; return v; }
static uint64_t read_u64(const uint8_t *p) { uint64_t v=0; for(int i=0;i<8;++i)v=(v<<8)|p[i]; return v; }

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
    if (buffer_size < (size_t)(FALAFEL_HEADER_SIZE + message->payload_length)) return false;

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
    if (buffer_size != (size_t)(FALAFEL_HEADER_SIZE + temp.payload_length))
        return false;

    // 10. Copy payload
    if (temp.payload_length > 0) {
        memcpy(temp.payload, buffer + offset, temp.payload_length);
    }

    // 11. Copy validated message to destination
    *message = temp;

    return true;
}

bool falafel_event_is_valid(const falafel_event_t *e)
{
    if (!e || !e->source_id || !e->destination_id || e->source_id==e->destination_id ||
        !e->id || e->payload_length>FALAFEL_MAX_PAYLOAD_SIZE) return false;
    if (e->type==FALAFEL_EVENT_TEXT) return e->payload_length>0 && e->ack_phase==FALAFEL_ACK_NONE;
    if (e->type==FALAFEL_EVENT_PERSONALITY) return !e->payload_length && e->personality<3 && e->revision && e->ack_phase==FALAFEL_ACK_NONE;
    if (e->type==FALAFEL_EVENT_ACK) return !e->payload_length && (e->ack_phase==FALAFEL_ACK_RELAY_ACCEPTED || e->ack_phase==FALAFEL_ACK_DEVICE_RECEIVED);
    return false;
}
bool falafel_event_serialize(const falafel_event_t *e,uint8_t *b,size_t n,size_t *out)
{
    if(!out)return false;
    *out=0;
    if(!falafel_event_is_valid(e)||!b||n<FALAFEL_EVENT_WIRE_HEADER_SIZE+e->payload_length)return false;
    b[0]=EVENT_MAGIC_0;b[1]=EVENT_MAGIC_1;b[2]=EVENT_VERSION;b[3]=(uint8_t)e->type;
    write_u32(b+4,e->source_id);write_u32(b+8,e->destination_id);write_u64(b+12,e->id);
    write_u64(b+20,e->timestamp);write_u64(b+28,e->revision);b[36]=e->personality;
    b[37]=(uint8_t)e->ack_phase;b[38]=(uint8_t)(e->payload_length>>8);b[39]=(uint8_t)e->payload_length;
    if(e->payload_length)memcpy(b+FALAFEL_EVENT_WIRE_HEADER_SIZE,e->payload,e->payload_length);
    *out=FALAFEL_EVENT_WIRE_HEADER_SIZE+e->payload_length; return true;
}
bool falafel_event_deserialize(const uint8_t *b,size_t n,falafel_event_t *e)
{
    if(!b||!e||n<FALAFEL_EVENT_WIRE_HEADER_SIZE||b[0]!=EVENT_MAGIC_0||b[1]!=EVENT_MAGIC_1||b[2]!=EVENT_VERSION)return false;
    falafel_event_t t={0};t.type=(falafel_event_type_t)b[3];t.source_id=read_u32(b+4);t.destination_id=read_u32(b+8);
    t.id=read_u64(b+12);t.timestamp=read_u64(b+20);t.revision=read_u64(b+28);t.personality=b[36];t.ack_phase=(falafel_ack_phase_t)b[37];
    t.payload_length=(uint16_t)(((uint16_t)b[38]<<8)|b[39]);
    if(n!=FALAFEL_EVENT_WIRE_HEADER_SIZE+t.payload_length||!falafel_event_is_valid(&t))return false;
    if(t.payload_length)memcpy(t.payload,b+FALAFEL_EVENT_WIRE_HEADER_SIZE,t.payload_length);
    *e=t;
    return true;
}
