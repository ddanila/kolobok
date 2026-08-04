#include "music.h"

#ifdef __WATCOMC__
#include <conio.h>
#endif

#define REST 255
#define STEPS 16

typedef struct MusicStep { u8 ticks; u8 note[6]; } MusicStep;
typedef struct OplInstrument { u8 mod_char,car_char,mod_level,car_level,attack,decay,wave,feedback; } OplInstrument;

/* The lead contours derive from public-domain song 45 in Tchaikovsky's 1869
 * collection. Counterpoint, chords, bass, rhythm, form and timbres are original. */
static const MusicStep garden_score[STEPS]={
 {4,{31,35,24,28,31,12}},{4,{33,36,24,28,31,12}},{4,{35,38,26,29,33,14}},{8,{36,40,24,28,31,12}},
 {4,{35,38,23,26,31,11}},{4,{33,36,21,24,28,9}},{8,{31,35,19,23,26,7}},{4,{28,31,21,24,28,9}},
 {4,{31,35,24,28,31,12}},{8,{33,36,26,29,33,14}},{8,{31,35,24,28,31,12}},{4,{28,31,21,24,28,9}},
 {4,{26,29,19,23,26,7}},{8,{24,28,17,21,24,5}},{4,{28,31,21,24,28,9}},{8,{24,28,17,21,24,0}}
};

static const MusicStep forest_score[STEPS]={
 {6,{31,REST,24,28,REST,12}},{3,{REST,35,24,28,31,12}},{6,{33,REST,26,29,REST,14}},{3,{REST,36,26,29,33,14}},
 {6,{35,REST,23,26,31,11}},{3,{REST,38,23,26,REST,11}},{9,{36,33,21,24,28,9}},{6,{33,31,19,23,26,7}},
 {6,{31,REST,24,28,31,12}},{3,{REST,35,24,28,REST,12}},{6,{28,31,21,24,28,9}},{6,{31,33,23,26,31,11}},
 {6,{29,33,22,26,29,10}},{6,{26,29,19,23,26,7}},{6,{28,31,21,24,28,9}},{9,{24,28,17,21,24,0}}
};

static const MusicStep deep_score[STEPS]={
 {6,{19,22,12,15,19,0}},{6,{21,24,14,17,21,2}},{6,{22,26,10,14,17,REST}},{10,{24,27,12,15,19,0}},
 {6,{22,26,11,14,19,REST}},{6,{21,24,9,12,16,REST}},{10,{19,22,7,10,14,REST}},{6,{16,19,9,12,16,REST}},
 {6,{19,22,12,15,19,0}},{8,{21,24,14,17,21,2}},{8,{19,22,11,14,19,REST}},{6,{16,19,9,12,16,REST}},
 {6,{14,17,7,10,14,REST}},{10,{12,15,5,8,12,REST}},{6,{11,14,4,7,11,REST}},{12,{12,19,5,8,12,0}}
};

static const MusicStep home_score[STEPS]={
 {3,{31,43,24,28,31,12}},{3,{33,45,26,29,33,14}},{3,{35,47,28,31,35,16}},{6,{36,48,29,33,36,17}},
 {3,{38,47,31,35,38,19}},{3,{36,45,29,33,36,17}},{6,{35,43,28,31,35,16}},{3,{31,40,24,28,31,12}},
 {3,{33,45,26,29,33,14}},{6,{35,47,28,31,35,16}},{6,{36,48,29,33,36,17}},{3,{35,47,28,31,35,16}},
 {3,{33,45,26,29,33,14}},{6,{31,43,24,28,31,12}},{3,{35,47,28,31,35,16}},{9,{36,48,24,28,36,12}}
};

static const OplInstrument instruments[4][6]={
 {{0x21,0x01,0x16,0x03,0xf3,0x45,0,2},{0x21,0x01,0x20,0x08,0xe3,0x45,0,2},{0x01,0x01,0x28,0x0c,0xd4,0x56,0,4},{0x01,0x01,0x2c,0x10,0xd4,0x56,0,4},{0x01,0x01,0x30,0x12,0xc4,0x67,0,4},{0x21,0x01,0x24,0x08,0xf2,0x45,0,6}},
 {{0x61,0x21,0x1c,0x06,0xd4,0x56,1,2},{0x21,0x21,0x24,0x0c,0xc4,0x67,1,2},{0x01,0x01,0x2c,0x10,0xb5,0x78,0,4},{0x01,0x01,0x30,0x14,0xb5,0x78,0,4},{0x21,0x01,0x34,0x18,0xa5,0x89,0,4},{0x21,0x01,0x28,0x0a,0xd3,0x56,0,6}},
 {{0x21,0x01,0x12,0x04,0xf2,0x67,2,6},{0x21,0x01,0x1c,0x0a,0xe2,0x67,2,6},{0x01,0x01,0x24,0x0c,0xc3,0x89,0,4},{0x01,0x01,0x28,0x10,0xc3,0x89,0,4},{0x01,0x01,0x2c,0x14,0xb3,0x9a,0,4},{0x21,0x01,0x18,0x04,0xf1,0x67,0,7}},
 {{0x61,0x21,0x10,0x02,0xf4,0x34,1,2},{0x61,0x21,0x18,0x05,0xf4,0x34,1,2},{0x21,0x01,0x20,0x08,0xe4,0x45,0,4},{0x21,0x01,0x24,0x0a,0xe4,0x45,0,4},{0x21,0x01,0x28,0x0c,0xd4,0x56,0,4},{0x21,0x01,0x18,0x04,0xf3,0x34,0,6}}
};

static const u8 operator_offsets[6]={0,1,2,8,9,10};
static const u16 fnums[12]={343,363,385,408,432,458,485,514,544,577,611,647};
static MusicRegisterSink register_sink;static void*sink_context;
static u8 enabled,detected,track_id,score_id,step_index,step_ticks,voice_on[6];
static u32 debug_ticks,debug_events;static u8 debug_voices;

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
    unsigned i,a,b;write_reg(4,0x60);write_reg(4,0x80);a=inp(0x388)&0xe0;
    write_reg(2,0xff);write_reg(4,0x21);for(i=0;i<100;++i)(void)inp(0x388);
    b=inp(0x388)&0xe0;write_reg(4,0x60);write_reg(4,0x80);return a==0&&b==0xc0;
#else
    return register_sink!=0;
#endif
}

static const MusicStep*current_score(void)
{
    return score_id==0?garden_score:score_id==1?forest_score:score_id==2?deep_score:home_score;
}

static void silence_voices(void)
{
    unsigned voice;for(voice=0;voice<6;++voice){write_reg((u8)(0xb0+voice),0);voice_on[voice]=0;}
}

static void program_instruments(unsigned score)
{
    unsigned voice;for(voice=0;voice<6;++voice){u8 op=operator_offsets[voice];const OplInstrument*i=&instruments[score][voice];
        write_reg((u8)(0x20+op),i->mod_char);write_reg((u8)(0x23+op),i->car_char);
        write_reg((u8)(0x40+op),i->mod_level);write_reg((u8)(0x43+op),i->car_level);
        write_reg((u8)(0x60+op),i->attack);write_reg((u8)(0x63+op),i->attack);
        write_reg((u8)(0x80+op),i->decay);write_reg((u8)(0x83+op),i->decay);
        write_reg((u8)(0xe0+op),i->wave);write_reg((u8)(0xe3+op),i->wave);
        write_reg((u8)(0xc0+voice),i->feedback);
    }
}

static void note_on(unsigned voice,unsigned note)
{
    unsigned octave=2+note/12;u16 f=fnums[note%12];write_reg((u8)(0xa0+voice),(u8)f);
    write_reg((u8)(0xb0+voice),(u8)(0x20|((octave&7)<<2)|(f>>8)));voice_on[voice]=1;
    ++debug_events;debug_voices|=(u8)(1U<<voice);
}

void music_set_sink(MusicRegisterSink sink,void*context){register_sink=sink;sink_context=context;}

int music_init(int requested)
{
    detected=(u8)(requested&&detect_opl());enabled=detected;if(!detected)return 0;
    write_reg(1,0x20);write_reg(8,0);music_play(MUSIC_TITLE);return 1;
}

void music_shutdown(void){if(detected)silence_voices();enabled=0;}
void music_set_enabled(int value){enabled=(u8)(value&&detected);if(!enabled)silence_voices();else music_play(track_id);}
int music_is_enabled(void){return enabled!=0;}int music_is_detected(void){return detected!=0;}

void music_play(unsigned track)
{
    track_id=(u8)track;score_id=(u8)(track==MUSIC_FOREST?1:track==MUSIC_DEEP?2:track==MUSIC_HOME?3:0);
    step_index=step_ticks=0;debug_ticks=debug_events=0;debug_voices=0;
    if(enabled){silence_voices();program_instruments(score_id);}
}

void music_tick(void)
{
    const MusicStep*score;unsigned voice;if(!enabled)return;++debug_ticks;
    if(step_ticks){--step_ticks;return;}score=current_score();silence_voices();
    for(voice=0;voice<6;++voice)if(score[step_index].note[voice]!=REST)note_on(voice,score[step_index].note[voice]);
    step_ticks=(u8)(score[step_index].ticks-1);step_index=(u8)((step_index+1)%STEPS);
}

u32 music_debug_ticks(void){return debug_ticks;}u32 music_debug_events(void){return debug_events;}u8 music_debug_voice_mask(void){return debug_voices;}
