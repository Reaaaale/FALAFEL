#include "message_catalog.h"

#include <stddef.h>
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))


typedef struct {
    const char *const *messages;
    size_t count;
} personality_catalog_t;

static const char *const cuoco_messages[] = {
    "Benvenuto, Cuoco!",
    "La tua cucina è pronta.",
    "Buon appetito!"
};

static const char *const taxi_messages[] = {
    "Benvenuto, Taxi!",
    "La tua corsa è pronta.",
    "Buon viaggio!"
};

static const char *const promemoria_messages[] = {
    "Benvenuto, Promemoria!",
    "Hai nuovi promemoria.",
    "Non dimenticare le tue attività!"
};

static const personality_catalog_t catalogs[PERSONALITY_COUNT] = {
        {cuoco_messages, ARRAY_SIZE(cuoco_messages)},
        {taxi_messages, ARRAY_SIZE(taxi_messages)},
        {promemoria_messages, ARRAY_SIZE(promemoria_messages)}
    };


size_t catalog_count(personality_t personality) {
    if ((unsigned int)personality < 0 || (unsigned int)personality >= PERSONALITY_COUNT) {
        return 0; // Invalid personality
    }
    return catalogs[personality].count;
}

const char *catalog_get_message(personality_t personality, size_t index) {
    if ((unsigned int)personality < 0 || (unsigned int)personality >= PERSONALITY_COUNT) {
        return NULL; // Invalid personality
    }
    if (index >= catalogs[personality].count) {
        return NULL; // Index out of bounds
    }
    return catalogs[personality].messages[index];
}