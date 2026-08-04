#include "assets.h"
#include "game.h"
#include "music.h"
#include "platform.h"
#include "video.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static int sound_on=1;

static const char *bank_names[3]={"GARDEN","FOREST","DEEP"};
static const char *level_names[3]={"GARDEN.KLV","SFOREST.KLV","DFOREST.KLV"};

static void wait_for_frame(clock_t*next,unsigned*remainder)
{
    clock_t now;do{now=clock();}while((long)(now-*next)<0);
    if((long)(now-*next)>(long)CLOCKS_PER_SEC/5L)*next=now;
    *next+=CLOCKS_PER_SEC/30;*remainder+=CLOCKS_PER_SEC%30;
    if(*remainder>=30){++*next;*remainder-=30;}
}

static void play_events(unsigned events)
{
    if(events&KOLO_EVENT_WIN)speaker_play(988,18);
    else if(events&KOLO_EVENT_PACIFY)speaker_play(880,12);
    else if(events&KOLO_EVENT_CHECKPOINT)speaker_play(784,8);
    else if(events&(KOLO_EVENT_BERRY|KOLO_EVENT_BLUE|KOLO_EVENT_PIE))speaker_play(1047,5);
    else if(events&KOLO_EVENT_HURT)speaker_play(147,10);
    else if(events&KOLO_EVENT_BOUNCE)speaker_play(659,4);
    else if(events&KOLO_EVENT_JUMP)speaker_play(440,3);
}

static int load_stage(AssetPack*assets,unsigned stage,char*error,unsigned error_size)
{
    return assets_load_bank(assets,"KOLOBOK.DAT",bank_names[stage],level_names[stage],error,error_size);
}

static int load_title(AssetPack*assets,char*error,unsigned error_size)
{
    return assets_load_bank(assets,"KOLOBOK.DAT","INTRO","GARDEN.KLV",error,error_size);
}

static int selftest(AssetPack*assets)
{
    GameState game;GameInput input;u32 a,b,code_good,code_bad;unsigned i;
    if(assets->map_w!=96||assets->map_h!=11||assets->level.required_red!=6||
       !assets_far_memory_active(assets))return 0;
    game_init(&game,assets);memset(&input,0,sizeof(input));input.right=1;
    for(i=0;i<30;++i)game_step(&game,&input);
    if(game.player.vx<=0||game.player.hp!=100||game.player.lives!=3)return 0;
    game_apply_pickup(&game,KOLO_PICKUP_BLUE);if(game.blue_timer!=300)return 0;
    game.player.hp=50;game_apply_pickup(&game,KOLO_PICKUP_SMALL_PIE);
    if(game.player.hp!=100||game.player.lives!=3)return 0;
    if(campaign_codeword_stage("repka")!=0||campaign_codeword_stage("TEREMOK")!=1||
       campaign_codeword_stage("MOROZKO")!=2||campaign_codeword_stage("NOPE")!=-1)return 0;
    if(!music_init(1)||!music_is_detected())return 0;
    music_play(MUSIC_GARDEN);for(i=0;i<192;++i)music_tick();
    if(music_debug_ticks()!=192||music_debug_events()<40||music_debug_voice_mask()!=0x3f){music_shutdown();return 0;}
    music_play(MUSIC_DEEP);for(i=0;i<192;++i)music_tick();
    if(music_debug_ticks()!=192||music_debug_events()<40||music_debug_voice_mask()!=0x3f){music_shutdown();return 0;}
    music_shutdown();
    if(!video_init(assets))return 0;video_vsync_enable(0);
    video_render_game(&game);video_present();a=video_vram_crc();
    video_render_game(&game);video_present();b=video_vram_crc();
    if(a!=b||!video_display_state_valid()){video_shutdown();return 0;}
    video_render_dialogue(&game,1);video_present();
    if(video_frame_crc()!=video_vram_crc()){video_shutdown();return 0;}
    video_render_codeword(assets,"REPKA",0);video_present();code_good=video_vram_crc();
    video_render_codeword(assets,"WRONG",1);video_present();code_bad=video_vram_crc();
    if(code_good==code_bad||video_frame_crc()!=video_vram_crc()){video_shutdown();return 0;}
    video_shutdown();printf("KOLOBOK SELFTEST PASS CRC=%08lX VRAM=%08lX\n",a,b);return 1;
}

static int benchmark(AssetPack*assets)
{
    GameState game;GameInput input;VideoProfile profile;clock_t started,elapsed,paced_started,paced_elapsed,next;
    unsigned long fps10,paced10;unsigned frame,remainder=0;const unsigned count=60;
    int max_camera=(int)assets->map_w*16-320;
    game_init(&game,assets);memset(&input,0,sizeof(input));input.right=1;
    if(!video_init(assets))return 0;video_vsync_enable(0);video_render_game(&game);video_present();started=clock();
    for(frame=0;frame<count;++frame){int camera=(int)((unsigned long)frame*61UL%(unsigned long)max_camera);game_step(&game,&input);game.camera_x=(s32)camera<<8;game.player.x=(s32)(camera+153)<<8;video_render_game(&game);video_present();}
    elapsed=clock()-started;video_vsync_enable(1);paced_started=clock();next=paced_started;
    for(frame=0;frame<count;++frame){int camera=(int)((unsigned long)frame*61UL%(unsigned long)max_camera);wait_for_frame(&next,&remainder);game_step(&game,&input);game.camera_x=(s32)camera<<8;game.player.x=(s32)(camera+153)<<8;video_render_game(&game);video_present();}
    paced_elapsed=clock()-paced_started;video_vsync_enable(0);video_profile_reset();video_profile_enable(1);
    for(frame=0;frame<count;++frame){int camera=(int)((unsigned long)frame*61UL%(unsigned long)max_camera);game.camera_x=(s32)camera<<8;game.player.x=(s32)(camera+153)<<8;video_render_game(&game);video_present();}
    video_profile_enable(0);video_profile_get(&profile);video_shutdown();if(!elapsed)elapsed=1;if(!paced_elapsed)paced_elapsed=1;
    fps10=(unsigned long)count*CLOCKS_PER_SEC*10UL/(unsigned long)elapsed; paced10=(unsigned long)count*CLOCKS_PER_SEC*10UL/(unsigned long)paced_elapsed;
    printf("KOLOBOK BENCH frames=%u ticks=%lu hz=%lu fps10=%lu paced10=%lu\n",count,(unsigned long)elapsed,(unsigned long)CLOCKS_PER_SEC,fps10,paced10);
    printf("KOLOBOK PROFILE frames=%u bg=%lu tiles=%lu sprites=%lu hud=%lu vga=%lu hz=%lu\n",profile.frames,profile.background_ticks,profile.tile_ticks,profile.sprite_ticks,profile.hud_ticks,profile.present_ticks,KOLO_PROFILE_TIMER_HZ);return 1;
}

static int capture_frame(AssetPack*assets,const char*kind)
{
    GameState game;GameInput input;int frame,written;u32 crc;game_init(&game,assets);memset(&input,0,sizeof(input));input.right=1;
    if(!video_init(assets))return 0;video_vsync_enable(0);
    if(kind&&!stricmp(kind,"intro"))video_render_intro(assets,1,30);
    else if(kind&&!stricmp(kind,"dialogue")){game.active_dialogue=1;game.active_encounter=0;video_render_dialogue(&game,1);}
    else if(kind&&!stricmp(kind,"gameover")){game.game_over=1;game.player.lives=0;video_render_game_over(&game);}
    else if(kind&&!stricmp(kind,"home"))video_render_ending(&game,190);
    else if(kind&&!stricmp(kind,"credits"))video_render_credits(&game,260);
    else if(kind&&!stricmp(kind,"frozen")){game.enemies[0].frozen=90;video_render_game(&game);}
    else{for(frame=0;frame<20;++frame)game_step(&game,&input);video_render_game(&game);}
    video_present();crc=video_vram_crc();written=video_write_ppm("KOLOBOK.PPM",assets);video_shutdown();if(written)printf("KOLOBOK CAPTURE PASS KOLOBOK.PPM CRC=%08lX\n",crc);return written;
}

static int campaign_playtest(AssetPack*assets,char*error,unsigned error_size)
{
    GameState game;GameInput input;u8 hp=100,lives=3;unsigned stage,frame,i;int passed=1;
    assets_free(assets);
    for(stage=0;stage<3&&passed;++stage){
        if(!load_stage(assets,stage,error,error_size))return 0;game_init_carry(&game,assets,hp,lives);
        for(frame=0;frame<(unsigned)assets->map_w*75U&&!game.won&&!game.game_over;++frame){
            int player_x=(int)(game.player.x>>KOLO_FP_SHIFT),danger=0,target_x=(int)assets->level.exit.x*16,direction=1,target_is_pickup=0;memset(&input,0,sizeof(input));
            if(game.active_dialogue){unsigned encounter=(unsigned)game.active_encounter;game_answer_dialogue(&game,assets->level.encounters[encounter].correct);continue;}
            {unsigned best=0xffff;for(i=0;i<assets->level.pickup_count;++i)if(!game.pickup_taken[i]&&assets->level.pickups[i].y==8){int candidate=(int)assets->level.pickups[i].x*16+4;unsigned distance=(unsigned)(candidate>player_x?candidate-player_x:player_x-candidate);if(distance<best){best=distance;target_x=candidate;target_is_pickup=1;}}}
            direction=target_x<player_x?-1:1;if(direction<0)input.left=1;else input.right=1;input.jump_held=1;input.talk_pressed=1;
            for(i=0;i<assets->level.animal_count;++i){int dx=(int)(game.enemies[i].x>>KOLO_FP_SHIFT)-player_x;if(!game.enemies[i].pacified&&((direction>0&&dx>-18&&dx<68)||(direction<0&&dx<18&&dx>-68)))danger=1;}
            for(i=12;i<=64;i+=8)if(game_tile_hazard(&game,player_x+direction*(int)i,KOLO_LEVEL_HEIGHT*16-8))danger=1;
            if(target_is_pickup&&player_x-target_x>-10&&player_x-target_x<10&&!game.player.on_ground){input.left=input.right=0;}
            if(game.player.on_ground&&(danger||(game.player.vx==0&&player_x-target_x>12)||(game.player.vx==0&&player_x-target_x< -12))){input.jump_pressed=1;}
            game_step(&game,&input);
        }
        if(!game.won||game.game_over||game.red_collected<assets->level.required_red||!game.guardian_solved){
            printf("KOLOBOK PLAYTEST FAIL stage=%u frame=%u x=%d y=%d vx=%ld vy=%ld ground=%u red=%u guardian=%u hp=%u lives=%u\n",stage,frame,(int)(game.player.x>>KOLO_FP_SHIFT),(int)(game.player.y>>KOLO_FP_SHIFT),game.player.vx,game.player.vy,game.player.on_ground,game.red_collected,game.guardian_solved,game.player.hp,game.player.lives);passed=0;
        }else{hp=game.player.hp;lives=game.player.lives;printf("KOLOBOK PLAYTEST LEVEL %u PASS frames=%u hp=%u lives=%u\n",stage+1,frame,hp,lives);}
        assets_free(assets);
    }
    if(passed)printf("KOLOBOK PLAYTEST PASS sequential carry hp=%u lives=%u\n",hp,lives);return passed;
}

static void read_game_input(GameInput*input)
{
    memset(input,0,sizeof(*input));input->left=(u8)(key_down(KEY_LEFT)||key_down(KEY_A));
    input->right=(u8)(key_down(KEY_RIGHT)||key_down(KEY_D));
    input->jump_held=(u8)(key_down(KEY_SPACE)||key_down(KEY_UP));
    input->jump_pressed=(u8)(key_pressed(KEY_SPACE)||key_pressed(KEY_UP));
    input->talk_pressed=(u8)key_pressed(KEY_ENTER);
}

static void append_code_key(char*word,unsigned*length)
{
    static const struct {unsigned key;char letter;} letters[]={
        {KEY_R,'R'},{KEY_E,'E'},{KEY_P,'P'},{KEY_K,'K'},{KEY_A,'A'},
        {KEY_T,'T'},{KEY_M,'M'},{KEY_O,'O'},{KEY_Z,'Z'}};
    unsigned i;if(key_pressed(KEY_BACKSPACE)){if(*length)word[--*length]=0;return;}
    for(i=0;i<sizeof(letters)/sizeof(letters[0]);++i)if(key_pressed(letters[i].key)&&*length<8){word[(*length)++]=letters[i].letter;word[*length]=0;}
}

int main(int argc,char**argv)
{
    enum {UI_TITLE,UI_CODE,UI_INTRO,UI_PLAY,UI_PAUSE,UI_ENDING,UI_CREDITS} ui=UI_TITLE;
    AssetPack assets;GameState game;GameInput input;char error[96],codeword[9]="";unsigned code_len=0;
    int running=1,music_requested=1,test_mode=0,benchmark_mode=0,capture_mode=0,playtest_mode=0,invalid_code=0;
    const char*capture_kind=0;unsigned stage=0,menu=0,dialogue_choice=0,intro_scene=0;u32 ui_ticks=0;unsigned remainder=0;clock_t next;int i;
    setvbuf(stdout,NULL,_IONBF,0);
    for(i=1;i<argc;++i){if(!stricmp(argv[i],"-nosound"))sound_on=0;else if(!stricmp(argv[i],"-nomusic"))music_requested=0;else if(!stricmp(argv[i],"-selftest"))test_mode=1;else if(!stricmp(argv[i],"-playtest"))playtest_mode=1;else if(!stricmp(argv[i],"-benchmark"))benchmark_mode=1;else if(!stricmp(argv[i],"-capture")){capture_mode=1;if(i+1<argc&&argv[i+1][0]!='-')capture_kind=argv[++i];}}
    if(capture_mode&&capture_kind&&(!stricmp(capture_kind,"forest")))stage=1;
    else if((capture_mode&&capture_kind&&(!stricmp(capture_kind,"deep")||!stricmp(capture_kind,"home")||!stricmp(capture_kind,"credits")||!stricmp(capture_kind,"frozen")))||benchmark_mode)stage=2;
    if((!benchmark_mode&&!capture_mode)?!load_title(&assets,error,sizeof(error)):
       (capture_mode&&capture_kind&&!stricmp(capture_kind,"intro"))?!load_title(&assets,error,sizeof(error)):
       !assets_load_bank(&assets,"KOLOBOK.DAT",bank_names[stage],level_names[stage],error,sizeof(error))){fprintf(stderr,"KOLOBOK: %s\n",error);return 2;}
    if(test_mode){i=selftest(&assets);assets_free(&assets);return i?0:3;}
    if(playtest_mode){i=campaign_playtest(&assets,error,sizeof(error));if(assets.blob)assets_free(&assets);return i?0:9;}
    if(benchmark_mode){i=benchmark(&assets);assets_free(&assets);return i?0:6;}
    if(capture_mode){i=capture_frame(&assets,capture_kind);assets_free(&assets);return i?0:7;}
    if(!video_init(&assets)){assets_free(&assets);fprintf(stderr,"KOLOBOK: cannot initialize Mode X\n");return 4;}
    if(!keyboard_install()){video_shutdown();assets_free(&assets);fprintf(stderr,"KOLOBOK: cannot install keyboard handler\n");return 5;}
    speaker_init(sound_on);music_init(music_requested);music_play(MUSIC_TITLE);game_init(&game,&assets);next=clock();
    while(running){wait_for_frame(&next,&remainder);speaker_tick();music_tick();++ui_ticks;
        if(key_pressed(KEY_S)){sound_on=!sound_on;speaker_shutdown();speaker_init(sound_on);}
        if(key_pressed(KEY_M))music_set_enabled(!music_is_enabled());
        if(ui==UI_TITLE){
            if(key_pressed(KEY_UP)){menu=menu?menu-1:2;}if(key_pressed(KEY_DOWN)){menu=(menu+1)%3;}
            if(key_pressed(KEY_ESCAPE))running=0;
            if(key_pressed(KEY_ENTER)||key_pressed(KEY_SPACE)){
                if(menu==2)running=0;else if(menu==1){ui=UI_CODE;code_len=0;codeword[0]=0;invalid_code=0;}
                else{stage=0;game_init(&game,&assets);ui=UI_INTRO;intro_scene=0;ui_ticks=0;music_play(MUSIC_GARDEN);}keyboard_clear_edges();
            }
            video_render_menu(&assets,ui_ticks,menu);video_present();continue;
        }
        if(ui==UI_CODE){
            if(key_pressed(KEY_ESCAPE)){ui=UI_TITLE;keyboard_clear_edges();}
            else{append_code_key(codeword,&code_len);if(key_pressed(KEY_ENTER)){int selected=campaign_codeword_stage(codeword);if(selected<0)invalid_code=1;else{u8 hp=100,lives=3;video_shutdown();assets_free(&assets);stage=(unsigned)selected;if(!load_stage(&assets,stage,error,sizeof(error)))break;if(!video_init(&assets))break;game_init_carry(&game,&assets,hp,lives);music_play(stage==0?MUSIC_GARDEN:stage==1?MUSIC_FOREST:MUSIC_DEEP);ui=UI_PLAY;keyboard_clear_edges();}}}
            video_render_codeword(&assets,codeword,invalid_code);video_present();continue;
        }
        if(ui==UI_INTRO){
            if(key_pressed(KEY_ESCAPE)){intro_scene=4;keyboard_clear_edges();}
            else if(key_pressed(KEY_ENTER)||ui_ticks>=135){++intro_scene;ui_ticks=0;keyboard_clear_edges();}
            if(intro_scene>=4){video_shutdown();assets_free(&assets);stage=0;if(!load_stage(&assets,stage,error,sizeof(error)))break;if(!video_init(&assets))break;game_init(&game,&assets);ui=UI_PLAY;}
            else{video_render_intro(&assets,intro_scene,ui_ticks);video_present();continue;}
        }
        if(ui==UI_PAUSE){if(key_pressed(KEY_ESCAPE))running=0;else if(key_pressed(KEY_ENTER)){ui=UI_PLAY;keyboard_clear_edges();}video_render_pause(&game);video_present();continue;}
        if(ui==UI_ENDING){if(key_pressed(KEY_ENTER)&&ui_ticks>180){ui=UI_CREDITS;ui_ticks=0;}video_render_ending(&game,ui_ticks);video_present();continue;}
        if(ui==UI_CREDITS){if((key_pressed(KEY_ENTER)&&ui_ticks>420)||key_pressed(KEY_ESCAPE)){video_shutdown();assets_free(&assets);stage=0;if(!load_title(&assets,error,sizeof(error)))break;if(!video_init(&assets))break;game_init(&game,&assets);ui=UI_TITLE;music_play(MUSIC_TITLE);}else{video_render_credits(&game,ui_ticks);video_present();}continue;}
        if(game.game_over){if(key_pressed(KEY_ENTER)){video_shutdown();assets_free(&assets);stage=0;if(!load_title(&assets,error,sizeof(error)))break;if(!video_init(&assets))break;game_init(&game,&assets);ui=UI_TITLE;music_play(MUSIC_TITLE);}video_render_game_over(&game);video_present();continue;}
        if(game.active_dialogue){if(key_pressed(KEY_UP))dialogue_choice=dialogue_choice?dialogue_choice-1:2;if(key_pressed(KEY_DOWN))dialogue_choice=(dialogue_choice+1)%3;if(key_pressed(KEY_1))dialogue_choice=0;if(key_pressed(KEY_2))dialogue_choice=1;if(key_pressed(KEY_3))dialogue_choice=2;if(key_pressed(KEY_ENTER)){game_answer_dialogue(&game,dialogue_choice);keyboard_clear_edges();}video_render_dialogue(&game,dialogue_choice);video_present();continue;}
        if(key_pressed(KEY_ESCAPE)){ui=UI_PAUSE;keyboard_clear_edges();continue;}
        read_game_input(&input);game_step(&game,&input);play_events(game.events);
        if(game.won){if(stage<2){u8 hp=game.player.hp,lives=game.player.lives;video_shutdown();assets_free(&assets);++stage;if(!load_stage(&assets,stage,error,sizeof(error)))break;if(!video_init(&assets))break;game_init_carry(&game,&assets,hp,lives);music_play(stage==1?MUSIC_FOREST:MUSIC_DEEP);}else{game.blue_timer=0;ui=UI_ENDING;ui_ticks=0;music_play(MUSIC_HOME);}}
        video_render_game(&game);video_present();
    }
    music_shutdown();speaker_shutdown();keyboard_remove();video_shutdown();assets_free(&assets);
    if(!running)return 0;fprintf(stderr,"KOLOBOK: %s\n",error);return 8;
}
