#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "falafel_protocol.h"

static void fill_payload(uint8_t *payload, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        payload[i] = (uint8_t)i;
    }
}

static void assert_messages_equal(
    const falafel_message_t *expected,
    const falafel_message_t *actual
)
{
    assert(actual->id == expected->id);
    assert(actual->timestamp == expected->timestamp);
    assert(actual->type == expected->type);
    assert(actual->payload_length == expected->payload_length);
    assert(memcmp(actual->payload, expected->payload, expected->payload_length) == 0);
}

static void assert_deserialization_failure_preserves_message(
    const uint8_t *buffer,
    size_t buffer_size
)
{
    falafel_message_t destination;
    memset(&destination, 0xA5, sizeof(destination));
    const falafel_message_t original = destination;

    assert(!falafel_message_deserialize(buffer, buffer_size, &destination));
    assert(memcmp(&destination, &original, sizeof(destination)) == 0);
}

static void test_message_init(void)
{
    const uint8_t payload[] = {'C', 'i', 'a', 'o'};
    uint8_t maximum_payload[FALAFEL_MAX_PAYLOAD_SIZE];
    falafel_message_t message;

    fill_payload(maximum_payload, sizeof(maximum_payload));

    assert(falafel_message_init(
        &message,
        UINT64_C(1),
        UINT64_C(1234567890),
        FALAFEL_MSG_TXT,
        payload,
        (uint16_t)sizeof(payload)
    ));
    assert(message.id == UINT64_C(1));
    assert(message.timestamp == UINT64_C(1234567890));
    assert(message.type == FALAFEL_MSG_TXT);
    assert(message.payload_length == sizeof(payload));
    assert(memcmp(message.payload, payload, sizeof(payload)) == 0);

    assert(!falafel_message_init(
        NULL, UINT64_C(1), UINT64_C(2), FALAFEL_MSG_TXT, payload,
        (uint16_t)sizeof(payload)
    ));
    assert(!falafel_message_init(
        &message, UINT64_C(1), UINT64_C(2), FALAFEL_MSG_TXT, NULL, UINT16_C(1)
    ));
    assert(falafel_message_init(
        &message, UINT64_C(1), UINT64_C(2), FALAFEL_MSG_TXT, NULL, UINT16_C(0)
    ));
    assert(message.payload_length == 0U);
    assert(!falafel_message_init(
        &message, UINT64_C(1), UINT64_C(2), (falafel_msg_type_t)99, payload,
        (uint16_t)sizeof(payload)
    ));
    assert(!falafel_message_init(
        &message, UINT64_C(1), UINT64_C(2), FALAFEL_MSG_TXT, maximum_payload,
        (uint16_t)(FALAFEL_MAX_PAYLOAD_SIZE + 1U)
    ));
    assert(falafel_message_init(
        &message, UINT64_C(1), UINT64_C(2), FALAFEL_MSG_ACK, maximum_payload,
        (uint16_t)sizeof(maximum_payload)
    ));
    assert(message.payload_length == FALAFEL_MAX_PAYLOAD_SIZE);
    assert(memcmp(message.payload, maximum_payload, sizeof(maximum_payload)) == 0);
}

static void test_serialization(void)
{
    const uint8_t payload[] = "Ciao fino";
    const uint8_t expected_header[FALAFEL_HEADER_SIZE] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x49, 0x96, 0x02, 0xD2,
        0x00,
        0x00, 0x09
    };
    falafel_message_t message;
    uint8_t buffer[FALAFEL_MAX_MESSAGE_SIZE];
    size_t serialized_size = SIZE_MAX;

    assert(falafel_message_init(
        &message,
        UINT64_C(1),
        UINT64_C(1234567890),
        FALAFEL_MSG_TXT,
        payload,
        UINT16_C(9)
    ));
    assert(falafel_message_serialize(
        &message, buffer, sizeof(buffer), &serialized_size
    ));
    assert(serialized_size == (size_t)(FALAFEL_HEADER_SIZE + 9U));
    assert(memcmp(buffer, expected_header, sizeof(expected_header)) == 0);
    assert(memcmp(buffer + FALAFEL_HEADER_SIZE, payload, 9U) == 0);

    assert(!falafel_message_serialize(&message, buffer, sizeof(buffer), NULL));
    assert(!falafel_message_serialize(NULL, buffer, sizeof(buffer), &serialized_size));
    assert(serialized_size == (size_t)(FALAFEL_HEADER_SIZE + 9U));
    assert(!falafel_message_serialize(&message, NULL, sizeof(buffer), &serialized_size));
    assert(serialized_size == (size_t)(FALAFEL_HEADER_SIZE + 9U));

    serialized_size = SIZE_MAX;
    assert(!falafel_message_serialize(
        &message,
        buffer,
        (size_t)(FALAFEL_HEADER_SIZE + message.payload_length - 1U),
        &serialized_size
    ));
    assert(serialized_size == 0U);

    message.type = (falafel_msg_type_t)99;
    serialized_size = SIZE_MAX;
    assert(!falafel_message_serialize(&message, buffer, sizeof(buffer), &serialized_size));
    assert(serialized_size == 0U);

    message.type = FALAFEL_MSG_TXT;
    message.payload_length = (uint16_t)(FALAFEL_MAX_PAYLOAD_SIZE + 1U);
    serialized_size = SIZE_MAX;
    assert(!falafel_message_serialize(&message, buffer, sizeof(buffer), &serialized_size));
    assert(serialized_size == 0U);
}

static void test_deserialization(void)
{
    const uint8_t payload[] = {0x00, 0x7F, 0x80, 0xFF};
    const uint8_t expected_prefix[] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
        0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10
    };
    uint8_t maximum_payload[FALAFEL_MAX_PAYLOAD_SIZE];
    uint8_t buffer[FALAFEL_MAX_MESSAGE_SIZE];
    falafel_message_t message;
    falafel_message_t decoded;
    size_t serialized_size = 0;

    assert(falafel_message_init(
        &message,
        UINT64_C(0x0123456789ABCDEF),
        UINT64_C(0xFEDCBA9876543210),
        FALAFEL_MSG_REMINDER,
        payload,
        (uint16_t)sizeof(payload)
    ));
    assert(falafel_message_serialize(
        &message, buffer, sizeof(buffer), &serialized_size
    ));
    assert(memcmp(buffer, expected_prefix, sizeof(expected_prefix)) == 0);
    assert(falafel_message_deserialize(buffer, serialized_size, &decoded));
    assert_messages_equal(&message, &decoded);

    assert(falafel_message_init(
        &message, UINT64_C(0), UINT64_C(0), FALAFEL_MSG_TXT, NULL, UINT16_C(0)
    ));
    assert(falafel_message_serialize(
        &message, buffer, sizeof(buffer), &serialized_size
    ));
    assert(serialized_size == FALAFEL_HEADER_SIZE);
    assert(falafel_message_deserialize(buffer, serialized_size, &decoded));
    assert_messages_equal(&message, &decoded);

    fill_payload(maximum_payload, sizeof(maximum_payload));
    assert(falafel_message_init(
        &message,
        UINT64_MAX,
        UINT64_MAX,
        FALAFEL_MSG_ACK,
        maximum_payload,
        (uint16_t)sizeof(maximum_payload)
    ));
    assert(falafel_message_serialize(
        &message, buffer, sizeof(buffer), &serialized_size
    ));
    assert(serialized_size == FALAFEL_MAX_MESSAGE_SIZE);
    assert(falafel_message_deserialize(buffer, serialized_size, &decoded));
    assert_messages_equal(&message, &decoded);
}

static void test_invalid_packets(void)
{
    uint8_t packet[FALAFEL_MAX_MESSAGE_SIZE] = {0};

    packet[16] = (uint8_t)FALAFEL_MSG_TXT;

    assert_deserialization_failure_preserves_message(NULL, FALAFEL_HEADER_SIZE);
    assert(!falafel_message_deserialize(packet, sizeof(packet), NULL));
    assert_deserialization_failure_preserves_message(packet, FALAFEL_HEADER_SIZE - 1U);

    packet[16] = 0xFF;
    assert_deserialization_failure_preserves_message(packet, FALAFEL_HEADER_SIZE);
    packet[16] = (uint8_t)FALAFEL_MSG_TXT;

    packet[17] = 0x01;
    packet[18] = 0x01;
    assert_deserialization_failure_preserves_message(packet, sizeof(packet));

    packet[17] = 0x00;
    packet[18] = 0x02;
    assert_deserialization_failure_preserves_message(packet, FALAFEL_HEADER_SIZE + 1U);

    packet[18] = 0x00;
    assert_deserialization_failure_preserves_message(packet, FALAFEL_HEADER_SIZE + 1U);

    packet[18] = 0x04;
    assert_deserialization_failure_preserves_message(packet, FALAFEL_HEADER_SIZE + 2U);
}

static void assert_payload_round_trip(uint16_t payload_length)
{
    uint8_t payload[FALAFEL_MAX_PAYLOAD_SIZE];
    uint8_t buffer[FALAFEL_MAX_MESSAGE_SIZE];
    falafel_message_t message;
    falafel_message_t decoded;
    size_t serialized_size = 0;
    const uint8_t *payload_pointer = payload_length == 0U ? NULL : payload;

    fill_payload(payload, sizeof(payload));
    if (payload_length > 1U) {
        payload[0] = 0x00;
        payload[payload_length - 1U] = 0xFF;
    }

    assert(falafel_message_init(
        &message,
        UINT64_C(42),
        UINT64_C(84),
        FALAFEL_MSG_TXT,
        payload_pointer,
        payload_length
    ));
    assert(falafel_message_serialize(
        &message, buffer, sizeof(buffer), &serialized_size
    ));
    assert(serialized_size == (size_t)(FALAFEL_HEADER_SIZE + payload_length));
    assert(falafel_message_deserialize(buffer, serialized_size, &decoded));
    assert_messages_equal(&message, &decoded);
}

static void test_payload_boundaries(void)
{
    const falafel_msg_type_t valid_types[] = {
        FALAFEL_MSG_TXT,
        FALAFEL_MSG_REMINDER,
        FALAFEL_MSG_ACK
    };
    const uint8_t binary_payload[] = {0x00, 0xFF, 0x00, 0xFF};
    uint8_t maximum_payload[FALAFEL_MAX_PAYLOAD_SIZE];
    uint8_t buffer[FALAFEL_MAX_MESSAGE_SIZE];
    falafel_message_t message;
    falafel_message_t decoded;
    size_t serialized_size = 0;

    assert_payload_round_trip(UINT16_C(0));
    assert_payload_round_trip(UINT16_C(1));
    assert_payload_round_trip(UINT16_C(255));
    assert_payload_round_trip(UINT16_C(256));

    fill_payload(maximum_payload, sizeof(maximum_payload));
    assert(!falafel_message_init(
        &message,
        UINT64_C(1),
        UINT64_C(2),
        FALAFEL_MSG_TXT,
        maximum_payload,
        UINT16_C(257)
    ));

    for (size_t i = 0; i < sizeof(valid_types) / sizeof(valid_types[0]); ++i) {
        assert(falafel_message_init(
            &message,
            UINT64_C(1),
            UINT64_C(2),
            valid_types[i],
            binary_payload,
            (uint16_t)sizeof(binary_payload)
        ));
        assert(message.type == valid_types[i]);
        assert(memcmp(message.payload, binary_payload, sizeof(binary_payload)) == 0);
        assert(falafel_message_serialize(
            &message, buffer, sizeof(buffer), &serialized_size
        ));
        assert(falafel_message_deserialize(buffer, serialized_size, &decoded));
        assert_messages_equal(&message, &decoded);
    }
}

static void run_all_tests(void)
{
    test_message_init();
    test_serialization();
    test_deserialization();
    test_invalid_packets();
    test_payload_boundaries();
}

int main(int argc, char **argv)
{
    if (argc == 1) {
        run_all_tests();
    } else if (argc == 2 && strcmp(argv[1], "init") == 0) {
        test_message_init();
    } else if (argc == 2 && strcmp(argv[1], "serialization") == 0) {
        test_serialization();
    } else if (argc == 2 && strcmp(argv[1], "deserialization") == 0) {
        test_deserialization();
    } else if (argc == 2 && strcmp(argv[1], "invalid-packets") == 0) {
        test_invalid_packets();
    } else if (argc == 2 && strcmp(argv[1], "payload-boundaries") == 0) {
        test_payload_boundaries();
    } else {
        fprintf(stderr, "Unknown test category\n");
        return 2;
    }

    puts("All selected protocol tests passed.");
    return 0;
}
