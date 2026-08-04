#include "assets.h"

#include <stdlib.h>
#include <string.h>
#ifdef __WATCOMC__
#include <dos.h>
#include <i86.h>
#endif

#define KLV_HEADER_SIZE 32

static u16 read_u16(const u8 **cursor)
{
    u16 value = (u16)((*cursor)[0] | ((u16)(*cursor)[1] << 8));
    *cursor += 2; return value;
}

static u32 read_u32_at(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void write_u16(FILE *file, u16 value)
{
    fputc(value & 255, file); fputc(value >> 8, file);
}

static void write_u32(FILE *file, u32 value)
{
    fputc((int)(value & 255), file); fputc((int)((value >> 8) & 255), file);
    fputc((int)((value >> 16) & 255), file); fputc((int)((value >> 24) & 255), file);
}

u32 assets_crc32(KoloConstFarPtr data, u32 length)
{
    u32 crc = 0xffffffffUL, i;
    int bit;
    for (i = 0; i < length; ++i) {
        crc ^= data[i];
        for (bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320UL & (0UL - (crc & 1UL)));
    }
    return crc ^ 0xffffffffUL;
}

static int set_error(char *error, unsigned size, const char *message)
{
    if (error != NULL && size) { strncpy(error, message, size - 1); error[size - 1] = 0; }
    return 0;
}

static u16 far_read_u16_at(KoloConstFarPtr cursor)
{
    return (u16)(cursor[0]|((u16)cursor[1]<<8));
}

static u32 far_read_u32(KoloConstFarPtr p)
{
    return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24);
}

static int far_equal(KoloConstFarPtr p,const char *text,unsigned length)
{
    unsigned i;for(i=0;i<length;++i)if(p[i]!=(u8)text[i])return 0;return 1;
}

void level_free(LevelData *level)
{
    if (level->map != NULL) free(level->map);
    memset(level, 0, sizeof(*level));
}

int level_validate(const LevelData *level, char *error, unsigned error_size)
{
    unsigned i,j, red = 0, required = 0;
    if (level->width < 32 || level->width > 256 || level->height != KOLO_LEVEL_HEIGHT)
        return set_error(error, error_size, "level must be 32..256 by 11 tiles");
    if (level->theme > KOLO_THEME_DEEP || level->map == NULL)
        return set_error(error, error_size, "invalid level theme or tile map");
    if (level->checkpoint_count > KOLO_MAX_CHECKPOINTS ||
        level->pickup_count > KOLO_MAX_PICKUPS || level->animal_count > KOLO_MAX_ENEMIES ||
        level->tree_count > KOLO_MAX_TREES || level->encounter_count > KOLO_MAX_ENCOUNTERS)
        return set_error(error, error_size, "level object limit exceeded");
    if(level->start.x>=level->width||level->start.y>=level->height||
       level->exit.x>=level->width||level->exit.y>=level->height||
       level->home.x>=level->width||level->home.y>=level->height)
        return set_error(error,error_size,"level marker is outside map");
    for(i=0;i<level->checkpoint_count;++i)
        if(level->checkpoints[i].x>=level->width||level->checkpoints[i].y>=level->height)
            return set_error(error,error_size,"checkpoint is outside map");
    for (i = 0; i < (unsigned)level->width * level->height; ++i)
        if (level->map[i] > 10) return set_error(error, error_size, "unknown tile in level");
    for (i = 0; i < level->pickup_count; ++i) {
        if (level->pickups[i].type > KOLO_PICKUP_BIG_PIE ||
            level->pickups[i].x >= level->width || level->pickups[i].y >= level->height)
            return set_error(error, error_size, "invalid pickup record");
        if (level->pickups[i].type == KOLO_PICKUP_RED) ++red;
        for(j=0;j<i;++j)if(level->pickups[j].id==level->pickups[i].id)
            return set_error(error,error_size,"duplicate pickup ID");
    }
    if (red < level->required_red) return set_error(error, error_size, "not enough red berries");
    for (i = 0; i < level->animal_count; ++i) {
        const KoloAnimalSpawn *a = &level->animals[i];
        if (a->type > KOLO_ANIMAL_BEAR || a->x >= level->width || a->y >= level->height ||
            a->min_x > a->x || a->max_x < a->x || a->max_x >= level->width)
            return set_error(error, error_size, "invalid animal record");
        for(j=0;j<i;++j)if(level->animals[j].id==a->id)
            return set_error(error,error_size,"duplicate animal ID");
    }
    for(i=0;i<level->tree_count;++i){
        if(level->trees[i].type>KOLO_TREE_OAK||level->trees[i].x>=level->width||
           level->trees[i].y>=level->height||!level->trees[i].height)
            return set_error(error,error_size,"invalid tree record");
        for(j=0;j<i;++j)if(level->trees[j].id==level->trees[i].id)
            return set_error(error,error_size,"duplicate tree ID");
    }
    for(i=0;i<level->animal_count;++i)if(level->animals[i].tree_id!=0xffff){
        int found=0;for(j=0;j<level->tree_count;++j)if(level->trees[j].id==level->animals[i].tree_id)found=1;
        if(!found)return set_error(error,error_size,"animal refers to missing tree");
    }
    for (i = 0; i < level->encounter_count; ++i) {
        int found=0;for(j=0;j<level->animal_count;++j)if(level->animals[j].id==level->encounters[i].animal_id)found=1;
        if(!found)return set_error(error,error_size,"encounter refers to missing animal");
        if (level->encounters[i].correct > 2 || level->encounters[i].reward > KOLO_REWARD_SMALL_PIE)
            return set_error(error, error_size, "invalid encounter record");
        for(j=0;j<i;++j)if(level->encounters[j].id==level->encounters[i].id)
            return set_error(error,error_size,"duplicate encounter ID");
        if (level->encounters[i].required) ++required;
    }
    if (required != 1) return set_error(error, error_size, "level needs exactly one guardian");
    return 1;
}

int level_load(LevelData *level, const char *path, char *error, unsigned error_size)
{
    FILE *file;
    long size;
    u8 *blob, *body;
    const u8 *p, *end;
    u16 version, header_size;
    u32 crc;
    unsigned i;
    memset(level, 0, sizeof(*level));
    file = fopen(path, "rb");
    if (!file) return set_error(error, error_size, "cannot open KLV level");
    if (fseek(file, 0, SEEK_END) || (size = ftell(file)) < KLV_HEADER_SIZE ||
        size > 8192 || fseek(file, 0, SEEK_SET)) {
        fclose(file); return set_error(error, error_size, "cannot measure KLV level");
    }
    blob = (u8 *)malloc((unsigned)size);
    if (!blob) { fclose(file); return set_error(error, error_size, "not enough memory for level"); }
    if (fread(blob, 1, (unsigned)size, file) != (unsigned)size) {
        fclose(file); free(blob); return set_error(error, error_size, "short read from KLV level");
    }
    fclose(file);
#ifdef KOLO_DEBUG_LOAD
    puts("LOAD level read");
#endif
    if (memcmp(blob, "KLV4", 4)) { free(blob); return set_error(error, error_size, "unsupported KLV signature"); }
    p = blob + 4; version = read_u16(&p); header_size = read_u16(&p); crc = read_u32_at(p); p += 4;
    if (version != 4 || header_size != KLV_HEADER_SIZE) {
        free(blob); return set_error(error, error_size, "unsupported KLV version");
    }
    body = blob + header_size;
    if (assets_crc32(body, (u32)size - header_size) != crc) {
        free(blob); return set_error(error, error_size, "KLV checksum mismatch");
    }
    level->width = read_u16(&p); level->height = read_u16(&p);
    level->theme = *p++; level->required_red = *p++; level->cloud_seed = read_u32_at(p); p += 4;
    level->checkpoint_count = read_u16(&p); level->pickup_count = read_u16(&p);
    level->animal_count = read_u16(&p); level->tree_count = read_u16(&p);
    level->encounter_count = read_u16(&p);
#ifdef KOLO_DEBUG_LOAD
    puts("LOAD level header");
#endif
    if (level->width < 32 || level->width > 256 || level->height != KOLO_LEVEL_HEIGHT ||
        level->checkpoint_count > KOLO_MAX_CHECKPOINTS || level->pickup_count > KOLO_MAX_PICKUPS ||
        level->animal_count > KOLO_MAX_ENEMIES || level->tree_count > KOLO_MAX_TREES ||
        level->encounter_count > KOLO_MAX_ENCOUNTERS) {
        free(blob); memset(level, 0, sizeof(*level));
        return set_error(error, error_size, "invalid KLV metadata");
    }
    end = blob + size; p = body;
#define NEED(n) if ((u32)(end - p) < (u32)(n)) { free(blob); memset(level,0,sizeof(*level)); return set_error(error,error_size,"truncated KLV payload"); }
    NEED((u32)level->width * level->height + 12UL);
    level->map = (u8 *)malloc((unsigned)level->width * level->height);
    if (!level->map) { free(blob); return set_error(error, error_size, "not enough memory for tile map"); }
    memcpy(level->map, p, (unsigned)level->width * level->height); p += (unsigned)level->width * level->height;
#ifdef KOLO_DEBUG_LOAD
    puts("LOAD level map");
#endif
#define READ_POINT(q) do { (q).x=read_u16(&p); (q).y=read_u16(&p); } while(0)
    READ_POINT(level->start); READ_POINT(level->exit); READ_POINT(level->home);
    NEED((u32)level->checkpoint_count * 4UL + (u32)level->pickup_count * 8UL +
         (u32)level->animal_count * 20UL + (u32)level->tree_count * 8UL +
         (u32)level->encounter_count * 10UL);
    for (i=0;i<level->checkpoint_count;++i) READ_POINT(level->checkpoints[i]);
    for (i=0;i<level->pickup_count;++i) {
        KoloPickup *v=&level->pickups[i]; v->type=*p++; v->flags=*p++; v->id=read_u16(&p); v->x=read_u16(&p); v->y=read_u16(&p);
    }
    for (i=0;i<level->animal_count;++i) {
        KoloAnimalSpawn *v=&level->animals[i]; v->type=*p++;v->flags=*p++;v->id=read_u16(&p);
        v->x=read_u16(&p);v->y=read_u16(&p);v->min_x=read_u16(&p);v->max_x=read_u16(&p);
        v->tree_id=read_u16(&p);v->climb_min=read_u16(&p);v->climb_max=read_u16(&p);v->dialogue_id=read_u16(&p);
    }
    for (i=0;i<level->tree_count;++i) {
        KoloTree *v=&level->trees[i]; u16 yh; v->type=*p++;v->flags=*p++;v->id=read_u16(&p);v->x=read_u16(&p);yh=read_u16(&p);v->y=(u8)yh;v->height=(u8)(yh>>8);
    }
    for (i=0;i<level->encounter_count;++i) {
        KoloEncounter *v=&level->encounters[i];v->id=read_u16(&p);v->animal_id=read_u16(&p);
        v->dialogue_id=*p++;v->required=*p++;v->correct=*p++;v->reward=*p++;v->retry_frames=read_u16(&p);
    }
#ifdef KOLO_DEBUG_LOAD
    puts("LOAD level objects");
#endif
    free(blob);
    if (p != end || !level_validate(level,error,error_size)) { level_free(level); return 0; }
    return 1;
#undef NEED
#undef READ_POINT
}

static void put_u16(u8 **p, u16 v) { *(*p)++=(u8)v; *(*p)++=(u8)(v>>8); }
int level_save(const LevelData *level, const char *path, char *error, unsigned error_size)
{
    char temp[132],backup[132]; FILE *file; u8 *body,*p; u32 body_size,crc; unsigned i;int had_original;
    LevelData check;
    if (!level_validate(level,error,error_size)) return 0;
    body_size=(u32)level->width*level->height+12UL+level->checkpoint_count*4UL+
        level->pickup_count*8UL+level->animal_count*20UL+level->tree_count*8UL+level->encounter_count*10UL;
    body=(u8 *)malloc((unsigned)body_size); if(!body)return set_error(error,error_size,"not enough memory to save level");
    p=body;memcpy(p,level->map,(unsigned)level->width*level->height);p+=(unsigned)level->width*level->height;
#define PUT_POINT(q) do { put_u16(&p,(q).x);put_u16(&p,(q).y); } while(0)
    PUT_POINT(level->start);PUT_POINT(level->exit);PUT_POINT(level->home);
    for(i=0;i<level->checkpoint_count;++i)PUT_POINT(level->checkpoints[i]);
    for(i=0;i<level->pickup_count;++i){const KoloPickup*v=&level->pickups[i];*p++=v->type;*p++=v->flags;put_u16(&p,v->id);put_u16(&p,v->x);put_u16(&p,v->y);}
    for(i=0;i<level->animal_count;++i){const KoloAnimalSpawn*v=&level->animals[i];*p++=v->type;*p++=v->flags;put_u16(&p,v->id);put_u16(&p,v->x);put_u16(&p,v->y);put_u16(&p,v->min_x);put_u16(&p,v->max_x);put_u16(&p,v->tree_id);put_u16(&p,v->climb_min);put_u16(&p,v->climb_max);put_u16(&p,v->dialogue_id);}
    for(i=0;i<level->tree_count;++i){const KoloTree*v=&level->trees[i];*p++=v->type;*p++=v->flags;put_u16(&p,v->id);put_u16(&p,v->x);put_u16(&p,(u16)(v->y|((u16)v->height<<8)));}
    for(i=0;i<level->encounter_count;++i){const KoloEncounter*v=&level->encounters[i];put_u16(&p,v->id);put_u16(&p,v->animal_id);*p++=v->dialogue_id;*p++=v->required;*p++=v->correct;*p++=v->reward;put_u16(&p,v->retry_frames);}
    crc=assets_crc32(body,body_size);
    if(strlen(path)+5>=sizeof(temp)){free(body);return set_error(error,error_size,"level filename is too long");}
    strcpy(temp,path);strcpy(backup,path);
    {
        char *dot=strrchr(temp,'.');char *slash=strrchr(temp,'/');
        if(dot!=NULL&&(slash==NULL||dot>slash))*dot=0;
        dot=strrchr(backup,'.');slash=strrchr(backup,'/');
        if(dot!=NULL&&(slash==NULL||dot>slash))*dot=0;
    }
    strcat(temp,".TMP");strcat(backup,".BAK");file=fopen(temp,"wb");
    if(!file){free(body);return set_error(error,error_size,"cannot create temporary level");}
    fwrite("KLV4",1,4,file);write_u16(file,4);write_u16(file,KLV_HEADER_SIZE);write_u32(file,crc);
    write_u16(file,level->width);write_u16(file,level->height);fputc(level->theme,file);fputc(level->required_red,file);write_u32(file,level->cloud_seed);
    write_u16(file,level->checkpoint_count);write_u16(file,level->pickup_count);write_u16(file,level->animal_count);write_u16(file,level->tree_count);write_u16(file,level->encounter_count);
    if(fwrite(body,1,(unsigned)body_size,file)!=(unsigned)body_size||fclose(file)){free(body);remove(temp);return set_error(error,error_size,"failed writing temporary level");}
    free(body);
    if(!level_load(&check,temp,error,error_size)){remove(temp);return 0;} level_free(&check);
    remove(backup);had_original=rename(path,backup)==0;
    if(rename(temp,path)){if(had_original)rename(backup,path);remove(temp);return set_error(error,error_size,"cannot replace level file");}
    if(had_original)remove(backup);
    return 1;
#undef PUT_POINT
}

static int parse_bank(AssetPack *pack, char *error, unsigned error_size)
{
    KoloConstFarPtr p,end,span_end; u16 version,tile_w,tile_h,span_size; unsigned i,row,variant;
    if(pack->blob_size<24||!far_equal(pack->blob,"KBANK4\0\0",8))return set_error(error,error_size,"bad resource bank signature");
    if(assets_crc32(pack->blob,pack->blob_size-4)!=far_read_u32(pack->blob+pack->blob_size-4))return set_error(error,error_size,"resource bank checksum mismatch");
    p=pack->blob+8;end=pack->blob+pack->blob_size-4;
    version=far_read_u16_at(p);p+=2;pack->theme=far_read_u16_at(p);p+=2;
    pack->tile_count=far_read_u16_at(p);p+=2;pack->sprite_count=far_read_u16_at(p);p+=2;
    tile_w=far_read_u16_at(p);p+=2;tile_h=far_read_u16_at(p);p+=2;
    if(version!=4||tile_w!=16||tile_h!=16||pack->tile_count==0||pack->tile_count>16||pack->sprite_count==0||pack->sprite_count>KOLO_MAX_SPRITES)return set_error(error,error_size,"unsupported resource bank metadata");
    if((u32)(end-p)<768UL+(u32)pack->tile_count*258UL+4UL)return set_error(error,error_size,"truncated resource bank");
    pack->palette=(KoloFarPtr)p;p+=768;pack->tiles=(KoloFarPtr)p;p+=(u32)pack->tile_count*256UL;
    pack->tile_flags=(KoloFarPtr)p;p+=pack->tile_count;pack->tile_material=(KoloFarPtr)p;p+=pack->tile_count;
    span_size=far_read_u16_at(p);p+=2;if((u32)(end-p)<(u32)span_size+2UL)return set_error(error,error_size,"truncated sprite spans");span_end=p+span_size;
    for(i=0;i<pack->sprite_count;++i){pack->sprite_spans[i]=(KoloFarPtr)p;for(row=0;row<16;++row){unsigned run,n;if(p>=span_end)return set_error(error,error_size,"truncated sprite spans");n=*p++;for(run=0;run<n;++run){unsigned x,len;if((u32)(span_end-p)<2)return set_error(error,error_size,"truncated sprite span");x=*p++;len=*p++;if(!len||x+len>16||(u32)(span_end-p)<len)return set_error(error,error_size,"invalid sprite span");p+=len;}}}
    if(p!=span_end)return set_error(error,error_size,"unexpected sprite span size");
    span_size=far_read_u16_at(p);p+=2;
    if((u32)(end-p)!=(u32)span_size)return set_error(error,error_size,"unexpected planar span size");
    span_end=p+span_size;
    for(i=0;i<pack->sprite_count;++i)for(variant=0;variant<16;++variant){pack->sprite_planar_spans[i][variant]=(KoloFarPtr)p;for(row=0;row<16;++row){unsigned run,n;if(p>=span_end)return set_error(error,error_size,"truncated planar spans");n=*p++;for(run=0;run<n;++run){unsigned x,len;if((u32)(span_end-p)<2)return set_error(error,error_size,"truncated planar span");x=*p++;len=*p++;if(!len||x+len>5||(u32)(span_end-p)<len)return set_error(error,error_size,"invalid planar span");p+=len;}}}
    return p==span_end?1:set_error(error,error_size,"unexpected planar span data");
}

static int read_bank_blob(AssetPack *pack,FILE *file,u32 size,
                          char *error,unsigned error_size)
{
#ifdef __WATCOMC__
    u8 buffer[1024];u32 done=0;unsigned chunk,segment;
    if(_dos_allocmem((unsigned)((size+15UL)>>4),&segment)!=0)
        return set_error(error,error_size,"not enough far memory for resource bank");
    pack->bank_segment=(u16)segment;
    pack->blob=(KoloFarPtr)MK_FP(pack->bank_segment,0);
    while(done<size){
        chunk=(unsigned)(size-done>sizeof(buffer)?sizeof(buffer):size-done);
        if(fread(buffer,1,chunk,file)!=chunk)return set_error(error,error_size,"short read from resource bank");
        _fmemcpy(pack->blob+done,buffer,chunk);done+=chunk;
    }
#else
    pack->blob=(u8*)malloc((unsigned)size);
    if(!pack->blob)return set_error(error,error_size,"not enough memory for resource bank");
    if(fread(pack->blob,1,(unsigned)size,file)!=(unsigned)size)
        return set_error(error,error_size,"short read from resource bank");
#endif
    pack->blob_size=size;return 1;
}

int assets_load_bank(AssetPack *pack,const char *archive_path,const char *bank_name,const char *level_path,char *error,unsigned error_size)
{
    FILE *file;u8 header[12],entry[16];u16 count,i;u32 offset=0,size=0;char wanted[9];
    memset(pack,0,sizeof(*pack));memset(wanted,0,sizeof(wanted));strncpy(wanted,bank_name,8);
    if(level_path&&!level_load(&pack->level,level_path,error,error_size))return 0;
    file=fopen(archive_path,"rb");if(!file){assets_free(pack);return set_error(error,error_size,"cannot open KOLOBOK.DAT");}
    if(fread(header,1,12,file)!=12||memcmp(header,"KOLODAT4",8)){fclose(file);assets_free(pack);return set_error(error,error_size,"unsupported KOLOBOK.DAT format");}
    if((header[8]|((u16)header[9]<<8))!=4){fclose(file);assets_free(pack);return set_error(error,error_size,"unsupported archive version");}count=(u16)(header[10]|((u16)header[11]<<8));
    if(!count||count>16){fclose(file);assets_free(pack);return set_error(error,error_size,"invalid archive bank count");}
    for(i=0;i<count;++i){if(fread(entry,1,16,file)!=16){fclose(file);assets_free(pack);return set_error(error,error_size,"truncated archive index");}if(!strncmp((char*)entry,wanted,8)){offset=read_u32_at(entry+8);size=read_u32_at(entry+12);}}
    if(!offset||size<24||size>=61440UL){fclose(file);assets_free(pack);return set_error(error,error_size,"resource bank missing or too large");}
    if(fseek(file,(long)offset,SEEK_SET)){fclose(file);assets_free(pack);return set_error(error,error_size,"cannot seek resource bank");}
    if(!read_bank_blob(pack,file,size,error,error_size)){fclose(file);assets_free(pack);return 0;}fclose(file);
#ifdef KOLO_DEBUG_LOAD
    puts("LOAD bank read");
#endif
    if(!parse_bank(pack,error,error_size)){assets_free(pack);return 0;}
#ifdef KOLO_DEBUG_LOAD
    puts("LOAD bank parsed");
#endif
    pack->map_w=pack->level.width;pack->map_h=pack->level.height;pack->map=pack->level.map;
    for(i=0;i<pack->level.pickup_count;++i)if(pack->level.pickups[i].type==KOLO_PICKUP_RED)++pack->berry_count;
    pack->enemy_count=pack->level.animal_count;return 1;
}

int assets_load(AssetPack *pack,const char *path,char *error,unsigned error_size)
{
    char level_path[132];const char *slash=strrchr(path,'/');
    if(slash){unsigned n=(unsigned)(slash-path+1);if(n+10>=sizeof(level_path))return set_error(error,error_size,"asset path too long");memcpy(level_path,path,n);strcpy(level_path+n,"GARDEN.KLV");}
    else strcpy(level_path,"GARDEN.KLV");
    return assets_load_bank(pack,path,"GARDEN",level_path,error,error_size);
}

void assets_free(AssetPack *pack)
{
    level_free(&pack->level);
#ifdef __WATCOMC__
    if(pack->bank_segment)_dos_freemem(pack->bank_segment);
#else
    if(pack->blob)free(pack->blob);
#endif
    memset(pack,0,sizeof(*pack));
}

int assets_far_memory_active(const AssetPack *pack)
{
#ifdef __WATCOMC__
    return pack->blob!=0&&pack->bank_segment!=0&&FP_OFF(pack->blob)==0;
#else
    return pack->blob!=0;
#endif
}
