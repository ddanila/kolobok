#include "game.h"

#include <string.h>
#include <ctype.h>

#define GRAVITY 55L
#define MAX_FALL 760L
#define SHORT_JUMP (-350L)

static s32 fp(int value) { return (s32)value << KOLO_FP_SHIFT; }
static int px(s32 value) { return (int)(value >> KOLO_FP_SHIFT); }
static int iabs(int value) { return value < 0 ? -value : value; }

static u8 tile_at(const GameState *game, int x, int y)
{
    int tx, ty;
    if (x < 0 || x >= (int)game->assets->map_w * KOLO_TILE_SIZE) return 2;
    if (y < 0) return 0;
    tx=x/KOLO_TILE_SIZE;ty=y/KOLO_TILE_SIZE;
    if (ty >= game->assets->map_h) return 4;
    return game->assets->map[ty*game->assets->map_w+tx];
}

int game_tile_solid(const GameState *game,int x,int y)
{
    u8 tile=tile_at(game,x,y);return tile<game->assets->tile_count&&
        (game->assets->tile_flags[tile]&1)!=0;
}

int game_tile_hazard(const GameState *game,int x,int y)
{
    u8 tile=tile_at(game,x,y);return tile<game->assets->tile_count&&
        (game->assets->tile_flags[tile]&2)!=0;
}

u8 game_surface_at(const GameState *game,int x,int y)
{
    u8 tile=tile_at(game,x,y);return tile<game->assets->tile_count?
        game->assets->tile_material[tile]:3;
}

static void event_add(GameState *game,u16 event)
{
    game->events|=event;
}

void game_respawn(GameState *game)
{
    game->player.x=game->checkpoint_x;game->player.y=game->checkpoint_y;
    game->player.vx=game->player.vy=0;game->player.invulnerable=KOLO_INVULNERABLE_FRAMES;
    game->player.on_ground=game->player.enemy_bounce=0;++game->respawns;
}

void game_lose_life(GameState *game)
{
    if(game->player.lives)--game->player.lives;
    game->blue_timer=0;event_add(game,KOLO_EVENT_DEATH);
    if(!game->player.lives){game->game_over=1;event_add(game,KOLO_EVENT_GAME_OVER);return;}
    game->player.hp=100;game_respawn(game);
}

static void initialize(GameState *game,const AssetPack *assets,u8 hp,u8 lives)
{
    const LevelData *level=&assets->level;unsigned i;
    memset(game,0,sizeof(*game));game->assets=assets;game->player.hp=hp?hp:100;
    game->player.lives=lives>4?4:(lives?lives:3);game->active_encounter=-1;
    game->checkpoint_x=fp(level->start.x*16);game->checkpoint_y=fp(level->start.y*16+2);
    for(i=0;i<level->animal_count;++i){const KoloAnimalSpawn*s=&level->animals[i];EnemyState*e=&game->enemies[i];
        e->id=s->id;e->type=s->type;e->flags=s->flags;e->tree_id=s->tree_id;e->x=fp(s->x*16);e->y=fp(s->y*16+2);e->spawn_y=e->y;
        e->min_x=fp(s->min_x*16);e->max_x=fp(s->max_x*16);e->vx=(s->type==KOLO_ANIMAL_RABBIT?220L:s->type==KOLO_ANIMAL_FOX?-220L:s->type==KOLO_ANIMAL_WOLF?-260L:-160L);
        if(s->type==KOLO_ANIMAL_RABBIT){e->state=KOLO_AI_WAIT;e->timer=30;}
    }
    game_respawn(game);game->respawns=0;
}

void game_init(GameState *game,const AssetPack *assets){initialize(game,assets,100,3);}
void game_init_carry(GameState *game,const AssetPack *assets,u8 hp,u8 lives){initialize(game,assets,hp,lives);}

static void approach_zero(s32*value,s32 amount)
{
    if(*value>0){*value-=amount;if(*value<0)*value=0;}else if(*value<0){*value+=amount;if(*value>0)*value=0;}
}

static void move_horizontal(GameState*game)
{
    PlayerState*p=&game->player;int x,y1,y2,tx;p->x+=p->vx;x=px(p->x);y1=px(p->y)+2;y2=px(p->y)+KOLO_PLAYER_H-2;
    if(x<0){p->x=0;p->vx=0;}else if(p->vx>0&&(game_tile_solid(game,x+KOLO_PLAYER_W-1,y1)||game_tile_solid(game,x+KOLO_PLAYER_W-1,y2))){tx=(x+KOLO_PLAYER_W-1)/16;p->x=fp(tx*16-KOLO_PLAYER_W);p->vx=0;}
    else if(p->vx<0&&(game_tile_solid(game,x,y1)||game_tile_solid(game,x,y2))){tx=x/16;p->x=fp((tx+1)*16);p->vx=0;}
}

static void move_vertical(GameState*game)
{
    PlayerState*p=&game->player;int x1,x2,y,ty;p->on_ground=0;p->y+=p->vy;y=px(p->y);x1=px(p->x)+2;x2=px(p->x)+KOLO_PLAYER_W-3;
    if(p->vy>=0&&(game_tile_solid(game,x1,y+KOLO_PLAYER_H)||game_tile_solid(game,x2,y+KOLO_PLAYER_H))){ty=(y+KOLO_PLAYER_H)/16;p->y=fp(ty*16-KOLO_PLAYER_H);p->vy=0;p->on_ground=1;p->coyote=3;}
    else if(p->vy<0&&(game_tile_solid(game,x1,y)||game_tile_solid(game,x2,y))){ty=y/16;p->y=fp((ty+1)*16);p->vy=0;}
}

static int overlap(int ax,int ay,int aw,int ah,int bx,int by,int bw,int bh){return ax<bx+bw&&ax+aw>bx&&ay<by+bh&&ay+ah>by;}

void game_damage(GameState*game,u8 type,int source_x)
{
    static const u8 damage[4]={10,25,40,50};PlayerState*p=&game->player;u8 amount;
    if(p->invulnerable||game->game_over)return;
    amount=damage[type<=3?type:3];
    if(type<=KOLO_ANIMAL_FOX&&p->hp<=amount)p->hp=1;
    else if(p->hp<=amount){p->hp=0;event_add(game,KOLO_EVENT_HURT);game_lose_life(game);return;}
    else p->hp=(u8)(p->hp-amount);
    p->invulnerable=KOLO_INVULNERABLE_FRAMES;p->vx=px(p->x)<source_x?-480L:480L;p->vy=-300L;event_add(game,KOLO_EVENT_HURT);
}

static KoloAnimalSpawn const* spawn_for(const GameState*game,unsigned index){return &game->assets->level.animals[index];}

static void bound_enemy(EnemyState*e)
{
    if(e->x<e->min_x){e->x=e->min_x;e->vx=e->vx<0?-e->vx:e->vx;}
    else if(e->x>e->max_x){e->x=e->max_x;e->vx=e->vx>0?-e->vx:e->vx;}
}

static int tree_x(const GameState*game,u16 id)
{
    unsigned i;for(i=0;i<game->assets->level.tree_count;++i)if(game->assets->level.trees[i].id==id)return game->assets->level.trees[i].x*16;return -1;
}

static void update_enemy_ai(GameState*game,unsigned index)
{
    EnemyState*e=&game->enemies[index];const KoloAnimalSpawn*s=spawn_for(game,index);int dx=px(game->player.x-e->x),dy=px(game->player.y-e->y),tx;
    if(e->retry)--e->retry;
    if(e->pacified)return;
    if(e->frozen){--e->frozen;return;}
    switch(e->type){
    case KOLO_ANIMAL_RABBIT:
        if(e->state==KOLO_AI_WAIT){if(e->timer)--e->timer;else{e->state=KOLO_AI_PATROL;e->vx=e->vx<0?-220L:220L;e->vy=-600L;e->y=e->spawn_y;}}
        else{e->x+=e->vx;e->y+=e->vy;e->vy+=GRAVITY;if(e->y>=e->spawn_y&&e->vy>0){e->y=e->spawn_y;e->vy=0;e->state=KOLO_AI_WAIT;e->timer=30;}bound_enemy(e);}break;
    case KOLO_ANIMAL_FOX:
        if(iabs(dx)<140&&iabs(dy)<28)e->vx=dx<0?-360L:360L;else e->vx=e->vx<0?-220L:220L;e->x+=e->vx;bound_enemy(e);break;
    case KOLO_ANIMAL_WOLF:
        if(e->state==KOLO_AI_PATROL){if(iabs(dx)<176&&iabs(dy)<32){e->state=KOLO_AI_TELEGRAPH;e->timer=15;e->vx=0;}else{e->vx=e->vx<0?-260L:260L;e->x+=e->vx;bound_enemy(e);}}
        else if(e->state==KOLO_AI_TELEGRAPH){if(!--e->timer){e->state=KOLO_AI_CHARGE;e->timer=24;e->vx=dx<0?-800L:800L;}}
        else if(e->state==KOLO_AI_CHARGE){e->x+=e->vx;bound_enemy(e);if(!--e->timer){e->state=KOLO_AI_RECOVER;e->timer=30;e->vx=0;}}
        else if(e->state==KOLO_AI_RECOVER&&!--e->timer){e->state=KOLO_AI_PATROL;e->vx=260L;}
        break;
    default:
        tx=tree_x(game,e->tree_id);
        if(e->state==KOLO_AI_PATROL){e->vx=e->vx<0?-160L:160L;e->x+=e->vx;bound_enemy(e);if(tx>=0&&iabs(px(e->x)-tx)<3){e->x=fp(tx);e->vx=0;e->state=KOLO_AI_CLIMB;}}
        else if(e->state==KOLO_AI_CLIMB){e->y-=180L;if(px(e->y)<=s->climb_min*16+2){e->y=fp(s->climb_min*16+2);e->state=KOLO_AI_TOP_WAIT;e->timer=30;}}
        else if(e->state==KOLO_AI_TOP_WAIT&&!--e->timer)e->state=KOLO_AI_DESCEND;
        else if(e->state==KOLO_AI_DESCEND){e->y+=180L;if(px(e->y)>=s->climb_max*16+2){e->y=e->spawn_y;e->state=KOLO_AI_WAIT;e->timer=30;}}
        else if(e->state==KOLO_AI_WAIT&&!--e->timer){e->state=KOLO_AI_PATROL;e->vx=-160L;}
    }
}

static void update_enemies(GameState*game,int old_bottom)
{
    unsigned i;PlayerState*p=&game->player;int player_x=px(p->x),player_y=px(p->y);
    for(i=0;i<game->assets->level.animal_count;++i){EnemyState*e=&game->enemies[i];int ex,ey;update_enemy_ai(game,i);ex=px(e->x);ey=px(e->y);
        if(e->pacified||!overlap(player_x,player_y,KOLO_PLAYER_W,KOLO_PLAYER_H,ex,ey,KOLO_PLAYER_W,KOLO_PLAYER_H))continue;
        if(p->vy>0&&old_bottom<=ey+4){p->y=fp(ey-KOLO_PLAYER_H);p->vy=KOLO_ENEMY_BOUNCE_SPEED;p->enemy_bounce=1;e->frozen=KOLO_FREEZE_FRAMES;event_add(game,KOLO_EVENT_BOUNCE);}
        else if(!e->frozen){game_damage(game,e->type,ex);if(game->game_over||game->respawns)return;}
    }
}

int game_apply_pickup(GameState*game,u8 type)
{
    PlayerState*p=&game->player;
    if(type==KOLO_PICKUP_RED){++game->red_collected;event_add(game,KOLO_EVENT_BERRY);return 1;}
    if(type==KOLO_PICKUP_BLUE){game->blue_timer=KOLO_BLUE_FRAMES;event_add(game,KOLO_EVENT_BLUE);return 1;}
    if(type==KOLO_PICKUP_SMALL_PIE){if(p->hp==100&&p->lives>=3)return 0;p->hp=100;if(p->lives<3)++p->lives;event_add(game,KOLO_EVENT_PIE);return 1;}
    p->hp=100;if(p->lives<3)p->lives=3;else if(p->lives<4)++p->lives;event_add(game,KOLO_EVENT_PIE);return 1;
}

static void update_collectibles(GameState*game)
{
    const LevelData*l=&game->assets->level;unsigned i;int x=px(game->player.x),y=px(game->player.y);
    for(i=0;i<l->pickup_count;++i){const KoloPickup*p=&l->pickups[i];if(!game->pickup_taken[i]&&overlap(x,y,14,14,p->x*16+3,p->y*16+2,10,12)&&game_apply_pickup(game,p->type))game->pickup_taken[i]=1;}
    for(i=0;i<l->checkpoint_count;++i){int cx=l->checkpoints[i].x*16;if(x>=cx-8&&x<=cx+20&&game->checkpoint_x!=fp(cx)){game->checkpoint_x=fp(cx);game->checkpoint_y=fp(l->checkpoints[i].y*16+2);event_add(game,KOLO_EVENT_CHECKPOINT);}}
    if(game_exit_ready(game)&&iabs(x-(int)l->exit.x*16)<16){game->won=1;event_add(game,KOLO_EVENT_WIN);}
}

int game_exit_ready(const GameState*game){return game->red_collected>=game->assets->level.required_red&&game->guardian_solved;}

int game_try_talk(GameState*game)
{
    unsigned i,j;int x=px(game->player.x),y=px(game->player.y);
    if(game->active_dialogue)return 1;
    for(i=0;i<game->assets->level.encounter_count;++i){const KoloEncounter*c=&game->assets->level.encounters[i];
        if(game->encounter_solved[i])continue;
        for(j=0;j<game->assets->level.animal_count;++j)
            if(game->enemies[j].id==c->animal_id){EnemyState*e=&game->enemies[j];if(!e->retry&&iabs(x-px(e->x))<=24&&iabs(y-px(e->y))<=24){game->active_dialogue=1;game->active_encounter=(s8)i;event_add(game,KOLO_EVENT_DIALOGUE);return 1;}}
    }
    return 0;
}

int game_answer_dialogue(GameState*game,unsigned answer)
{
    const KoloEncounter*c;unsigned j,index;if(!game->active_dialogue||game->active_encounter<0||answer>2)return 0;
    index=(unsigned)game->active_encounter;c=&game->assets->level.encounters[index];game->active_dialogue=0;game->active_encounter=-1;
    for(j=0;j<game->assets->level.animal_count;++j)if(game->enemies[j].id==c->animal_id){EnemyState*e=&game->enemies[j];
        if(answer==c->correct){game->encounter_solved[index]=1;e->pacified=1;if(c->required)game->guardian_solved=1;if(c->reward==KOLO_REWARD_BLUE)game_apply_pickup(game,KOLO_PICKUP_BLUE);else if(c->reward==KOLO_REWARD_SMALL_PIE)game_apply_pickup(game,KOLO_PICKUP_SMALL_PIE);event_add(game,KOLO_EVENT_PACIFY);return 1;}
        e->retry=c->retry_frames;game_damage(game,e->type,px(e->x));return -1;}
    return 0;
}

void game_step(GameState*game,const GameInput*input)
{
    PlayerState*p=&game->player;int x,y,old_bottom;u8 surface;s32 accel,reverse,max_speed,brake,target_camera;
    game->events=0;++game->ticks;if(game->won||game->game_over||game->active_dialogue)return;
    if(p->invulnerable)--p->invulnerable;
    if(game->blue_timer)--game->blue_timer;
    if(input->talk_pressed&&game_try_talk(game))return;
    surface=p->on_ground?game_surface_at(game,px(p->x)+7,px(p->y)+KOLO_PLAYER_H+1):3;
    if(surface==1){accel=24;reverse=48;max_speed=512;brake=32;}else if(surface==2){accel=16;reverse=24;max_speed=704;brake=4;}else if(surface==3){accel=16;reverse=16;max_speed=640;brake=0;}else{accel=32;reverse=64;max_speed=640;brake=20;}
    if(game->blue_timer){accel=accel*5/4;reverse=reverse*5/4;max_speed=max_speed*3/2;}
    if(input->jump_pressed)p->jump_buffer=3;
    if(input->left&&!input->right){p->vx-=p->vx>0?reverse:accel;if(p->vx < -max_speed)p->vx=-max_speed;}
    else if(input->right&&!input->left){p->vx+=p->vx<0?reverse:accel;if(p->vx>max_speed)p->vx=max_speed;}
    else if(brake)approach_zero(&p->vx,brake);
    if(p->jump_buffer&&(p->on_ground||p->coyote)){p->vy=KOLO_JUMP_SPEED;p->enemy_bounce=p->on_ground=p->coyote=p->jump_buffer=0;event_add(game,KOLO_EVENT_JUMP);}else if(p->jump_buffer)--p->jump_buffer;
    if(!input->jump_held&&!p->enemy_bounce&&p->vy<SHORT_JUMP)p->vy=SHORT_JUMP;
    old_bottom=px(p->y)+KOLO_PLAYER_H;p->vy+=GRAVITY;if(p->vy>MAX_FALL)p->vy=MAX_FALL;move_horizontal(game);move_vertical(game);if(p->vy>=0)p->enemy_bounce=0;if(!p->on_ground&&p->coyote)--p->coyote;
    update_enemies(game,old_bottom);if(game->game_over)return;x=px(p->x);y=px(p->y);
    if(game_tile_hazard(game,x+2,y+KOLO_PLAYER_H)||game_tile_hazard(game,x+KOLO_PLAYER_W-3,y+KOLO_PLAYER_H)||y>(int)game->assets->map_h*16){event_add(game,KOLO_EVENT_HURT);game_lose_life(game);return;}
    update_collectibles(game);if(p->vx)p->animation=(u8)((px(p->x)>>2)&3);
    target_camera=p->x-fp(153);if(target_camera<0)target_camera=0;if(target_camera>fp(game->assets->map_w*16-320))target_camera=fp(game->assets->map_w*16-320);game->camera_x+=(target_camera-game->camera_x)/4;
}

int campaign_codeword_stage(const char *word)
{
    char upper[9];unsigned i=0;
    while(word[i]&&i<8){upper[i]=(char)toupper((unsigned char)word[i]);++i;}upper[i]=0;
    if(!strcmp(upper,"REPKA"))return 0;
    if(!strcmp(upper,"TEREMOK"))return 1;
    if(!strcmp(upper,"MOROZKO"))return 2;
    return -1;
}
