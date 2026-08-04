#include "assets.h"
#include "game.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void load(AssetPack *pack,const char*bank,const char*level)
{
    char error[96];assert(assets_load_bank(pack,"build/KOLOBOK.DAT",bank,level,error,sizeof(error)));
}

static void step(GameState*g,unsigned count)
{
    GameInput input={0,0,0,0,0};while(count--)game_step(g,&input);
}

static void test_assets_and_levels(void)
{
    AssetPack p;load(&p,"GARDEN","build/GARDEN.KLV");
    assert(p.map_w==96&&p.map_h==11&&p.tile_count==11&&p.sprite_count==15);
    assert(p.level.required_red==6&&p.level.pickup_count==10&&p.level.animal_count==8);
    assets_free(&p);load(&p,"FOREST","build/SFOREST.KLV");assert(p.map_w==128&&p.level.required_red==8);assets_free(&p);
    load(&p,"DEEP","build/DFOREST.KLV");assert(p.map_w==160&&p.level.required_red==10&&p.level.animal_count==9);assets_free(&p);
}

static void test_crc_rejection(void)
{
    FILE*s=fopen("build/GARDEN.KLV","rb"),*t=fopen("build/BAD.KLV","wb");int c;long n=0;AssetPack p;char error[96];
    assert(s&&t);while((c=fgetc(s))!=EOF){if(n++==100)c^=1;fputc(c,t);}fclose(s);fclose(t);
    assert(!assets_load_bank(&p,"build/KOLOBOK.DAT","GARDEN","build/BAD.KLV",error,sizeof(error)));
    assert(strstr(error,"checksum")!=0);remove("build/BAD.KLV");
}

static void test_surface_physics(void)
{
    AssetPack p;GameState g;GameInput right={0,1,0,0,0};load(&p,"GARDEN","build/GARDEN.KLV");game_init(&g,&p);
    g.player.invulnerable=255;g.player.on_ground=1;g.player.x=(s32)(6*16)<<8;g.player.y=(s32)(9*16-14)<<8;g.player.vx=0;game_step(&g,&right);assert(g.player.vx==32);
    g.player.on_ground=1;g.player.x=(s32)(33*16)<<8;g.player.y=(s32)(9*16-14)<<8;g.player.vx=0;game_step(&g,&right);assert(g.player.vx==24);
    g.player.on_ground=0;g.player.x=(s32)(33*16)<<8;g.player.y=(s32)(7*16)<<8;g.player.vx=0;game_step(&g,&right);assert(g.player.vx==16);assets_free(&p);
    load(&p,"FOREST","build/SFOREST.KLV");game_init(&g,&p);g.player.on_ground=1;g.player.x=(s32)(66*16)<<8;g.player.y=(s32)(9*16-14)<<8;g.player.vx=0;game_step(&g,&right);assert(g.player.vx==16);assets_free(&p);
}

static void test_boost_and_pies(void)
{
    AssetPack p;GameState g;GameInput right={0,1,0,0,0};load(&p,"GARDEN","build/GARDEN.KLV");game_init(&g,&p);
    assert(game_apply_pickup(&g,KOLO_PICKUP_BLUE));assert(g.blue_timer==300);g.player.on_ground=1;g.player.vx=0;game_step(&g,&right);assert(g.player.vx==40&&g.blue_timer==299);
    step(&g,10);game_apply_pickup(&g,KOLO_PICKUP_BLUE);assert(g.blue_timer==300);step(&g,300);assert(g.blue_timer==0);
    g.player.hp=100;g.player.lives=3;assert(!game_apply_pickup(&g,KOLO_PICKUP_SMALL_PIE));g.player.hp=50;assert(game_apply_pickup(&g,KOLO_PICKUP_SMALL_PIE)&&g.player.hp==100&&g.player.lives==3);
    g.player.hp=50;g.player.lives=2;game_apply_pickup(&g,KOLO_PICKUP_SMALL_PIE);assert(g.player.hp==100&&g.player.lives==3);
    g.player.lives=3;game_apply_pickup(&g,KOLO_PICKUP_BIG_PIE);assert(g.player.lives==4);g.player.lives=1;game_apply_pickup(&g,KOLO_PICKUP_BIG_PIE);assert(g.player.lives==3);assets_free(&p);
}

static void test_damage_lives_checkpoint(void)
{
    AssetPack p;GameState g;load(&p,"GARDEN","build/GARDEN.KLV");game_init(&g,&p);g.player.invulnerable=0;
    game_damage(&g,KOLO_ANIMAL_RABBIT,1000);assert(g.player.hp==90&&g.player.invulnerable==30&&g.player.vx==-480&&g.player.vy==-300);
    g.player.invulnerable=0;g.player.hp=20;game_damage(&g,KOLO_ANIMAL_FOX,0);assert(g.player.hp==1&&g.player.lives==3);
    g.checkpoint_x=(s32)40*16<<8;g.checkpoint_y=(s32)130<<8;g.pickup_taken[0]=1;g.red_collected=1;g.blue_timer=200;g.player.invulnerable=0;g.player.hp=30;
    game_damage(&g,KOLO_ANIMAL_WOLF,0);assert(g.player.lives==2&&g.player.hp==100&&g.player.x==g.checkpoint_x&&g.red_collected==1&&g.pickup_taken[0]&&g.blue_timer==0);
    g.player.invulnerable=0;g.player.hp=40;g.player.lives=1;game_damage(&g,KOLO_ANIMAL_BEAR,0);assert(g.game_over&&g.player.lives==0);assets_free(&p);
}

static void test_ai_freeze(void)
{
    AssetPack p;GameState g;unsigned i;load(&p,"GARDEN","build/GARDEN.KLV");game_init(&g,&p);g.player.x=0;g.player.y=0;
    for(i=0;i<30;++i)step(&g,1);
    assert(g.enemies[0].state==KOLO_AI_WAIT&&g.enemies[0].timer==0);
    step(&g,1);assert(g.enemies[0].state==KOLO_AI_PATROL&&g.enemies[0].vy==-600);
    g.enemies[0].frozen=90;{s32 x=g.enemies[0].x;step(&g,1);assert(g.enemies[0].frozen==89&&g.enemies[0].x==x);}assets_free(&p);
    load(&p,"FOREST","build/SFOREST.KLV");game_init(&g,&p);g.player.x=g.enemies[6].x;g.player.y=g.enemies[6].y;g.player.invulnerable=255;step(&g,1);assert(g.enemies[6].state==KOLO_AI_TELEGRAPH&&g.enemies[6].timer==15);for(i=0;i<15;++i)step(&g,1);assert(g.enemies[6].state==KOLO_AI_CHARGE&&(g.enemies[6].vx==800||g.enemies[6].vx==-800));assets_free(&p);
    load(&p,"DEEP","build/DFOREST.KLV");game_init(&g,&p);g.player.x=0;g.player.y=0;for(i=0;i<400&&g.enemies[6].state!=KOLO_AI_CLIMB;++i)step(&g,1);assert(g.enemies[6].state==KOLO_AI_CLIMB||g.enemies[6].state==KOLO_AI_TOP_WAIT);assets_free(&p);
}

static void test_repeated_stomp_refresh(void)
{
    AssetPack p;GameState g;GameInput input={0,0,0,0,0};EnemyState*e;unsigned i;load(&p,"GARDEN","build/GARDEN.KLV");game_init(&g,&p);
    for(i=1;i<p.level.animal_count;++i)g.enemies[i].pacified=1;
    e=&g.enemies[0];e->vx=0;g.player.invulnerable=255;
    g.player.x=e->x;g.player.y=e->y-((s32)KOLO_PLAYER_H<<8);g.player.vy=400;g.player.on_ground=0;game_step(&g,&input);assert((g.events&KOLO_EVENT_BOUNCE)&&e->frozen==90);
    g.player.x=0;for(i=0;i<10;++i)game_step(&g,&input);assert(e->frozen==80);
    g.player.x=e->x;g.player.y=e->y-((s32)KOLO_PLAYER_H<<8);g.player.vy=400;g.player.on_ground=0;game_step(&g,&input);assert(e->frozen==90);assets_free(&p);
}

static void test_complete_bear_cycle(void)
{
    AssetPack p;GameState g;unsigned i,seen_climb=0,seen_top=0,seen_down=0,seen_wait=0,top_frames=0,wait_frames=0;EnemyState*b;
    load(&p,"DEEP","build/DFOREST.KLV");game_init(&g,&p);for(i=0;i<p.level.animal_count;++i)if(i!=6)g.enemies[i].pacified=1;b=&g.enemies[6];g.player.invulnerable=255;
    for(i=0;i<1400;++i){step(&g,1);if(b->state==KOLO_AI_CLIMB)seen_climb=1;if(b->state==KOLO_AI_TOP_WAIT){seen_top=1;++top_frames;}if(b->state==KOLO_AI_DESCEND)seen_down=1;if(seen_down&&b->state==KOLO_AI_WAIT){seen_wait=1;++wait_frames;}if(seen_wait&&b->state==KOLO_AI_PATROL)break;}
    assert(seen_climb&&seen_top&&seen_down&&seen_wait&&b->state==KOLO_AI_PATROL);assert(top_frames==30&&wait_frames==30);assets_free(&p);
}

static void test_dialogue_and_gate(void)
{
    AssetPack p;GameState g;unsigned guardian=0,i;load(&p,"GARDEN","build/GARDEN.KLV");game_init(&g,&p);
    for(i=0;i<p.level.animal_count;++i)if(p.level.animals[i].id==p.level.encounters[0].animal_id)guardian=i;
    g.player.x=g.enemies[guardian].x;g.player.y=g.enemies[guardian].y;g.player.invulnerable=0;assert(game_try_talk(&g));assert(game_answer_dialogue(&g,0)==-1);assert(g.enemies[guardian].retry==150&&!g.guardian_solved);
    g.enemies[guardian].retry=0;g.player.x=g.enemies[guardian].x;g.player.y=g.enemies[guardian].y;g.player.invulnerable=0;assert(game_try_talk(&g));assert(game_answer_dialogue(&g,1)==1);assert(g.guardian_solved&&g.enemies[guardian].pacified);
    assert(!game_exit_ready(&g));g.red_collected=6;assert(game_exit_ready(&g));assets_free(&p);
}

static void test_optional_rewards(void)
{
    AssetPack p;GameState g;unsigned i,animal=0;load(&p,"GARDEN","build/GARDEN.KLV");game_init(&g,&p);
    for(i=0;i<p.level.animal_count;++i)if(p.level.animals[i].id==p.level.encounters[1].animal_id)animal=i;
    g.player.x=g.enemies[animal].x;g.player.y=g.enemies[animal].y;g.player.invulnerable=0;assert(game_try_talk(&g));assert(g.active_encounter==1);assert(game_answer_dialogue(&g,0)==1);assert(g.blue_timer==300&&g.enemies[animal].pacified);assets_free(&p);
    load(&p,"FOREST","build/SFOREST.KLV");game_init(&g,&p);for(i=0;i<p.level.animal_count;++i)if(p.level.animals[i].id==p.level.encounters[1].animal_id)animal=i;
    g.player.hp=50;g.player.lives=2;g.player.x=g.enemies[animal].x;g.player.y=g.enemies[animal].y;assert(game_try_talk(&g));assert(g.active_encounter==1);assert(game_answer_dialogue(&g,2)==1);assert(g.player.hp==100&&g.player.lives==3);assets_free(&p);
}

static void test_sequential_carry(void)
{
    AssetPack garden,forest,deep;GameState g;load(&garden,"GARDEN","build/GARDEN.KLV");load(&forest,"FOREST","build/SFOREST.KLV");load(&deep,"DEEP","build/DFOREST.KLV");
    game_init(&g,&garden);g.player.hp=63;g.player.lives=2;g.blue_timer=200;game_init_carry(&g,&forest,g.player.hp,g.player.lives);assert(g.player.hp==63&&g.player.lives==2&&g.blue_timer==0&&g.red_collected==0);
    g.player.hp=41;g.player.lives=4;game_init_carry(&g,&deep,g.player.hp,g.player.lives);assert(g.player.hp==41&&g.player.lives==4&&g.blue_timer==0);
    game_init(&g,&deep);assert(g.player.hp==100&&g.player.lives==3);assets_free(&garden);assets_free(&forest);assets_free(&deep);
}

static void test_codewords(void)
{
    assert(campaign_codeword_stage("REPKA")==0);assert(campaign_codeword_stage("teremok")==1);assert(campaign_codeword_stage("Morozko")==2);assert(campaign_codeword_stage("bogus")==-1);
}

int main(void)
{
    test_assets_and_levels();test_crc_rejection();test_surface_physics();test_boost_and_pies();test_damage_lives_checkpoint();test_ai_freeze();test_repeated_stomp_refresh();test_complete_bear_cycle();test_dialogue_and_gate();test_optional_rewards();test_sequential_carry();test_codewords();puts("host gameplay tests: PASS");return 0;
}
