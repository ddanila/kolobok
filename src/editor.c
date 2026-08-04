#include "assets.h"
#include "game.h"
#include "platform.h"
#include "video.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static int editor_selftest(void)
{
    LevelData level,check;char error[96];const char*path="EDITTEST.KLV";
    remove(path);remove("EDITTEST.TMP");if(!make_blank(&level))return 0;
    level.map[5*80+10]=3;level.checkpoint_count=1;level.checkpoints[0].x=20;level.checkpoints[0].y=8;
    if(!level_save(&level,path,error,sizeof(error))){printf("KOLOEDIT SELFTEST FAIL %s\n",error);level_free(&level);return 0;}
    level_free(&level);if(!level_load(&check,path,error,sizeof(error))){printf("KOLOEDIT SELFTEST FAIL %s\n",error);return 0;}
    if(check.map[5*80+10]!=3||check.checkpoint_count!=1){level_free(&check);return 0;}
    check.map[5*80+10]=0;check.checkpoint_count=0;if(!level_save(&check,path,error,sizeof(error))){level_free(&check);return 0;}level_free(&check);
    if(!level_load(&check,path,error,sizeof(error))||check.map[5*80+10]!=0||check.checkpoint_count!=0){level_free(&check);return 0;}
    level_free(&check);if(remove(path)){return 0;}puts("KOLOEDIT SELFTEST PASS create edit save reload delete");return 1;
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
       if(used)++id;}while(used);
    return id;
}

static int place_object(AssetPack*assets,unsigned x,unsigned y,unsigned tool)
{
    LevelData*l=&assets->level;u16 id=next_object_id(assets);
    if(tool<4&&l->pickup_count<KOLO_MAX_PICKUPS){KoloPickup*p=&l->pickups[l->pickup_count++];memset(p,0,sizeof(*p));p->id=id;p->type=(u8)tool;p->x=(u16)x;p->y=(u16)y;return 1;}
    if(tool<8&&l->animal_count<KOLO_MAX_ENEMIES){KoloAnimalSpawn*a=&l->animals[l->animal_count++];memset(a,0,sizeof(*a));a->id=id;a->type=(u8)(tool-4);a->x=(u16)x;a->y=(u16)y;a->min_x=(u16)(x>3?x-3:0);a->max_x=(u16)(x+3<l->width?x+3:l->width-1);a->tree_id=a->dialogue_id=0xffff;a->climb_min=a->climb_max=(u16)y;return 1;}
    if(tool<11&&l->tree_count<KOLO_MAX_TREES){KoloTree*t=&l->trees[l->tree_count++];memset(t,0,sizeof(*t));t->id=id;t->type=(u8)(tool-8);t->x=(u16)x;t->y=(u8)y;t->height=3;return 1;}return 0;
}

static int erase_object(LevelData*l,unsigned x,unsigned y)
{
    unsigned i;for(i=0;i<l->pickup_count;++i)if(l->pickups[i].x==x&&l->pickups[i].y==y){memmove(&l->pickups[i],&l->pickups[i+1],(l->pickup_count-i-1)*sizeof(KoloPickup));--l->pickup_count;return 1;}
    for(i=0;i<l->animal_count;++i)if(l->animals[i].x==x&&l->animals[i].y==y){memmove(&l->animals[i],&l->animals[i+1],(l->animal_count-i-1)*sizeof(KoloAnimalSpawn));--l->animal_count;return 1;}
    for(i=0;i<l->tree_count;++i)if(l->trees[i].x==x&&l->trees[i].y==y){memmove(&l->trees[i],&l->trees[i+1],(l->tree_count-i-1)*sizeof(KoloTree));--l->tree_count;return 1;}return 0;
}

static int edit_object(LevelData*l,unsigned x,unsigned y)
{
    unsigned i;for(i=0;i<l->pickup_count;++i)if(l->pickups[i].x==x&&l->pickups[i].y==y){l->pickups[i].type=(u8)((l->pickups[i].type+1)%4);return 1;}
    for(i=0;i<l->animal_count;++i)if(l->animals[i].x==x&&l->animals[i].y==y){l->animals[i].type=(u8)((l->animals[i].type+1)%4);l->animals[i].flags^=1;return 1;}
    for(i=0;i<l->tree_count;++i)if(l->trees[i].x==x&&l->trees[i].y==y){l->trees[i].type=(u8)((l->trees[i].type+1)%3);l->trees[i].flags^=1;l->trees[i].height=(u8)(3+(l->trees[i].height-2)%4);return 1;}return 0;
}

int main(int argc,char**argv)
{
    char filename[80],error[96];AssetPack assets;GameState game;unsigned cx=0,cy=0,layer=0,tool=1;int dirty=0,valid=1,running=1,confirm=0,help=0;
    if(argc>1&&!stricmp(argv[1],"-selftest"))return editor_selftest()?0:3;
    if(argc>1)strncpy(filename,argv[1],sizeof(filename)-1);else{printf("Level filename (8.3): ");if(scanf("%79s",filename)!=1)return 2;}filename[sizeof(filename)-1]=0;
    if(!valid_83(filename)){fprintf(stderr,"KOLOEDIT: filename must use 8.3 form\n");return 2;}
    {
        FILE*probe=fopen(filename,"rb");
        if(probe){fclose(probe);if(!level_load(&assets.level,filename,error,sizeof(error))){fprintf(stderr,"KOLOEDIT: %s\n",error);return 2;}level_free(&assets.level);}
        else{LevelData blank;if(!make_blank(&blank)){fprintf(stderr,"KOLOEDIT: out of memory\n");return 2;}if(!level_save(&blank,filename,error,sizeof(error))){fprintf(stderr,"KOLOEDIT: %s\n",error);level_free(&blank);return 2;}level_free(&blank);}
    }
    if(!assets_load_bank(&assets,"KOLOBOK.DAT","GARDEN",filename,error,sizeof(error))){fprintf(stderr,"KOLOEDIT: %s\n",error);return 2;}
    if(!video_init(&assets)||!keyboard_install()){assets_free(&assets);fprintf(stderr,"KOLOEDIT: cannot initialize Mode X editor\n");return 4;}
    game_init(&game,&assets);
    while(running){
        if(help){if(key_pressed(KEY_F1)||key_pressed(KEY_ESCAPE)){help=0;keyboard_clear_edges();}}
        else if(confirm){if(key_pressed(KEY_ENTER)){if(level_save(&assets.level,filename,error,sizeof(error)))running=0;else confirm=0;}else if(key_pressed(KEY_DELETE))running=0;else if(key_pressed(KEY_ESCAPE))confirm=0;}
        else{
            if(key_pressed(KEY_F1)){help=1;keyboard_clear_edges();}
            if(key_pressed(KEY_ESCAPE)){confirm=1;keyboard_clear_edges();}
            if(key_pressed(KEY_LEFT)&&cx)--cx;if(key_pressed(KEY_RIGHT)&&cx+1<assets.level.width)++cx;if(key_pressed(KEY_UP)&&cy)--cy;if(key_pressed(KEY_DOWN)&&cy+1<11)++cy;
            if(key_pressed(KEY_TAB))layer=(layer+1)%3;if(key_pressed(KEY_PAGE_UP))tool=(tool+1)%11;if(key_pressed(KEY_PAGE_DOWN))tool=tool?tool-1:10;
            if(key_pressed(KEY_SPACE)){if(layer==0){assets.level.map[cy*assets.level.width+cx]=(u8)tool;dirty=1;}else if(layer==1){if(place_object(&assets,cx,cy,tool))dirty=1;}else{assets.level.start.x=(u16)cx;assets.level.start.y=(u16)cy;dirty=1;}}
            if(key_pressed(KEY_DELETE)){if(layer==0){assets.level.map[cy*assets.level.width+cx]=0;dirty=1;}else if(layer==1&&erase_object(&assets.level,cx,cy))dirty=1;}
            if(key_pressed(KEY_1)){assets.level.start.x=(u16)cx;assets.level.start.y=(u16)cy;dirty=1;}
            if(key_pressed(KEY_2)){if(!assets.level.checkpoint_count)assets.level.checkpoint_count=1;assets.level.checkpoints[0].x=(u16)cx;assets.level.checkpoints[0].y=(u16)cy;dirty=1;}
            if(key_pressed(KEY_3)){assets.level.exit.x=(u16)cx;assets.level.exit.y=(u16)cy;dirty=1;}
            if(key_pressed(KEY_ENTER)&&layer==1&&edit_object(&assets.level,cx,cy))dirty=1;
            if(key_pressed(KEY_F2)&&level_save(&assets.level,filename,error,sizeof(error)))dirty=0;
            if(key_pressed(KEY_F3))valid=level_validate(&assets.level,error,sizeof(error));
            if(key_pressed(KEY_F4)){assets.level.theme=(u8)((assets.level.theme+1)%3);dirty=1;}
        }
        sync_aliases(&assets);game_init(&game,&assets);game.camera_x=(s32)((cx*16>153?cx*16-153:0))<<8;
        if(help)video_render_editor_help(&game);else if(confirm)video_render_editor_exit(&game);else video_render_editor(&game,cx,cy,layer,tool,dirty,valid);video_present();
    }
    keyboard_remove();video_shutdown();assets_free(&assets);return 0;
}
