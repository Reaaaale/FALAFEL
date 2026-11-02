//gestione tempi attesa per la riconnessione WIFI 

//Task: se perdo connessione devo riconnettermi automaticamente, con un tempo di attesa crescente (esponenziale) tra un tentativo e l'altro, deve raddoppiare fino a 16000ms


#ifndef FALAFEL_RECONNECT_H
#define FALAFEL_RECONNECT_H
#include <stdint.h>


#define FALAFEL_RECONNECT_MAX_DELAY_MS 16000u
//#define FALAFEL_RECONNECT_INITIAL_DELAY_MS 1000u
//#define UINT32_MAX 0xFFFFFFFFu

typedef struct {
    uint32_t attempts;
    uint32_t initial_delay_ms; // Initial delay in milliseconds
    uint32_t max_delay_ms;     // Maximum delay in milliseconds
    uint32_t current_delay_ms; // Current delay in milliseconds
} falafel_reconnect_t;

int falafel_reconnect_init(falafel_reconnect_t *reconnect, uint32_t initial_delay_ms, uint32_t max_delay_ms);

int falafel_reconnect_next_delay(falafel_reconnect_t *reconnect, uint32_t *delay_ms);

int falafel_reconnect_reset(falafel_reconnect_t *reconnect);

#endif // FALAFEL_RECONNECT_H