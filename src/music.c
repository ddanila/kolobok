#include "music.h"

#ifdef __WATCOMC__
#include <conio.h>
#endif

typedef struct NoteEvent { u8 delay, voice, note, duration; } NoteEvent;

/* The contour is transcribed from public-domain song 45 in Tchaikovsky's
 * 1869 collection. Harmony, bass movement, rhythm and OPL instruments are
 * original to this game. Notes are semitone offsets from C4. */
static const NoteEvent melody[] = {
    {0,0,7,5},{6,0,9,5},{6,0,11,5},{6,0,12,11},{12,0,11,5},{6,0,9,5},
    {6,0,7,11},{12,0,4,5},{6,0,7,5},{6,0,9,11},{12,0,7,11},
    {12,0,4,5},{6,0,2,5},{6,0,0,17},{18,0,255,1}
};

static const u16 fnums[12] = {343,363,385,408,432,458,485,514,544,577,611,647};
static MusicRegisterSink register_sink;
static void *sink_context;
static u8 enabled, detected, track_id, event_index, wait_ticks;
static u8 voice_off[6];

static void write_reg(u8 reg,u8 value)
{
    if(register_sink){register_sink(reg,value,sink_context);return;}
#ifdef __WATCOMC__
    outp(0x388,reg);{unsigned i;for(i=0;i<6;++i)(void)inp(0x388);}
    outp(0x389,value);{unsigned i;for(i=0;i<35;++i)(void)inp(0x388);}
#else
    (void)reg;(void)value;
#endif
}

static int detect_opl(void)
{
#ifdef __WATCOMC__
    unsigned i,a,b;
    write_reg(4,0x60);write_reg(4,0x80);a=inp(0x388)&0xe0;
    write_reg(2,0xff);write_reg(4,0x21);for(i=0;i<100;++i)(void)inp(0x388);
    b=inp(0x388)&0xe0;write_reg(4,0x60);write_reg(4,0x80);
    return a==0&&b==0xc0;
#else
    return register_sink!=0;
#endif
}

void music_set_sink(MusicRegisterSink sink,void*context){register_sink=sink;sink_context=context;}

int music_init(int requested)
{
    unsigned voice;
    detected=(u8)(requested&&detect_opl());enabled=detected;
    if(!detected)return 0;
    write_reg(1,0x20);
    for(voice=0;voice<6;++voice){
        static const u8 operators[6]={0,1,2,8,9,10};u8 op=operators[voice];
        write_reg(0x20+op,0x21);write_reg(0x23+op,0x01);
        write_reg(0x40+op,0x18);write_reg(0x43+op,track_id==MUSIC_DEEP?0x08:0x04);
        write_reg(0x60+op,0xf3);write_reg(0x63+op,0xf2);
        write_reg(0x80+op,0x45);write_reg(0x83+op,0x34);write_reg(0xc0+voice,0x02);
    }
    music_play(MUSIC_TITLE);return 1;
}

void music_shutdown(void){unsigned i;for(i=0;i<9;++i)write_reg((u8)(0xb0+i),0);enabled=0;}
void music_set_enabled(int value){enabled=(u8)(value&&detected);if(!enabled){unsigned i;for(i=0;i<9;++i)write_reg((u8)(0xb0+i),0);}}
int music_is_enabled(void){return enabled!=0;}int music_is_detected(void){return detected!=0;}

void music_play(unsigned track)
{
    track_id=(u8)track;event_index=wait_ticks=0;
}

static void note_on(unsigned voice,unsigned note)
{
    unsigned octave=4+note/12;u16 f=fnums[note%12];
    if(track_id==MUSIC_DEEP&&octave) --octave;
    write_reg((u8)(0xa0+voice),(u8)f);
    write_reg((u8)(0xb0+voice),(u8)(0x20|((octave&7)<<2)|(f>>8)));
}

void music_tick(void)
{
    unsigned voice;if(!enabled)return;
    for(voice=0;voice<6;++voice)if(voice_off[voice]&&!--voice_off[voice])write_reg((u8)(0xb0+voice),0);
    if(wait_ticks){--wait_ticks;return;}
    if(melody[event_index].note==255){event_index=0;wait_ticks=track_id==MUSIC_DEEP?12:5;return;}
    note_on(melody[event_index].voice,melody[event_index].note);
    voice_off[melody[event_index].voice]=melody[event_index].duration;
    wait_ticks=melody[event_index].delay?melody[event_index].delay:1;++event_index;
}
