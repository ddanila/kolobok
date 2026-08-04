#include "music.h"

#include <assert.h>
#include <stdio.h>

typedef struct MockOPL { unsigned writes,key_on,key_off;u32 hash;u8 voices; } MockOPL;

static void sink(u8 reg,u8 value,void*context)
{
    MockOPL*m=(MockOPL*)context;++m->writes;m->hash=(m->hash^reg)*16777619UL;m->hash=(m->hash^value)*16777619UL;
    if(reg>=0xb0&&reg<=0xb5){if(value&0x20){++m->key_on;m->voices|=(u8)(1U<<(reg-0xb0));}else ++m->key_off;}
}

static u32 trace_track(MockOPL*mock,unsigned track)
{
    unsigned i;mock->writes=mock->key_on=mock->key_off=0;mock->hash=2166136261UL;mock->voices=0;
    music_play(track);for(i=0;i<192;++i)music_tick();
    assert(music_debug_ticks()==192&&music_debug_events()>40&&music_debug_voice_mask()==0x3f);
    assert(mock->writes>200&&mock->key_on>40&&mock->key_off>40&&mock->voices==0x3f);return mock->hash;
}

int main(void)
{
    MockOPL mock={0,0,0,0,0};u32 hashes[4];unsigned i,j;music_set_sink(sink,&mock);
    assert(music_init(1));assert(music_is_detected()&&music_is_enabled());
    hashes[0]=trace_track(&mock,MUSIC_GARDEN);hashes[1]=trace_track(&mock,MUSIC_FOREST);
    hashes[2]=trace_track(&mock,MUSIC_DEEP);hashes[3]=trace_track(&mock,MUSIC_HOME);
    for(i=0;i<4;++i)for(j=0;j<i;++j)assert(hashes[i]!=hashes[j]);
    music_set_enabled(0);assert(!music_is_enabled());music_tick();assert(music_debug_ticks()==192);
    music_set_enabled(1);assert(music_is_enabled());music_shutdown();puts("host AdLib arrangement and sequencer tests: PASS");return 0;
}
