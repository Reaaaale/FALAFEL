#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "desktop_transport.h"
static bool id(void *p,uint64_t *v){(void)p;*v=7;return true;}
static bool now(void *p,uint64_t *v){(void)p;*v=9;return true;}
int main(void){const char *path="/tmp/falafel-state-test.bin";falafel_app_config_t c={id,now,NULL,NULL,1,2};falafel_app_t a,b;assert(falafel_app_init(&a,&c)==FALAFEL_APP_OK);assert(falafel_app_create_text(&a,"persist",NULL)==FALAFEL_APP_OK);a.personality=2;a.personality_revision=4;a.personality_author=2;a.seen_count=1;a.seen_ids[0]=55;a.seen_sources[0]=2;assert(desktop_state_save(path,&a));assert(falafel_app_init(&b,&c)==FALAFEL_APP_OK);assert(desktop_state_load(path,&b));assert(b.personality==2&&b.personality_revision==4&&b.queue_count==1&&b.seen_ids[0]==55);remove(path);puts("Desktop persistence test passed.");return 0;}
