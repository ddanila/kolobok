#include "assets.h"
#include "game.h"
#include "platform.h"
#include "video.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROP_PICKUP 0
#define PROP_ANIMAL 1
#define PROP_TREE 2
#define PROP_LEVEL 3

typedef struct PropertyModal {
    int open;
    unsigned kind, index, field;
    LevelData backup;
} PropertyModal;

static int make_blank(LevelData *level)
{
    unsigned x;memset(level,0,sizeof(*level));level->width=80;level->height=11;
    level->theme=KOLO_THEME_GARDEN;level->required_red=1;level->cloud_seed=1;
    level->map=(u8*)calloc(80*11,1);if(!level->map)return 0;
    for(x=0;x<80;++x){level->map[9*80+x]=1;level->map[10*80+x]=2;}
    level->start.x=2;level->start.y=8;level->exit.x=77;level->exit.y=8;level->home=level->start;
    level->pickup_count=1;level->pickups[0].id=1;level->pickups[0].type=KOLO_PICKUP_RED;level->pickups[0].x=40;level->pickups[0].y=8;
    level->animal_count=1;level->animals[0].id=1;level->animals[0].type=KOLO_ANIMAL_RABBIT;level->animals[0].x=70;level->animals[0].y=8;level->animals[0].min_x=65;level->animals[0].max_x=75;level->animals[0].tree_id=0xffff;level->animals[0].dialogue_id=1;level->animals[0].flags=1;
    level->encounter_count=1;level->encounters[0].id=1;level->encounters[0].animal_id=1;level->encounters[0].dialogue_id=1;level->encounters[0].required=1;level->encounters[0].correct=1;level->encounters[0].retry_frames=150;
    return 1;
}

static int valid_83(const char*name)
{
    const char*base=name;const char*p;unsigned stem=0,ext=0;int dot=0;
    for(p=name;*p;++p)if(*p=='/'||*p=='\\')base=p+1;
    if(!*base)return 0;
    for(p=base;*p;++p){if(*p=='.'){if(dot)return 0;dot=1;continue;}if(*p==' '||*p=='/'||*p=='\\')return 0;if(dot)++ext;else ++stem;}
    return stem>0&&stem<=8&&ext<=3;
}

static void sync_aliases(AssetPack*assets)
{
    unsigned i;assets->map=assets->level.map;assets->map_w=assets->level.width;assets->map_h=assets->level.height;assets->enemy_count=assets->level.animal_count;assets->berry_count=0;
    for(i=0;i<assets->level.pickup_count;++i)if(assets->level.pickups[i].type==KOLO_PICKUP_RED)++assets->berry_count;
}

static u16 next_object_id(const AssetPack*assets)
{
    u16 id=1;unsigned i;int used;
    do{used=0;for(i=0;i<assets->level.pickup_count;++i)if(assets->level.pickups[i].id==id)used=1;
       for(i=0;i<assets->level.animal_count;++i)if(assets->level.animals[i].id==id)used=1;
       for(i=0;i<assets->level.tree_count;++i)if(assets->level.trees[i].id==id)used=1;
       if(used)++id;}while(used);return id;
}

static u16 next_encounter_id(const LevelData*level)
{
    u16 id=1;unsigned i;int used;do{used=0;for(i=0;i<level->encounter_count;++i)if(level->encounters[i].id==id)used=1;if(used)++id;}while(used);return id;
}

static KoloEncounter* encounter_for(LevelData*level,u16 animal_id,int create)
{
    unsigned i;KoloEncounter*e;KoloAnimalSpawn*a=0;
    for(i=0;i<level->encounter_count;++i)if(level->encounters[i].animal_id==animal_id)return &level->encounters[i];
    if(!create||level->encounter_count>=KOLO_MAX_ENCOUNTERS)return 0;
    for(i=0;i<level->animal_count;++i)if(level->animals[i].id==animal_id)a=&level->animals[i];
    e=&level->encounters[level->encounter_count++];memset(e,0,sizeof(*e));e->id=next_encounter_id(level);e->animal_id=animal_id;e->dialogue_id=(u8)(a&&a->dialogue_id!=0xffff?a->dialogue_id:1);e->retry_frames=150;return e;
}

static int find_object(const LevelData*level,unsigned x,unsigned y,unsigned*kind,unsigned*index)
{
    unsigned i;for(i=0;i<level->pickup_count;++i)if(level->pickups[i].x==x&&level->pickups[i].y==y){*kind=PROP_PICKUP;*index=i;return 1;}
    for(i=0;i<level->animal_count;++i)if(level->animals[i].x==x&&level->animals[i].y==y){*kind=PROP_ANIMAL;*index=i;return 1;}
    for(i=0;i<level->tree_count;++i)if(level->trees[i].x==x&&level->trees[i].y==y){*kind=PROP_TREE;*index=i;return 1;}return 0;
}

static unsigned property_count(unsigned kind){return kind==PROP_PICKUP?2:kind==PROP_ANIMAL?10:3;}

static u8 wrap_u8(u8 value,int delta,unsigned count)
{
    int next=(int)value+delta;while(next<0)next+=(int)count;while(next>=(int)count)next-=(int)count;return (u8)next;
}

static void adjust_tree_association(LevelData*level,KoloAnimalSpawn*a,int delta)
{
    int pos=-1,next;unsigned i;if(!level->tree_count){a->tree_id=0xffff;return;}
    for(i=0;i<level->tree_count;++i)if(level->trees[i].id==a->tree_id)pos=(int)i;
    next=pos+delta;if(next< -1)next=(int)level->tree_count-1;if(next>=(int)level->tree_count)next=-1;
    a->tree_id=next<0?0xffff:level->trees[next].id;
}

static int adjust_property(LevelData*level,unsigned kind,unsigned index,unsigned field,int delta)
{
    if(kind==PROP_LEVEL){int value;if(field==0)level->theme=wrap_u8(level->theme,delta,3);else if(field==1){value=(int)level->required_red+delta;if(value<0)value=0;if(value>KOLO_MAX_PICKUPS)value=KOLO_MAX_PICKUPS;level->required_red=(u8)value;}else{long seed=(long)level->cloud_seed+delta;if(seed<1)seed=65535;if(seed>65535)seed=1;level->cloud_seed=(u32)seed;}return 1;}
    if(kind==PROP_PICKUP&&index<level->pickup_count){KoloPickup*p=&level->pickups[index];if(field==0)p->type=wrap_u8(p->type,delta,4);else p->flags=(u8)(p->flags+delta);return 1;}
    if(kind==PROP_TREE&&index<level->tree_count){KoloTree*t=&level->trees[index];if(field==0)t->type=wrap_u8(t->type,delta,3);else if(field==1)t->flags=(u8)(t->flags+delta);else{int h=(int)t->height+delta;if(h<1)h=1;if(h>8)h=8;t->height=(u8)h;}return 1;}
    if(kind==PROP_ANIMAL&&index<level->animal_count){
        KoloAnimalSpawn*a=&level->animals[index];KoloEncounter*e;int value;
        switch(field){
        case 0:a->type=wrap_u8(a->type,delta,4);break;
        case 1:a->flags=(u8)(a->flags+delta);break;
        case 2:value=a->dialogue_id==0xffff?1:(int)a->dialogue_id+delta;if(value<1)value=255;if(value>255)value=1;a->dialogue_id=(u16)value;e=encounter_for(level,a->id,1);if(e)e->dialogue_id=(u8)value;break;
        case 3:e=encounter_for(level,a->id,1);if(!e)return 0;e->reward=wrap_u8(e->reward,delta,3);a->flags|=1;break;
        case 4:e=encounter_for(level,a->id,1);if(!e)return 0;e->correct=wrap_u8(e->correct,delta,3);a->flags|=1;break;
        case 5:value=(int)a->min_x+delta;if(value<0)value=0;if(value>(int)a->x)value=a->x;a->min_x=(u16)value;break;
        case 6:value=(int)a->max_x+delta;if(value<(int)a->x)value=a->x;if(value>=level->width)value=level->width-1;a->max_x=(u16)value;break;
        case 7:adjust_tree_association(level,a,delta);break;
        case 8:value=(int)a->climb_min+delta;if(value<0)value=0;if(value>(int)a->climb_max)value=a->climb_max;a->climb_min=(u16)value;break;
        default:value=(int)a->climb_max+delta;if(value<(int)a->climb_min)value=a->climb_min;if(value>=level->height)value=level->height-1;a->climb_max=(u16)value;break;
        }return 1;
    }return 0;
}

static int place_object(AssetPack*assets,unsigned x,unsigned y,unsigned tool)
{
    LevelData*l=&assets->level;u16 id=next_object_id(assets);
    if(tool<4&&l->pickup_count<KOLO_MAX_PICKUPS){KoloPickup*p=&l->pickups[l->pickup_count++];memset(p,0,sizeof(*p));p->id=id;p->type=(u8)tool;p->x=(u16)x;p->y=(u16)y;return 1;}
    if(tool<8&&l->animal_count<KOLO_MAX_ENEMIES){KoloAnimalSpawn*a=&l->animals[l->animal_count++];memset(a,0,sizeof(*a));a->id=id;a->type=(u8)(tool-4);a->x=(u16)x;a->y=(u16)y;a->min_x=(u16)(x>3?x-3:0);a->max_x=(u16)(x+3<l->width?x+3:l->width-1);a->tree_id=a->dialogue_id=0xffff;a->climb_min=a->climb_max=(u16)y;return 1;}
    if(tool<11&&l->tree_count<KOLO_MAX_TREES){KoloTree*t=&l->trees[l->tree_count++];memset(t,0,sizeof(*t));t->id=id;t->type=(u8)(tool-8);t->x=(u16)x;t->y=(u16)y;t->height=3;return 1;}return 0;
}

static int erase_object(LevelData*l,unsigned x,unsigned y)
{
    unsigned i,j;for(i=0;i<l->pickup_count;++i)if(l->pickups[i].x==x&&l->pickups[i].y==y){memmove(&l->pickups[i],&l->pickups[i+1],(l->pickup_count-i-1)*sizeof(KoloPickup));--l->pickup_count;return 1;}
    for(i=0;i<l->animal_count;++i)if(l->animals[i].x==x&&l->animals[i].y==y){u16 id=l->animals[i].id;memmove(&l->animals[i],&l->animals[i+1],(l->animal_count-i-1)*sizeof(KoloAnimalSpawn));--l->animal_count;for(j=0;j<l->encounter_count;)if(l->encounters[j].animal_id==id){memmove(&l->encounters[j],&l->encounters[j+1],(l->encounter_count-j-1)*sizeof(KoloEncounter));--l->encounter_count;}else ++j;return 1;}
    for(i=0;i<l->tree_count;++i)if(l->trees[i].x==x&&l->trees[i].y==y){u16 id=l->trees[i].id;memmove(&l->trees[i],&l->trees[i+1],(l->tree_count-i-1)*sizeof(KoloTree));--l->tree_count;for(j=0;j<l->animal_count;++j)if(l->animals[j].tree_id==id)l->animals[j].tree_id=0xffff;return 1;}return 0;
}

static int editor_selftest(void)
{
    LevelData level,check,backup;KoloAnimalSpawn*a;KoloEncounter*e;char error[96];const char*path="EDITTEST.KLV";
    remove(path);remove("EDITTEST.TMP");if(!make_blank(&level))return 0;
    level.map[5*80+10]=3;level.checkpoint_count=1;level.checkpoints[0].x=20;level.checkpoints[0].y=8;
    level.tree_count=1;level.trees[0].id=10;level.trees[0].type=KOLO_TREE_OAK;level.trees[0].x=55;level.trees[0].y=8;level.trees[0].height=4;
    level.animal_count=2;a=&level.animals[1];memset(a,0,sizeof(*a));a->id=2;a->type=KOLO_ANIMAL_FOX;a->x=60;a->y=8;a->min_x=57;a->max_x=63;a->tree_id=a->dialogue_id=0xffff;a->climb_min=a->climb_max=8;
    backup=level;adjust_property(&level,PROP_ANIMAL,1,0,1);level=backup;if(level.animals[1].type!=KOLO_ANIMAL_FOX)return 0;
    adjust_property(&level,PROP_LEVEL,0,0,1);adjust_property(&level,PROP_LEVEL,0,1,-1);adjust_property(&level,PROP_LEVEL,0,2,1);
    adjust_property(&level,PROP_ANIMAL,1,0,1);adjust_property(&level,PROP_ANIMAL,1,1,1);adjust_property(&level,PROP_ANIMAL,1,2,1);
    adjust_property(&level,PROP_ANIMAL,1,3,1);adjust_property(&level,PROP_ANIMAL,1,4,1);adjust_property(&level,PROP_ANIMAL,1,5,-1);adjust_property(&level,PROP_ANIMAL,1,6,1);adjust_property(&level,PROP_ANIMAL,1,7,1);adjust_property(&level,PROP_ANIMAL,1,8,-1);adjust_property(&level,PROP_ANIMAL,1,9,1);
    if(!level_save(&level,path,error,sizeof(error))){printf("KOLOEDIT SELFTEST FAIL %s\n",error);level_free(&level);return 0;}level_free(&level);
    if(!level_load(&check,path,error,sizeof(error))){printf("KOLOEDIT SELFTEST FAIL %s\n",error);return 0;}a=&check.animals[1];e=encounter_for(&check,a->id,0);
    if(check.map[5*80+10]!=3||check.checkpoint_count!=1||check.theme!=KOLO_THEME_FOREST||check.required_red!=0||check.cloud_seed!=2||a->type!=KOLO_ANIMAL_WOLF||a->flags!=1||a->dialogue_id!=1||a->min_x!=56||a->max_x!=64||a->tree_id!=10||a->climb_min!=7||a->climb_max!=9||!e||e->reward!=KOLO_REWARD_BLUE||e->correct!=1){level_free(&check);return 0;}
    check.map[5*80+10]=0;check.checkpoint_count=0;if(!level_save(&check,path,error,sizeof(error))){level_free(&check);return 0;}level_free(&check);
    if(!level_load(&check,path,error,sizeof(error))||check.map[5*80+10]!=0||check.checkpoint_count!=0){level_free(&check);return 0;}level_free(&check);
    if(remove(path))return 0;puts("KOLOEDIT SELFTEST PASS create edit properties save reload delete");return 1;
}

int main(int argc,char**argv)
{
    char filename[80],error[96];AssetPack assets;GameState game;PropertyModal prop;unsigned cx=0,cy=0,layer=0,tool=1;int dirty=0,valid=1,running=1,confirm=0,help=0;
    memset(&prop,0,sizeof(prop));if(argc>1&&!stricmp(argv[1],"-selftest"))return editor_selftest()?0:3;
    if(argc>1)strncpy(filename,argv[1],sizeof(filename)-1);else{printf("Level filename (8.3): ");if(scanf("%79s",filename)!=1)return 2;}filename[sizeof(filename)-1]=0;
    if(!valid_83(filename)){fprintf(stderr,"KOLOEDIT: filename must use 8.3 form\n");return 2;}
    {FILE*probe=fopen(filename,"rb");if(probe){fclose(probe);if(!level_load(&assets.level,filename,error,sizeof(error))){fprintf(stderr,"KOLOEDIT: %s\n",error);return 2;}level_free(&assets.level);}else{LevelData blank;if(!make_blank(&blank)){fprintf(stderr,"KOLOEDIT: out of memory\n");return 2;}if(!level_save(&blank,filename,error,sizeof(error))){fprintf(stderr,"KOLOEDIT: %s\n",error);level_free(&blank);return 2;}level_free(&blank);}}
    if(!assets_load_bank(&assets,"KOLOBOK.DAT","GARDEN",filename,error,sizeof(error))){fprintf(stderr,"KOLOEDIT: %s\n",error);return 2;}
    if(!video_init(&assets)||!keyboard_install()){assets_free(&assets);fprintf(stderr,"KOLOEDIT: cannot initialize Mode X editor\n");return 4;}game_init(&game,&assets);
    while(running){
        if(prop.open){
            if(key_pressed(KEY_ESCAPE)){assets.level=prop.backup;prop.open=0;keyboard_clear_edges();}
            else if(key_pressed(KEY_ENTER)){prop.open=0;dirty=1;keyboard_clear_edges();}
            else{if(key_pressed(KEY_UP))prop.field=prop.field?prop.field-1:property_count(prop.kind)-1;if(key_pressed(KEY_DOWN))prop.field=(prop.field+1)%property_count(prop.kind);if(key_pressed(KEY_LEFT))adjust_property(&assets.level,prop.kind,prop.index,prop.field,-1);if(key_pressed(KEY_RIGHT))adjust_property(&assets.level,prop.kind,prop.index,prop.field,1);}
        }else if(help){if(key_pressed(KEY_F1)||key_pressed(KEY_ESCAPE)){help=0;keyboard_clear_edges();}}
        else if(confirm){if(key_pressed(KEY_ENTER)){if(level_save(&assets.level,filename,error,sizeof(error)))running=0;else confirm=0;}else if(key_pressed(KEY_DELETE))running=0;else if(key_pressed(KEY_ESCAPE))confirm=0;}
        else{
            if(key_pressed(KEY_F1)){help=1;keyboard_clear_edges();}if(key_pressed(KEY_ESCAPE)){confirm=1;keyboard_clear_edges();}
            if(key_pressed(KEY_LEFT)&&cx)--cx;if(key_pressed(KEY_RIGHT)&&cx+1<assets.level.width)++cx;if(key_pressed(KEY_UP)&&cy)--cy;if(key_pressed(KEY_DOWN)&&cy+1<11)++cy;
            if(key_pressed(KEY_TAB))layer=(layer+1)%3;if(key_pressed(KEY_PAGE_UP))tool=(tool+1)%11;if(key_pressed(KEY_PAGE_DOWN))tool=tool?tool-1:10;
            if(key_pressed(KEY_SPACE)){if(layer==0){assets.level.map[cy*assets.level.width+cx]=(u8)tool;dirty=1;}else if(layer==1){if(place_object(&assets,cx,cy,tool))dirty=1;}else{assets.level.start.x=(u16)cx;assets.level.start.y=(u16)cy;dirty=1;}}
            if(key_pressed(KEY_DELETE)){if(layer==0){assets.level.map[cy*assets.level.width+cx]=0;dirty=1;}else if(layer==1&&erase_object(&assets.level,cx,cy))dirty=1;}
            if(key_pressed(KEY_1)){assets.level.start.x=(u16)cx;assets.level.start.y=(u16)cy;dirty=1;}if(key_pressed(KEY_2)){if(!assets.level.checkpoint_count)assets.level.checkpoint_count=1;assets.level.checkpoints[0].x=(u16)cx;assets.level.checkpoints[0].y=(u16)cy;dirty=1;}if(key_pressed(KEY_3)){assets.level.exit.x=(u16)cx;assets.level.exit.y=(u16)cy;dirty=1;}
            if(key_pressed(KEY_ENTER)&&layer==1&&find_object(&assets.level,cx,cy,&prop.kind,&prop.index)){prop.open=1;prop.field=0;prop.backup=assets.level;keyboard_clear_edges();}
            if(key_pressed(KEY_F2)&&level_save(&assets.level,filename,error,sizeof(error)))dirty=0;if(key_pressed(KEY_F3))valid=level_validate(&assets.level,error,sizeof(error));if(key_pressed(KEY_F4)){prop.open=1;prop.kind=PROP_LEVEL;prop.index=0;prop.field=0;prop.backup=assets.level;keyboard_clear_edges();}
        }
        sync_aliases(&assets);game_init(&game,&assets);game.camera_x=(s32)((cx*16>153?cx*16-153:0))<<8;
        if(prop.open)video_render_editor_properties(&game,prop.kind,prop.index,prop.field);else if(help)video_render_editor_help(&game);else if(confirm)video_render_editor_exit(&game);else video_render_editor(&game,cx,cy,layer,tool,dirty,valid);video_present();
    }
    keyboard_remove();video_shutdown();assets_free(&assets);return 0;
}
