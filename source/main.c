/* PROJECT NOISE DSI - native Nintendo DS/DSi shell
 * Uses libnds + libfat only. Audio decoding is deliberately isolated in audio.c;
 * this build does not claim a track is playing until a real stream backend exists.
 */
#include <nds.h>
#include <fat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "audio.h"

#define MUSIC_ROOT       "fat:/Music"
#define CONFIG_FILE      "fat:/project_noise.cfg"
#define FAVORITES_FILE   "fat:/project_noise.fav"
#define MAX_ENTRIES      256
#define MAX_FAVORITES    256
#define NAME_LEN         128
#define PATH_LEN         512
#define VISIBLE_ROWS     8

typedef struct { char name[NAME_LEN]; bool directory; } MusicEntry;

typedef enum { PAGE_LIBRARY = 0, PAGE_FAVORITES, PAGE_SETTINGS, PAGE_ABOUT } Page;

static PrintConsole topConsole, bottomConsole;
static MusicEntry entries[MAX_ENTRIES];
static char favorites[MAX_FAVORITES][PATH_LEN];
static int entryCount, favoriteCount, selected, scroll;
static char currentPath[PATH_LEN] = MUSIC_ROOT;
static char nowPlaying[NAME_LEN] = "Nothing selected";
static char statusText[80] = "Put MP3 files in /Music";
static Page page = PAGE_LIBRARY;
static bool shuffleEnabled, repeatEnabled;
static int volume = 96;
static bool audioReady;

static bool is_audio(const char *name) {
    const char *dot = strrchr(name, '.');
    return dot && !strcasecmp(dot, ".mp3");
}
static void path_join(char *out, size_t cap, const char *a, const char *b) {
    snprintf(out, cap, "%s%s%s", a, (a[strlen(a)-1] == '/') ? "" : "/", b);
}
static bool is_favorite(const char *path) {
    for (int i=0; i<favoriteCount; ++i) if (!strcmp(favorites[i], path)) return true;
    return false;
}
static void save_favorites(void) {
    FILE *f=fopen(FAVORITES_FILE,"w"); if(!f) return;
    for(int i=0;i<favoriteCount;++i) fprintf(f,"%s\n",favorites[i]);
    fclose(f);
}
static void load_favorites(void) {
    FILE *f=fopen(FAVORITES_FILE,"r"); char line[PATH_LEN]; favoriteCount=0;
    if(!f) return;
    while(fgets(line,sizeof(line),f) && favoriteCount<MAX_FAVORITES) {
        line[strcspn(line,"\r\n")]=0;
        if(line[0] && !is_favorite(line)) {
            strncpy(favorites[favoriteCount],line,PATH_LEN-1);
            favorites[favoriteCount][PATH_LEN-1]=0; favoriteCount++;
        }
    }
    fclose(f);
}
static void save_config(void) {
    FILE *f=fopen(CONFIG_FILE,"w"); if(!f) return;
    fprintf(f,"shuffle=%d\nrepeat=%d\nvolume=%d\n",shuffleEnabled,repeatEnabled,volume);
    fclose(f);
}
static void load_config(void) {
    FILE *f=fopen(CONFIG_FILE,"r"); char line[80]; if(!f) return;
    while(fgets(line,sizeof(line),f)) {
        int v;
        if(sscanf(line,"shuffle=%d",&v)==1) shuffleEnabled=(v!=0);
        else if(sscanf(line,"repeat=%d",&v)==1) repeatEnabled=(v!=0);
        else if(sscanf(line,"volume=%d",&v)==1 && v>=0 && v<=127) volume=v;
    }
    fclose(f);
}
static void sort_entries(void) {
    for(int i=0;i<entryCount;i++) for(int j=i+1;j<entryCount;j++) {
        bool sw=false;
        if(entries[j].directory && !entries[i].directory) sw=true;
        else if(entries[j].directory==entries[i].directory && strcasecmp(entries[j].name,entries[i].name)<0) sw=true;
        if(sw){MusicEntry t=entries[i];entries[i]=entries[j];entries[j]=t;}
    }
}
static void load_directory(void) {
    DIR *d=opendir(currentPath); struct dirent *it; entryCount=0; selected=scroll=0;
    if(!d){snprintf(statusText,sizeof(statusText),"Cannot open folder");return;}
    while((it=readdir(d)) && entryCount<MAX_ENTRIES) {
        if(!strcmp(it->d_name,".")||!strcmp(it->d_name,"..")) continue;
        char full[PATH_LEN+NAME_LEN+2]; struct stat st;
        path_join(full,sizeof(full),currentPath,it->d_name);
        if(stat(full,&st)) continue;
        bool isdir=S_ISDIR(st.st_mode);
        if(!isdir && !is_audio(it->d_name)) continue;
        strncpy(entries[entryCount].name,it->d_name,NAME_LEN-1);
        entries[entryCount].name[NAME_LEN-1]=0;
        entries[entryCount].directory=isdir; entryCount++;
    }
    closedir(d); sort_entries();
    snprintf(statusText,sizeof(statusText),"%d items",entryCount);
}
static void load_favorite_view(void) {
    entryCount=0; selected=scroll=0;
    for(int i=0;i<favoriteCount && entryCount<MAX_ENTRIES;i++) {
        const char *base=strrchr(favorites[i],'/'); base=base?base+1:favorites[i];
        strncpy(entries[entryCount].name,base,NAME_LEN-1);
        entries[entryCount].name[NAME_LEN-1]=0; entries[entryCount].directory=false; entryCount++;
    }
    snprintf(statusText,sizeof(statusText),"%d favorites",entryCount);
}
static void refresh_page(void) { if(page==PAGE_FAVORITES) load_favorite_view(); else if(page==PAGE_LIBRARY) load_directory(); }
static void go_parent(void) {
    if(!strcmp(currentPath,MUSIC_ROOT)||!strcmp(currentPath,"fat:/")) return;
    char *s=strrchr(currentPath,'/'); if(s && s>currentPath+4) *s=0; else strcpy(currentPath,MUSIC_ROOT);
    load_directory();
}
static void selected_path(char *out,size_t cap) {
    if(selected<0||selected>=entryCount){if(cap)out[0]=0;return;}
    if(page==PAGE_FAVORITES) snprintf(out,cap,"%s",favorites[selected]);
    else path_join(out,cap,currentPath,entries[selected].name);
}
static void toggle_favorite(void) {
    char full[PATH_LEN]; selected_path(full,sizeof(full)); if(!full[0])return;
    for(int i=0;i<favoriteCount;i++) if(!strcmp(favorites[i],full)) {
        for(int j=i;j<favoriteCount-1;j++) strcpy(favorites[j],favorites[j+1]);
        favoriteCount--; save_favorites(); snprintf(statusText,sizeof(statusText),"Removed favorite");
        if(page==PAGE_FAVORITES) load_favorite_view();
        return;
    }
    if(favoriteCount>=MAX_FAVORITES){snprintf(statusText,sizeof(statusText),"Favorites full");return;}
    strncpy(favorites[favoriteCount],full,PATH_LEN-1); favorites[favoriteCount][PATH_LEN-1]=0;
    favoriteCount++; save_favorites(); snprintf(statusText,sizeof(statusText),"Added to favorites");
}
static void select_track(void) {
    if(selected<0||selected>=entryCount)return;
    if(page==PAGE_LIBRARY && entries[selected].directory) {
        char next[PATH_LEN]; path_join(next,sizeof(next),currentPath,entries[selected].name);
        if(strlen(next)<sizeof(currentPath)){strcpy(currentPath,next);load_directory();} return;
    }
    char full[PATH_LEN]; selected_path(full,sizeof(full));
    const char *base=strrchr(full,'/'); base=base?base+1:full;
    strncpy(nowPlaying,base,sizeof(nowPlaying)-1); nowPlaying[sizeof(nowPlaying)-1]=0;
    if (!audioReady) {
        snprintf(statusText,sizeof(statusText),"Audio engine unavailable");
        return;
    }
    if (audio_play(full)) snprintf(statusText,sizeof(statusText),"Playing");
    else snprintf(statusText,sizeof(statusText),"Could not open MP3");
}
static void move_selection(int delta) {
    if(!entryCount) return;
    selected+=delta;
    if(selected<0) selected=0;
    if(selected>=entryCount) selected=entryCount-1;
    if(selected<scroll)scroll=selected;
    if(selected>=scroll+VISIBLE_ROWS)scroll=selected-VISIBLE_ROWS+1;
}
static void draw_top(void) {
    consoleSelect(&topConsole); consoleClear();
    iprintf("PROJECT NOISE\n");
    iprintf("DSI  /  MUSIC PLAYER\n");
    iprintf("--------------------------------\n\n");
    iprintf("SELECTED TRACK\n"); iprintf("%s\n\n",nowPlaying);
    iprintf("[Y] PAUSE   [A] PLAY   [L/R] MODE\n\n");
    iprintf("SHUFFLE: %s\n",shuffleEnabled?"ON":"OFF");
    iprintf("REPEAT : %s\n",repeatEnabled?"ON":"OFF");
    iprintf("VOLUME : %d\n\n",volume);
    iprintf("--------------------------------\n");
    iprintf("MADE BY GRIMZZZZ\n\n");
    iprintf("%s",statusText);
}
static void draw_bottom(void) {
    consoleSelect(&bottomConsole); consoleClear();
    if(page==PAGE_SETTINGS) {
        iprintf("SETTINGS\n--------------------------------\n");
        iprintf("A  Shuffle       %s\n",shuffleEnabled?"ON":"OFF");
        iprintf("X  Repeat        %s\n",repeatEnabled?"ON":"OFF");
        iprintf("Volume pref.    %d/127\n",volume);
        iprintf("START  Save settings\n\n");
        iprintf("L/R or UP/DOWN to change\n"); iprintf("Y: Library  SELECT: Favorites\n");
        return;
    }
    if(page==PAGE_ABOUT) {
        iprintf("ABOUT PROJECT NOISE DSI\n--------------------------------\n\n");
        iprintf("A native Nintendo DS/DSi\n"); iprintf("music player project.\n\n");
        iprintf("MADE BY GRIMZZZZ\n\n");
        iprintf("B: Back   Y: Library\n"); return;
    }
    iprintf("%s\n",page==PAGE_FAVORITES?"FAVORITES":"LIBRARY");
    iprintf("%.30s\n",page==PAGE_LIBRARY?currentPath:"Saved tracks");
    iprintf("--------------------------------\n");
    for(int r=0;r<VISIBLE_ROWS;r++) {
        int idx=scroll+r; iprintf("%c ",idx==selected?'>':' ');
        if(idx<entryCount) {
            if(page==PAGE_LIBRARY && entries[idx].directory) iprintf("[DIR] ");
            else {
                char full[PATH_LEN]; selected_path(full,sizeof(full));
                if(page==PAGE_FAVORITES) snprintf(full,sizeof(full),"%s",favorites[idx]);
                else path_join(full,sizeof(full),currentPath,entries[idx].name);
                iprintf("%c ",is_favorite(full)?'*':' ');
            }
            iprintf("%.25s",entries[idx].name);
        }
        iprintf("\n");
    }
    iprintf("--------------------------------\n");
    iprintf("A Open  B Back  X Favorite\n");
    iprintf("Y Pause  SELECT Favorites\n");
    iprintf("START Settings  L/R Shuffle/Repeat\n");
    iprintf("Tap bottom for Settings / About\n");
    iprintf("%s",statusText);
}
int main(void) {
    videoSetMode(MODE_0_2D); videoSetModeSub(MODE_0_2D);
    vramSetBankA(VRAM_A_MAIN_BG); vramSetBankC(VRAM_C_SUB_BG);
    consoleInit(&topConsole,0,BgType_Text4bpp,BgSize_T_256x256,31,0,true,true);
    consoleInit(&bottomConsole,0,BgType_Text4bpp,BgSize_T_256x256,31,0,false,true);
    if(!fatInitDefault()) {
        consoleSelect(&topConsole); iprintf("PROJECT NOISE DSI\n\nSD INIT FAILED\nCheck SD card and DSi SD support.");
        while(1){swiWaitForVBlank();} return 0;
    }
    load_config(); load_favorites();
    audioReady = audio_init();
    if (audioReady) audio_set_volume(volume);
    else snprintf(statusText,sizeof(statusText),"Audio initialization failed");
    struct stat st;
    if(stat(MUSIC_ROOT,&st)==0 && S_ISDIR(st.st_mode)) strcpy(currentPath,MUSIC_ROOT);
    else strcpy(currentPath,"fat:/");
    load_directory();
    while(1) {
        scanKeys(); int down=keysDown(); touchPosition touch;
        if(page==PAGE_SETTINGS) {
            if(down&(KEY_UP|KEY_LEFT|KEY_L)) { volume-=4; if(volume<0)volume=0; if(audioReady)audio_set_volume(volume); }
            if(down&(KEY_DOWN|KEY_RIGHT|KEY_R)) { volume+=4; if(volume>127)volume=127; if(audioReady)audio_set_volume(volume); }
            if(down&KEY_A)shuffleEnabled=!shuffleEnabled;
            if(down&KEY_X)repeatEnabled=!repeatEnabled;
            if(down&KEY_START){save_config();page=PAGE_LIBRARY;load_directory();}
            if(down&KEY_B){page=PAGE_LIBRARY;load_directory();}
            if(down&KEY_SELECT){page=PAGE_FAVORITES;load_favorite_view();}
        } else if(page==PAGE_ABOUT) {
            if(down&(KEY_B|KEY_Y)){page=PAGE_LIBRARY;load_directory();}
        } else {
            if(down&KEY_UP)move_selection(-1);
            if(down&KEY_DOWN)move_selection(1);
            if(down&KEY_A)select_track();
            if(down&KEY_B){if(page==PAGE_FAVORITES){page=PAGE_LIBRARY;load_directory();}else go_parent();}
            if(down&KEY_X)toggle_favorite();
            if(down&KEY_Y){ if(audioReady && (audio_is_playing() || audio_is_paused())) { audio_toggle_pause(); snprintf(statusText,sizeof(statusText),audio_is_paused()?"Paused":"Playing"); } else { page=PAGE_LIBRARY;load_directory(); } }
            if(down&KEY_SELECT){page=PAGE_FAVORITES;load_favorite_view();}
            if(down&KEY_START){page=PAGE_SETTINGS;}
            if(down&KEY_L){shuffleEnabled=!shuffleEnabled;save_config();}
            if(down&KEY_R){repeatEnabled=!repeatEnabled;save_config();}
            if(down&KEY_TOUCH){
                touchRead(&touch);
                if(touch.py>=36 && touch.py<132){int row=(touch.py-36)/12;int idx=scroll+row;if(idx<entryCount){selected=idx;select_track();}}
                else if(touch.py>=144 && touch.py<168) page=PAGE_SETTINGS;
                else if(touch.py>=168) page=PAGE_ABOUT;
            }
        }
        if(audioReady) audio_update();
        if(audioReady && audio_finished()) snprintf(statusText,sizeof(statusText),"Track finished");
        draw_top(); draw_bottom(); swiWaitForVBlank();
    }
    return 0;
}
