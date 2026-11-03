#include "sec003_suite.h"
#include "sec003_probe.h"
#include <stdio.h>
#include <stdlib.h>

static void report(void *user, const char *name, bool ok)
{ (void)user; printf("SEC003_HOST CASE %s %s\n", name, ok ? "PASS" : "FAIL"); }
int main(void)
{
    falafel_crypto_context_t *context = calloc(1, sizeof(*context));
    if (!context) return 1;
    sec003_result result;
    sec003_hooks callbacks = {.case_result = report};
    bool ok = sec003_suite_run(context, &callbacks, &result);
    sec003_probe_stats stats = sec003_probe_get();
    printf("SEC003_HOST RESULT %s passed=%u failed=%u injected=%u hardware=NOT_RUN\n",
           ok ? "PASS" : "FAIL", result.passed, result.failed, (unsigned)stats.injected_failures);
    free(context);
    return ok ? 0 : 1;
}
