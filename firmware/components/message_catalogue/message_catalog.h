// modulo indipendente dall'hardware che gestisca il catalogo dei messaggi, rendendolo utilizzabile sia dal simulatore desktop sia dal futuro firmware ESP32-S3.


//supporto per 3 personalità diverse
// 3 messaggi preimpostati per ogni personalità memorizzati in strutture statiche

#ifndef MESSAGE_CATALOG_H
#define MESSAGE_CATALOG_H
//#include <stdint.h>
#include <stddef.h>
//#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

typedef enum {
    CUOCO,
    TAXI,
    PROMEMORIA,
    PERSONALITY_COUNT //contare il numero di personalità
} personality_t;

size_t catalog_count(personality_t personality);
const char *catalog_get_message(personality_t personality, size_t index);

#endif // MESSAGE_CATALOG_H

