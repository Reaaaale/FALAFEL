#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "falafel_connection.h"
#include "falafel_reconnect.h"

/*
 * EMB-002 — Connection State Machine Unit Tests
 *
 * T01 — Valid initialization
 * T02 — NULL pointer initialization
 * T03 — Invalid initialization parameters
 * T04 — DISCONNECTED -> CONNECTING
 * T05 — CONNECTING -> CONNECTED
 * T06 — CONNECTING -> WAITING_RETRY
 * T07 — WAITING_RETRY -> CONNECTING
 * T08 — CONNECTED -> DISCONNECTED
 * T09 — Exponential backoff
 * T10 — Backoff reset after success
 * T11 — Invalid events
 * T12 — NULL pointer event handling
 * T13 — Complete connection lifecycle
 */

/* T01 — Valid initialization */
static void test_init_valid(void)
{
    falafel_connection_t connection;

    int result = falafel_connection_init(&connection, 1000, 16000);

    assert(result == 0);
    assert(connection.state == FALAFEL_CONNECTION_DISCONNECTED);
    assert(connection.retry_delay_ms == 0);

    assert(connection.reconnect.attempts == 0);
    assert(connection.reconnect.initial_delay_ms == 1000);
    assert(connection.reconnect.max_delay_ms == 16000);
    assert(connection.reconnect.current_delay_ms == 1000);
}

/* T02 — NULL pointer initialization */
static void test_init_null_pointer(void)
{
    int result = falafel_connection_init(NULL, 1000, 16000);

    assert(result == -1);
}

/* T03 — Invalid initialization parameters */
static void test_init_invalid_parameters(void)
{
    falafel_connection_t connection;

    int result = falafel_connection_init(&connection, 0, 16000);
    assert(result == -1);

    result = falafel_connection_init(&connection, 20000, 16000);
    assert(result == -1);

    result = falafel_connection_init(&connection, 1000, 0);
    assert(result == -1);
}

/* T04 — DISCONNECTED -> CONNECTING */
static void test_connect_from_disconnected(void)
{
    falafel_connection_t connection;

    assert(falafel_connection_init(&connection, 1000, 16000) == 0);

    int result = falafel_connection_handle_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_CONNECT
    );

    assert(result == 0);
    assert(connection.state == FALAFEL_CONNECTION_CONNECTING);
    assert(connection.retry_delay_ms == 0);
    assert(connection.reconnect.attempts == 0);
}

/* T05 — CONNECTING -> CONNECTED */
static void test_connect_success(void)
{
    falafel_connection_t connection;

    assert(falafel_connection_init(&connection, 1000, 16000) == 0);

    assert(falafel_connection_handle_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_CONNECT
    ) == 0);

    int result = falafel_connection_handle_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_SUCCESS
    );

    assert(result == 0);
    assert(connection.state == FALAFEL_CONNECTION_CONNECTED);
    assert(connection.retry_delay_ms == 0);
    assert(connection.reconnect.attempts == 0);
    assert(connection.reconnect.current_delay_ms == 1000);
}

/* T06 — CONNECTING -> WAITING_RETRY */
static void test_connect_failure(void)
{
    falafel_connection_t connection;

    assert(falafel_connection_init(&connection, 1000, 16000) == 0);

    assert(falafel_connection_handle_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_CONNECT
    ) == 0);

    int result = falafel_connection_handle_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_FAILURE
    );

    assert(result == 0);
    assert(connection.state == FALAFEL_CONNECTION_WAITING_RETRY);
    assert(connection.retry_delay_ms == 1000);
    assert(connection.reconnect.attempts == 1);
    assert(connection.reconnect.current_delay_ms == 2000);
}

/* T07 — WAITING_RETRY -> CONNECTING */
static void test_connect_failure_timeout(void)
{
    falafel_connection_t connection;

    assert(falafel_connection_init(&connection, 1000, 16000) == 0);

    assert(falafel_connection_handle_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_CONNECT
    ) == 0);

    assert(falafel_connection_handle_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_FAILURE
    ) == 0);

    assert(connection.state == FALAFEL_CONNECTION_WAITING_RETRY);
    assert(connection.retry_delay_ms == 1000);

    int result = falafel_connection_handle_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_TIMEOUT
    );

    assert(result == 0);
    assert(connection.state == FALAFEL_CONNECTION_CONNECTING);
    assert(connection.retry_delay_ms == 0);
    assert(connection.reconnect.attempts == 1);
    assert(connection.reconnect.current_delay_ms == 2000);
}

/* T08 — CONNECTED -> DISCONNECTED */
static void test_connect_success_disconnect(void)
{
    falafel_connection_t connection;

    assert(falafel_connection_init(&connection, 1000, 16000) == 0);

    assert(falafel_connection_handle_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_CONNECT
    ) == 0);

    assert(falafel_connection_handle_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_SUCCESS
    ) == 0);

    int result = falafel_connection_handle_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_DISCONNECT
    );

    assert(result == 0);
    assert(connection.state == FALAFEL_CONNECTION_DISCONNECTED);
    assert(connection.retry_delay_ms == 0);
    assert(connection.reconnect.attempts == 0);
    assert(connection.reconnect.current_delay_ms == 1000);
}

/* T09 — Exponential backoff */
static void test_exponential_backoff(void)
{
    falafel_connection_t connection;

    assert(falafel_connection_init(&connection, 1000, 16000) == 0);

    assert(falafel_connection_handle_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_CONNECT
    ) == 0);

    const uint32_t expected_delays[] = {
        1000, 2000, 4000, 8000, 16000, 16000
    };

    for (uint32_t i = 0; i < 6; i++) {

        assert(falafel_connection_handle_event(
            &connection,
            FALAFEL_CONNECTION_EVENT_FAILURE
        ) == 0);

        assert(connection.state == FALAFEL_CONNECTION_WAITING_RETRY);
        assert(connection.retry_delay_ms == expected_delays[i]);
        assert(connection.reconnect.attempts == i + 1);

        assert(falafel_connection_handle_event(
            &connection,
            FALAFEL_CONNECTION_EVENT_TIMEOUT
        ) == 0);

        assert(connection.state == FALAFEL_CONNECTION_CONNECTING);
        assert(connection.retry_delay_ms == 0);
    }

    assert(connection.reconnect.current_delay_ms == 16000);
}

/* T10 — Backoff reset after success */
static void test_backoff_reset_after_success(void)
{
    falafel_connection_t connection;

    assert(falafel_connection_init(&connection, 1000, 16000) == 0);

    assert(falafel_connection_handle_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_CONNECT
    ) == 0);

    /* First failure */
    assert(falafel_connection_handle_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_FAILURE
    ) == 0);

    assert(connection.retry_delay_ms == 1000);

    /* Retry */
    assert(falafel_connection_handle_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_TIMEOUT
    ) == 0);

    /* Second failure */
    assert(falafel_connection_handle_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_FAILURE
    ) == 0);

    assert(connection.retry_delay_ms == 2000);
    assert(connection.reconnect.attempts == 2);

    /* Retry and successful connection */
    assert(falafel_connection_handle_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_TIMEOUT
    ) == 0);

    assert(falafel_connection_handle_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_SUCCESS
    ) == 0);

    assert(connection.state == FALAFEL_CONNECTION_CONNECTED);
    assert(connection.retry_delay_ms == 0);
    assert(connection.reconnect.attempts == 0);
    assert(connection.reconnect.current_delay_ms == 1000);
}

/* Helper: verify that an invalid event does not modify the connection */
static void assert_invalid_event(
    falafel_connection_t *connection,
    falafel_connection_event_t event
)
{
    falafel_connection_t before = *connection;

    int result = falafel_connection_handle_event(connection, event);

    assert(result == -1);

    assert(connection->state == before.state);
    assert(connection->retry_delay_ms == before.retry_delay_ms);

    assert(connection->reconnect.attempts ==
           before.reconnect.attempts);

    assert(connection->reconnect.initial_delay_ms ==
           before.reconnect.initial_delay_ms);

    assert(connection->reconnect.current_delay_ms ==
           before.reconnect.current_delay_ms);

    assert(connection->reconnect.max_delay_ms ==
           before.reconnect.max_delay_ms);
}

/* T11 — Invalid events */
static void test_invalid_events(void)
{
    falafel_connection_t connection;

    assert(falafel_connection_init(&connection, 1000, 16000) == 0);

    /* DISCONNECTED: only CONNECT is valid */
    assert_invalid_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_SUCCESS
    );

    assert_invalid_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_FAILURE
    );

    assert_invalid_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_TIMEOUT
    );

    assert_invalid_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_DISCONNECT
    );

    /* Enter CONNECTING */
    assert(falafel_connection_handle_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_CONNECT
    ) == 0);

    /* CONNECTING: only SUCCESS and FAILURE are valid */
    assert_invalid_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_CONNECT
    );

    assert_invalid_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_TIMEOUT
    );

    assert_invalid_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_DISCONNECT
    );

    /* Enter WAITING_RETRY */
    assert(falafel_connection_handle_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_FAILURE
    ) == 0);

    /* WAITING_RETRY: only TIMEOUT is valid */
    assert_invalid_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_CONNECT
    );

    assert_invalid_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_SUCCESS
    );

    assert_invalid_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_FAILURE
    );

    assert_invalid_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_DISCONNECT
    );

    /* Return to CONNECTING */
    assert(falafel_connection_handle_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_TIMEOUT
    ) == 0);

    /* Enter CONNECTED */
    assert(falafel_connection_handle_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_SUCCESS
    ) == 0);

    /* CONNECTED: only DISCONNECT is valid */
    assert_invalid_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_CONNECT
    );

    assert_invalid_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_SUCCESS
    );

    assert_invalid_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_FAILURE
    );

    assert_invalid_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_TIMEOUT
    );
}

/* T12 — NULL pointer event handling */
static void test_handle_event_null_pointer(void)
{
    assert(falafel_connection_handle_event(
        NULL,
        FALAFEL_CONNECTION_EVENT_CONNECT
    ) == -1);
}

/* T13 — Complete connection lifecycle */
static void test_complete_connection_lifecycle(void)
{
    falafel_connection_t connection;

    assert(falafel_connection_init(&connection, 1000, 16000) == 0);

    /* Start connection */
    assert(falafel_connection_handle_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_CONNECT
    ) == 0);

    assert(connection.state == FALAFEL_CONNECTION_CONNECTING);

    /* First failure */
    assert(falafel_connection_handle_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_FAILURE
    ) == 0);

    assert(connection.state == FALAFEL_CONNECTION_WAITING_RETRY);
    assert(connection.retry_delay_ms == 1000);
    assert(connection.reconnect.attempts == 1);

    /* First retry */
    assert(falafel_connection_handle_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_TIMEOUT
    ) == 0);

    assert(connection.state == FALAFEL_CONNECTION_CONNECTING);

    /* Second failure */
    assert(falafel_connection_handle_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_FAILURE
    ) == 0);

    assert(connection.state == FALAFEL_CONNECTION_WAITING_RETRY);
    assert(connection.retry_delay_ms == 2000);
    assert(connection.reconnect.attempts == 2);

    /* Second retry */
    assert(falafel_connection_handle_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_TIMEOUT
    ) == 0);

    /* Connection succeeds */
    assert(falafel_connection_handle_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_SUCCESS
    ) == 0);

    assert(connection.state == FALAFEL_CONNECTION_CONNECTED);
    assert(connection.retry_delay_ms == 0);
    assert(connection.reconnect.attempts == 0);
    assert(connection.reconnect.current_delay_ms == 1000);

    /* Disconnect */
    assert(falafel_connection_handle_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_DISCONNECT
    ) == 0);

    assert(connection.state == FALAFEL_CONNECTION_DISCONNECTED);
    assert(connection.retry_delay_ms == 0);
    assert(connection.reconnect.attempts == 0);

    /* Start a new connection */
    assert(falafel_connection_handle_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_CONNECT
    ) == 0);

    /* New failure must restart backoff from 1000 ms */
    assert(falafel_connection_handle_event(
        &connection,
        FALAFEL_CONNECTION_EVENT_FAILURE
    ) == 0);

    assert(connection.retry_delay_ms == 1000);
    assert(connection.reconnect.attempts == 1);
}

int main(void)
{
    test_init_valid();
    test_init_null_pointer();
    test_init_invalid_parameters();

    test_connect_from_disconnected();
    test_connect_success();
    test_connect_failure();
    test_connect_failure_timeout();
    test_connect_success_disconnect();

    test_exponential_backoff();
    test_backoff_reset_after_success();

    test_invalid_events();
    test_handle_event_null_pointer();

    test_complete_connection_lifecycle();

    printf("EMB-002: All 13 connection tests passed!\n");

    return 0;
}