#include <assert.h>
#include "message_catalog.h"


int main(void) {
    // Test catalog_count
    assert(catalog_count(CUOCO) == 3);
    assert(catalog_count(TAXI) == 3);
    assert(catalog_count(PROMEMORIA) == 3);
    assert(catalog_count((personality_t)3) == 0); // Invalid personality

    // Test catalog_get_message
    assert(catalog_get_message(CUOCO, 0) != NULL);
    #include <string.h>

    assert(strcmp(catalog_get_message(CUOCO, 0), "Benvenuto, Cuoco!") == 0);    
    assert(catalog_get_message(CUOCO, 1) != NULL);
    assert(catalog_get_message(CUOCO, 2) != NULL);
    assert(catalog_get_message(CUOCO, 3) == NULL); // Out of bounds
    assert(catalog_get_message(TAXI, 0) != NULL);
    assert(catalog_get_message(TAXI, 1) != NULL);
    assert(catalog_get_message(TAXI, 2) != NULL);
    assert(catalog_get_message(TAXI, 3) == NULL); // Out of bounds
    assert(catalog_get_message(PROMEMORIA, 0) != NULL);
    assert(catalog_get_message(PROMEMORIA, 1) != NULL);
    assert(catalog_get_message(PROMEMORIA, 2) != NULL);
    assert(catalog_get_message(PROMEMORIA, 3) == NULL); // Out of bounds
    assert(catalog_get_message((personality_t)3, 0) == NULL); // Invalid personality

    return 0;
}