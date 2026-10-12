#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include "falafel_protocol.h"


int main (void)
{
    falafel_message_t message;

    const char *text ="Ciao fino";

    bool result = falafel_message_init(&message, 1, 1234567890, FALAFEL_MSG_TXT, (const uint8_t *)text, (uint16_t)strlen(text));

    bool result_null = falafel_message_init(
    NULL,
    1,
    123456789,
    FALAFEL_MSG_TXT,
    (const uint8_t *)"Ciao",
    4
);


    assert(result == true);
    assert(result_null == false);

    assert(message.id == 1);
    assert(message.timestamp == 1234567890);
    assert(message.type == FALAFEL_MSG_TXT);
    assert(message.payload_length == strlen(text));

    assert(memcmp(message.payload, text, strlen(text)) == 0);


    // Test serialization
    uint8_t buffer[FALAFEL_MAX_MESSAGE_SIZE];
    size_t serialized_size = 0;

    bool serialize_result = falafel_message_serialize(&message, buffer, sizeof(buffer), &serialized_size);

    assert(serialize_result == true);

    assert (serialized_size == FALAFEL_HEADER_SIZE + message.payload_length);

    // Check ID
    assert(buffer[0] == 0x00);
    assert(buffer[1] == 0x00);
    assert(buffer[2] == 0x00);
    assert(buffer[3] == 0x00);
    assert(buffer[4] == 0x00);
    assert(buffer[5] == 0x00);
    assert(buffer[6] == 0x00);
    assert(buffer[7] == 0x01);

    // Check timestamp
    assert(buffer[8]  == 0x00);
    assert(buffer[9]  == 0x00);
    assert(buffer[10] == 0x00);
    assert(buffer[11] == 0x00);
    assert(buffer[12] == 0x49);
    assert(buffer[13] == 0x96);
    assert(buffer[14] == 0x02);
    assert(buffer[15] == 0xD2);

    // Check type
    assert(buffer[16] == 0x00);

    // Check payload length
    assert(buffer[17] == 0x00);
    assert(buffer[18] == 0x09);

    // Check payload
    assert(memcmp(buffer + FALAFEL_HEADER_SIZE, text, strlen(text)) == 0);

    // print the serialized message in hex format

    printf("Serialized message: ");
    for (size_t i=0; i < serialized_size; ++i) {
        printf("%02X ", buffer[i]);
    }
    printf("\n");

    printf("=====================\n");
    printf(" DESERIALIZE TEST\n");

    // Test deserialization
falafel_message_t received_message;

bool deserialize_result = falafel_message_deserialize(
    buffer,
    serialized_size,
    &received_message
);

assert(deserialize_result == true);

assert(received_message.id == message.id);
assert(received_message.timestamp == message.timestamp);
assert(received_message.type == message.type);
assert(received_message.payload_length == message.payload_length);

assert(memcmp(
    received_message.payload,
    message.payload,
    message.payload_length
) == 0);

// print the deserialized message
printf("Deserialized message:\n");
printf("ID: %llu\n", received_message.id);
printf("Timestamp: %llu\n", received_message.timestamp);
printf("Type: %d\n", received_message.type);
printf("Payload length: %d\n", received_message.payload_length);
printf("Payload: %.*s\n", received_message.payload_length, received_message.payload);





printf("All tests passed successfully.\n");

return 0;

}