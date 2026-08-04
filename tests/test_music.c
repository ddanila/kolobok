#include "music.h"

#include <assert.h>
#include <stdio.h>

typedef struct MockOPL { unsigned writes, key_on, key_off; u8 last_reg, last_value; } MockOPL;

static void sink(u8 reg,u8 value,void*context)
{
    MockOPL*m=(MockOPL*)context;++m->writes;m->last_reg=reg;m->last_value=value;
    if(reg>=0xb0&&reg<=0xb8){if(value&0x20)++m->key_on;else ++m->key_off;}
}

int main(void)
{
    MockOPL mock={0,0,0,0,0};unsigned i;music_set_sink(sink,&mock);
    assert(music_init(1));assert(music_is_detected()&&music_is_enabled());
    music_play(MUSIC_GARDEN);for(i=0;i<120;++i)music_tick();
    assert(mock.writes>50&&mock.key_on>4&&mock.key_off>4);
    music_set_enabled(0);assert(!music_is_enabled());music_tick();
    music_set_enabled(1);music_play(MUSIC_DEEP);for(i=0;i<30;++i)music_tick();assert(mock.key_on>5);
    music_shutdown();puts("host AdLib sequencer tests: PASS");return 0;
}
