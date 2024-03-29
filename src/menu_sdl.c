/*********************************************************************
 * Copyright (C) 2012,  Fabio Olimpieri
 * Copyright (C) 2009,  Simon Kagstrom
 *
 * Filename:      menu_sdl.c
 * 
 * Description:   Code for menus (originally for Mophun)
 *
 * This file is part of FBZX Wii
 *
 * FBZX Wii is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * FBZX Wii is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 ********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>
#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>
#include<SDL/SDL_image.h>
#include <stdint.h>

#include "menu_sdl.h"
#include "emulator.h"
#include "VirtualKeyboard.h"

#include "characters.h"

#include "minizip/unzip.h"
#include "tape_browser.h"
#include "cargador.h"
#include "sound.h"
#include "rzx_lib/rzx.h"
#include "rzx_init.h"


#if defined(HW_RVL)
#include <wiiuse/wpad.h> 
#include <asndlib.h>
#endif

#if defined(HW_DOL)
#include <ogc/pad.h> 
#include <asndlib.h>
#endif

#ifdef DEBUG
extern FILE *fdebug;
#define printf(...) fprintf(fdebug,__VA_ARGS__)
#else
 #ifdef GEKKO
 #define printf(...)
 #endif
#endif

typedef struct
{
	int n_entries;
	int index;
	int sel;
} submenu_t;

typedef struct
{
	char title[MAX_PATH_LENGTH];
	const char **pp_msgs;
	TTF_Font  *p_font;
	int        x1,y1;
	int        x2,y2;
	int        text_w;
	int        text_h;

	int        n_submenus;
	submenu_t *p_submenus;

	int        cur_sel; /* Main selection */
	int        start_entry_visible;
	int        n_entries;
} menu_t;

static SDL_Surface *real_screen;

#define IS_SUBMENU(p_msg) ( (p_msg)[0] == '^' )
#define IS_TEXT(p_msg) ( (p_msg)[0] == '#' || (p_msg)[0] == ' ' )
#define IS_MARKER(p_msg) ( (p_msg)[0] == '@' )

static int is_inited = 0;
static TTF_Font *menu_font_alt_large, *menu_font_large, *menu_font_alt_small, *menu_font_small;

int fh, fw;

static int *click_buffer_pointer[3];
static int len_click_buffer[3];
static SDL_Surface *image_stripes, *image_stripes_small,*tmp_surface;

int msgInfo(char *text, int duration, SDL_Rect *irc)
{
	int len = strlen(text);
	int X, Y, w, h;
	SDL_Rect src;
	SDL_Rect rc;
	SDL_Rect brc;
	
	if (RATIO==1) TTF_SizeText(menu_font_large, "Z", &w, &h);  else TTF_SizeText(menu_font_small, "Z", &w, &h);

	X = (FULL_DISPLAY_X /2) - (len / 2 + 1)*w;
	Y = (FULL_DISPLAY_Y /2) - h;

	brc.x = FULL_DISPLAY_X/2-2*w-2/RATIO; 
	brc.y=Y+h*2-4/RATIO;
	brc.w=w*4;
	brc.h=h*3/2;

	rc.x = X; 
	rc.y=Y;
	rc.w=w*(len + 2);
	rc.h=duration >= 0 ? h*2 : h*4;

	src.x=rc.x+2/RATIO;
	src.y=rc.y+2/RATIO;
	src.w=rc.w-4/RATIO;
	src.h=rc.h-4/RATIO;


	if (irc)
	{
		irc->x=rc.x;
		irc->y=rc.y;
		irc->w=src.w;
		irc->h=src.h;
	}	
	SDL_FillRect(real_screen, &rc, SDL_MapRGB(real_screen->format, 255, 255, 0));
	SDL_FillRect(real_screen, &src, SDL_MapRGB(real_screen->format, 255, 255, 255));
	menu_print_font(real_screen, 0,0,0, X+w, Y+h/2, text,FONT_NORM,64);
	SDL_UpdateRect(real_screen, src.x, src.y, src.w, src.h);
	SDL_UpdateRect(real_screen, rc.x, rc.y, rc.w,rc.h);
	if (duration > 0)
		SDL_Delay(duration);
	else if (duration < 0)
	{
		SDL_FillRect(real_screen, &brc, SDL_MapRGB(real_screen->format, 0x00, 0x80, 0x00));
		menu_print_font(real_screen, 0,0,0, FULL_DISPLAY_X/2-w, Y+h*2, "OK",FONT_NORM,64);
		SDL_UpdateRect(real_screen, brc.x, brc.y, brc.w, brc.h);
		while (!(KEY_SELECT & menu_wait_key_press())) {}

	}

	return 1;
}

/*
void msgKill(SDL_Rect *rc)
{
	SDL_UpdateRect(real_screen, rc->x, rc->y, rc->w,rc->h);
}
*/

int msgYesNo(char *text, int default_opt, int x, int y)
{
	int len = strlen(text);
	int X, Y, w, h;
	SDL_Rect src;
	SDL_Rect rc;
	SDL_Rect brc;
	uint32_t key;
	
	if (RATIO==1) TTF_SizeText(menu_font_large, "Z", &w, &h);  else TTF_SizeText(menu_font_small, "Z", &w, &h);

	if (x < 0)
		X = (FULL_DISPLAY_X /2) - (len / 2 + 1)*w;
	else
		X = x;

	if (y < 0)	
		Y = (FULL_DISPLAY_Y /2) - h*2;
	else
		Y = y;

	rc.x=X; 
	rc.y=Y;
	rc.w=w*(len + 2);
	rc.h=h*4;

	src.x=rc.x+2/RATIO;
	src.y=rc.y+2/RATIO;
	src.w=rc.w-4/RATIO;
	src.h=rc.h-4/RATIO;

	while (1)
	{	
		SDL_FillRect(real_screen, &rc, SDL_MapRGB(real_screen->format, 255, 255, 0));
		SDL_FillRect(real_screen, &src, SDL_MapRGB(real_screen->format, 255, 255, 255));
		menu_print_font(real_screen, 0,0,0, X+w, Y+h/2, text,FONT_NORM,64);

		if (default_opt) //"YES"
		{
			brc.x=rc.x + rc.w/2-5*w-2/RATIO; 
			brc.y=rc.y+h*2-4/RATIO;
			brc.w=w*3;
			brc.h=h*3/2;
			SDL_FillRect(real_screen, &brc, SDL_MapRGB(real_screen->format, 0x00, 255, 0x00));
		}
		else //"NO"
		{
			brc.x=rc.x + rc.w/2+5*w-2*w-2/RATIO; 
			brc.y=rc.y+h*2-4/RATIO;
			brc.w=w*2;
			brc.h=h*3/2;
			SDL_FillRect(real_screen, &brc, SDL_MapRGB(real_screen->format, 255, 0x00, 0x00));
		}
	
		menu_print_font(real_screen, 0,0,0, rc.x + rc.w/2-5*w, Y+h*2, "YES",FONT_NORM,64);
		menu_print_font(real_screen, 0,0,0, rc.x + rc.w/2+5*w-2*w, Y+h*2, "NO",FONT_NORM,64);
		
		SDL_UpdateRect(real_screen, src.x, src.y, src.w, src.h);
		SDL_UpdateRect(real_screen, rc.x, rc.y, rc.w,rc.h);
		SDL_UpdateRect(real_screen, brc.x, brc.y, brc.w,brc.h);

		//SDL_Flip(real_screen);
		key = menu_wait_key_press();
		if (key & KEY_SELECT)
		{
			play_click(1);
			return default_opt;
		}
		else if (key & KEY_ESCAPE)
		{
			play_click(2);
			return 0;
		}
		else if (key & KEY_LEFT)
		{
			play_click(0);
			default_opt = !default_opt;
		}
		else if (key & KEY_RIGHT)
		{
			play_click(0);
			default_opt = !default_opt;
		}
	}
}



static int cmpstringp(const void *p1, const void *p2)
{
	const char *p1_s = *(const char**)p1;
	const char *p2_s = *(const char**)p2;

	/* Put directories first */
	if (*p1_s == '[' && *p2_s != '[')
		return -1;
	if (*p1_s != '[' && *p2_s == '[')
		return 1;
	return strcasecmp(* (char * const *) p1, * (char * const *) p2);
}

/* Return true if name ends with ext (for filenames) */
int ext_matches(const char *name, const char *ext)
{
	int len = strlen(name);
	int ext_len = strlen(ext);

	if (len <= ext_len)
		return 0;
	return (strcmp(name + len - ext_len, ext) == 0);
}

static int ext_matches_list(const char *name, const char **exts)
{
	const char **p;

	for (p = exts; *p; p++)
	{
		if (ext_matches(name, *p))
			return 1;
	}

	return 0;
}

static const char **get_file_list_zip(const char *path)
{
	unzFile uf = unzOpen(path);
	unz_global_info gi;
	const char **file_list;
	int err, cur=0;
	
	if (!uf) return NULL;

	err = unzGetGlobalInfo (uf,&gi);
    if (err!=UNZ_OK) printf("error %d with zipfile in unzGetGlobalInfo \n",err);
	
	file_list = (const char**)malloc((gi.number_entry +3) * sizeof(char*));
	file_list[cur++] = strdup("None");
	file_list[cur++] = strdup("[..]");
	file_list[cur] = NULL;

	for (cur=2;cur<gi.number_entry+2;cur++)
    {
        char filename_inzip[MAX_PATH_LENGTH];
        unz_file_info file_info;
   
        err = unzGetCurrentFileInfo(uf,&file_info,filename_inzip,sizeof(filename_inzip),NULL,0,NULL,0);
        if (err!=UNZ_OK)
        {
            printf("Error %d with zipfile in unzGetCurrentFileInfo\n",err);
            break;
        }
		
		const char *exts[] = {".tap", ".TAP", ".tzx", ".TZX", ".z80",".Z80",".sna", ".SNA", "rzx", "RZX",
				".mdr", ".MDR", ".scr", ".SCR", ".conf", ".CONF",".pok", ".POK" ,".rom", ".ROM", NULL};

		if (ext_matches_list(filename_inzip, exts))
		{
			char *p;

			p = strdup(filename_inzip);
			file_list[cur] = p;
			file_list[cur+1] = NULL;
		}
		
		if (cur<gi.number_entry+1)
        {
            err = unzGoToNextFile(uf);
            if (err!=UNZ_OK)
            {
                printf("error %d with zipfile in unzGoToNextFile\n",err);
                break;
            }
        }

	}
	
	unzClose(uf);

    //qsort(&file_list[2], gi.number_entry, sizeof(const char *), cmpstringp);

    return file_list;
}


static const char **get_file_list(const char *base_dir)
{
	DIR *d = opendir(base_dir);
	const char **file_list, **realloc_file_list;
	int cur = 0;
	struct dirent *de;
	int cnt = 16;

	if (!d)
		return NULL;

	file_list = (const char**)malloc(cnt * sizeof(char*));
	file_list[cur++] = strdup("None"); 
	file_list[cur] = NULL;

	for (de = readdir(d);
	de;
	de = readdir(d))
	{
		char buf[255];
		const char *exts[] = {".tap", ".TAP", ".tzx", ".TZX", ".z80",".Z80",".sna", ".SNA", "rzx", "RZX",
				".mdr", ".MDR", ".scr", ".SCR", ".conf", ".CONF",".pok", ".POK", ".zip", ".ZIP",".rom", ".ROM",NULL};
		struct stat st;

		snprintf(buf, 255, "%s/%s", base_dir, de->d_name);
		if (stat(buf, &st) < 0)
			continue;
		if (S_ISDIR(st.st_mode)&&strcmp(".", de->d_name))
		{
			char *p;
			size_t len = strlen(de->d_name) + 4;

			p = (char*)malloc( len );
			if (p==NULL) break; //Terminate the list
			snprintf(p, len, "[%s]", de->d_name);
			file_list[cur++] = p;
			file_list[cur] = NULL;
		}
		else if (ext_matches_list(de->d_name, exts))
		{
			char *p;

			p = strdup(de->d_name);
			if (p==NULL) break; //Terminate the list
			file_list[cur++] = p;
			file_list[cur] = NULL;
		}

		if (cur > cnt - 2)
		{
			cnt = cnt + 32;
			realloc_file_list = (const char**)realloc(file_list, cnt * sizeof(char*));
			if (realloc_file_list) file_list = realloc_file_list; else break;
				
		}
	}
	closedir(d);
        qsort(&file_list[1], cur-1, sizeof(const char *), cmpstringp);

        return file_list;
}


static submenu_t *find_submenu(menu_t *p_menu, int index)
{
	int i;

	for (i=0; i<p_menu->n_submenus; i++)
	{
		if (p_menu->p_submenus[i].index == index)
			return &p_menu->p_submenus[i];
	}

	return NULL;
}

void menu_print_font(SDL_Surface *screen, int r, int g, int b,
		int x, int y, const char *msg, int font_type, int max_string)
{
	SDL_Surface *font_surf;
	SDL_Rect dst = {x, y,  0, 0};
	SDL_Color color = {r, g, b, 0};
	char buf[255];
	unsigned int i, length;

	memset(buf, 0, sizeof(buf));
	strncpy(buf, msg, 254);
	if (buf[0] != '|' && buf[0] != '^' && buf[0] != '.'
		&& buf[0] != '-' && buf[0] != ' ' && !strstr(buf, "  \""))
	{
		length = strlen(buf); 
		if (length>max_string) buf[max_string]=0;
	}
	/* Fixup multi-menu option look */
	for (i = 0; i < strlen(buf) ; i++)
	{
		if (buf[i] == '^' || buf[i] == '|')
			buf[i] = ' ';
	}

	if (FULL_DISPLAY_X == 640)
		{
		if (font_type == FONT_ALT) font_surf = TTF_RenderUTF8_Blended(menu_font_alt_large, buf, color);
		else font_surf = TTF_RenderUTF8_Blended(menu_font_large, buf, color);
		}
	else 	
		{
		if (font_type == FONT_ALT) font_surf = TTF_RenderUTF8_Blended(menu_font_alt_small, buf, color);
		else font_surf = TTF_RenderUTF8_Blended(menu_font_small, buf, color);
		}
		
	if (!font_surf)
	{
		fprintf(stderr, "%s\n", TTF_GetError());
		exit(1);
	}

	SDL_BlitSurface(font_surf, NULL, screen, &dst);
	SDL_FreeSurface(font_surf);
}

void print_font(SDL_Surface *screen, int r, int g, int b,
		int x, int y, const char *msg, int font_type)
{
#define _MAX_STRING 52
	SDL_Surface *font_surf;
	SDL_Rect dst = {x, y,  0, 0};
	SDL_Color color = {r, g, b, 0};
	char buf[255];

	memset(buf, 0, sizeof(buf));
	strncpy(buf, msg, 254);
	
	
		if (strlen(buf)>_MAX_STRING)
		{
			buf[_MAX_STRING] = '\0';
		}


	if (FULL_DISPLAY_X == 640)
		{
		if (font_type == FONT_ALT) font_surf = TTF_RenderUTF8_Blended(menu_font_alt_large, buf, color);
		else font_surf = TTF_RenderUTF8_Blended(menu_font_large, buf, color);
		}
	else 	
		{
		if (font_type == FONT_ALT) font_surf = TTF_RenderUTF8_Blended(menu_font_alt_small, buf, color);
		else font_surf = TTF_RenderUTF8_Blended(menu_font_small, buf, color);
		}
		
	if (!font_surf)
	{
		fprintf(stderr, "%s\n", TTF_GetError());
		exit(1);
	}

	SDL_BlitSurface(font_surf, NULL, screen, &dst);
	SDL_FreeSurface(font_surf);
}

int extract_screen(char* screen, const char* name)
{
	FILE *fichero;
	char filename[MAX_PATH_LENGTH];
	char char_id[10];
	int retorno;
	
	if ((ext_matches(name, ".tap")||ext_matches(name, ".TAP")))
	{
		sprintf(filename,"%s/%s",load_path_taps, name);
		fichero=fopen(filename,"rb");
		if (!fichero) //Try in the tmp zip directory
		{
			sprintf(filename,"%s/%s",path_tmp, name);
			fichero=fopen(filename,"rb");
			if (!fichero) return -1;
		}
		retorno = extract_screen_tap(screen, fichero);
		fclose(fichero);
		return retorno;
	}
	
	if ((ext_matches(name, ".tzx")||ext_matches(name, ".TZX")))
	{
		sprintf(filename,"%s/%s",load_path_taps, name);
		fichero=fopen(filename,"rb");
		if (!fichero) //Try in the tmp zip directory
		{
			sprintf(filename,"%s/%s",path_tmp, name);
			fichero=fopen(filename,"rb");
			if (!fichero) return -1;
		}
		fread(char_id,10,1,fichero); // read the (maybe) TZX header
		if((strncmp(char_id,"ZXTape!",7)) || (char_id[7]!=0x1A) || (char_id[8]!=1)) 
		{fclose(fichero);retorno = -1;};
		retorno = extract_screen_tzx(screen, fichero);
		fclose(fichero);
		return retorno;
	}

	if (ext_matches(name, ".z80")||ext_matches(name, ".Z80")||ext_matches(name, ".sna")||ext_matches(name, ".SNA"))
	{
		sprintf(filename,"%s/%s",load_path_snaps, name);
		fichero=fopen(filename,"rb");
		if (!fichero) //Try in the tmp zip directory
		{
			sprintf(filename,"%s/%s",path_tmp, name);
			fichero=fopen(filename,"rb");
			if (!fichero) return -1;
		}
		if (ext_matches(name, ".z80")||ext_matches(name, ".Z80")) retorno = extract_screen_z80(screen, fichero);
		else retorno = extract_screen_sna(screen, fichero);
		fclose(fichero);
		return retorno;
	}
	
	return -1;
}

void draw_scr_file(int x,int y, const char *selected_file, int which)
{
	FILE *fichero;
	char screen [6912];
	unsigned int *p_translt, *p_translt2;
	unsigned char attribute, ink, paper, mask, octect;
	int loop_x, loop_y,bucle,valor,*p, length;
	unsigned char *address;
	char name[MAX_PATH_LENGTH];
	char filename[MAX_PATH_LENGTH];
	char *ptr;
	
	if (selected_file==NULL) // Aborted
		return; 
	 
	strcpy(name,selected_file);	
	
	if ((ext_matches(name, ".zip")||ext_matches(name, ".ZIP"))) 
	{
		//remove the zip extension
		ptr = strrchr (name, '.');	
		if (ptr) *ptr = 0;	
	}
	
	//remove the other extensions
	ptr = strrchr (name, '.');	
	if (ptr) *ptr = 0;		
	
	//Always load from Default device
	strcpy(filename,getenv("HOME"));
	length=strlen(filename);
	if ((length>0)&&(filename[length-1]!='/'))
		strcat(filename,"/");
	if (which==0) strcat(filename, "scr/"); else strcat(filename, "scr2/");
	strcat(filename, name);
	strcat(filename, ".scr");	
		
	if (which) //second SCR
	{
		fichero=fopen(filename,"rb");

		if (!fichero) return;
	
		if (fread(screen,1,6912,fichero)!=6912) {fclose(fichero);return;}
		fclose(fichero);
	}
	else //first SCR
	{
		fichero=fopen(filename,"rb");
		
		if (!fichero) 
			{if (extract_screen(screen, selected_file)) return;}
		else
		{
			if (fread(screen,1,6912,fichero)!=6912) {fclose(fichero);return;}
			fclose(fichero);
		}
	}
	
	
	p_translt = ordenador.translate;
	p_translt2 = ordenador.translate2;
	
	for (loop_y=0; loop_y<192;loop_y++)
		for(loop_x=0; loop_x<32; loop_x++)
		{

		attribute = screen[(*p_translt2)-147456];	// attribute
	
		ink = attribute & 0x07;	// ink colour
		paper = (attribute >> 3) & 0x07;	// paper colour
			
		octect = screen[(*p_translt)-147456];	// bitmap
		mask = 0x80;
	 
		for (bucle = 0; bucle < 8; bucle++)
			{
			valor = (octect & mask) ? (int) ink : (int) paper;
			p=(colors+valor);
			
			address = (unsigned char *)(ordenador.screen->pixels + (x + loop_x*8 + bucle + (y + loop_y)*640)*ordenador.bpp);
		
			paint_one_pixel((unsigned char *)p, address);
		
			mask = ((mask >> 1) & 0x7F);
			}
		
		p_translt++;
		p_translt2++;
		}
}

int quit_thread;

struct  
{ 
	SDL_Surface *screen; 
	int x; 
	int y; 
	SDL_Rect r;
	const char *msg;  
	int font_type;
	int max_string;
	int browser;
} thread_struct;


int menu_thread(void * data) 
{ 

	int i , a;
	
	SDL_Delay(30);
	
	while( quit_thread == 0 )
	{
		for (i=0; i<=(strlen(thread_struct.msg)-thread_struct.max_string);i++)
		{
			SDL_FillRect(screen, &thread_struct.r, SDL_MapRGB(screen->format, 0, 255, 255));
			if (thread_struct.browser)
			menu_print_font(thread_struct.screen, 255, 0, 0, thread_struct.x, thread_struct.y, //Selected menu entry begining with ']' (tape browser)
				thread_struct.msg+i, thread_struct.font_type, thread_struct.max_string);
			else
			menu_print_font(thread_struct.screen, 0, 0, 0, thread_struct.x, thread_struct.y, 
				thread_struct.msg+i, thread_struct.font_type, thread_struct.max_string);
			SDL_UpdateRect(thread_struct.screen, thread_struct.r.x, 
				thread_struct.r.y, thread_struct.r.w, thread_struct.r.h);
				for (a=0; a<10;a++)
					{
					if (i==0) SDL_Delay(80); else SDL_Delay(30);
					if (quit_thread) return 0;
					}
		}
		for (a=0; a<10;a++)
		{
			SDL_Delay(60);
			if (quit_thread) return 0;
		}
	} 
return 0;
}

static void menu_draw(SDL_Surface *screen, menu_t *p_menu, int sel, int font_type, int draw_scr)
{
	static SDL_Thread *thread = NULL;  
	quit_thread = 1;
	
	//int font_height = TTF_FontHeight(p_menu->p_font);
	//int line_height = (font_height + font_height / 2);
	int line_height = 22/ RATIO;
	int x_start = p_menu->x1+4/RATIO;
	int y_start;
	SDL_Rect r;
	int entries_visible = (p_menu->y2 - p_menu->y1-10/RATIO) / line_height - 1;
	const char *selected_file = NULL;
	int i, y, max_string;
	
	if (font_type==FONT_ALT) y_start = p_menu->y1 + line_height+2/RATIO;
	else y_start = p_menu->y1 + line_height+4/RATIO;
	
	if ((draw_scr)&&(RATIO==1)&&ordenador.show_preview) max_string = 30; else max_string = 52;

	//if ( p_menu->n_entries * line_height > p_menu->y2 )
		//y_start = p_menu->y1 + line_height;

	if (p_menu->cur_sel - p_menu->start_entry_visible > entries_visible)
	{
		while (p_menu->cur_sel - p_menu->start_entry_visible > entries_visible)
		{
			p_menu->start_entry_visible ++;
			if (p_menu->start_entry_visible > p_menu->n_entries)
			{
				p_menu->start_entry_visible = 0;
				break;
			}
		}
	}
	else if ( p_menu->cur_sel < p_menu->start_entry_visible )
		p_menu->start_entry_visible = p_menu->cur_sel;

	if (strlen(p_menu->title))
	{
		r.x = p_menu->x1;
		r.y = p_menu->y1;
		r.w = p_menu->x2 - p_menu->x1;
		r.h = line_height;
		if (sel < 0)
			SDL_FillRect(screen, &r, SDL_MapRGB(screen->format, 0x40, 0x00, 0x00));
		else
			SDL_FillRect(screen, &r, SDL_MapRGB(screen->format, 255, 255, 0)); //Title
		if (font_type==FONT_ALT) menu_print_font(screen, 0,0,0, p_menu->x1+4/RATIO, p_menu->y1+2/RATIO, p_menu->title, font_type, 50);
		else menu_print_font(screen, 0,0,0, p_menu->x1+4/RATIO, p_menu->y1+4/RATIO, p_menu->title, font_type, 50);
	}

	for (i = p_menu->start_entry_visible; i <= p_menu->start_entry_visible + entries_visible; i++)
	{
		const char *msg = p_menu->pp_msgs[i];

		if (i >= p_menu->n_entries)
			break;
		if (IS_MARKER(msg))
			p_menu->cur_sel = atoi(&msg[1]);
		else
		{
			y = (i - p_menu->start_entry_visible) * line_height;
			r.x = p_menu->x1+2/RATIO;
			r.y = p_menu->y1 + line_height +y;
			if ((draw_scr)&&(RATIO==1)&&ordenador.show_preview) r.w = 365; //Only in 640 mode
			else r.w = p_menu->x2 - p_menu->x1-4/RATIO;
			r.h = line_height;
			
			if (sel < 0)
				menu_print_font(screen, 0x40,0x40,0x40, //Not used
						x_start, y_start + y, msg, font_type, max_string);
			else if (p_menu->cur_sel == i) /* Selected - color */
				{
					SDL_FillRect(screen, &r, SDL_MapRGB(screen->format, 0, 255, 255));
					if (msg[0] == ']') //Tape browser
					{
					 menu_print_font(screen, 255,0,0, //Selected menu entry begining with ']' (tape browser)
						x_start, y_start + y, msg+1, font_type,max_string ); //do not show ']'
						
						if (strlen(msg)-1>max_string)
						{
							if (thread) SDL_WaitThread(thread, NULL);
							thread_struct.screen=screen;
							thread_struct.x=x_start;
							thread_struct.y=y_start + y;
							thread_struct.msg=msg+1; //do not show ']'
							thread_struct.font_type=font_type;
							thread_struct.max_string=max_string;
							thread_struct.r=r;
							thread_struct.browser=1;
							quit_thread=0;
							thread = SDL_CreateThread(menu_thread, NULL );
						}
					}
					else
					{
					 menu_print_font(screen, 0,0,0, //Selected menu entry
						x_start, y_start + y, msg, font_type,max_string);
						
						if (strlen(msg)>max_string)
						{
							if (thread) SDL_WaitThread(thread, NULL);
							thread_struct.screen=screen;
							thread_struct.x=x_start;
							thread_struct.y=y_start + y;
							thread_struct.msg=msg;
							thread_struct.font_type=font_type;
							thread_struct.max_string=max_string;
							thread_struct.r=r;
							thread_struct.browser=0;
							quit_thread=0;
							thread = SDL_CreateThread(menu_thread, NULL );
						}
					}	
					selected_file = msg;	
				}	
			else if (IS_SUBMENU(msg))
			{
				if (p_menu->cur_sel == i-1)
					{
					SDL_FillRect(screen, &r, SDL_MapRGB(screen->format, 0, 255, 255));
					menu_print_font(screen, 0,0,0, //Selected sub menu entry
							x_start, y_start + y, msg, font_type, max_string);
					}		
				else
					menu_print_font(screen, 255,255,255, //Non selected sub menu entry
							x_start, y_start + y, msg, font_type, max_string);
			}
			else if (msg[0] == '#')
			{
				switch (msg[1])
				{
				case '1':
					menu_print_font(screen, 255,255,0, //Text 1
							x_start, y_start + y, msg+2, font_type, max_string);
					break;
				case '2':
					menu_print_font(screen, 255,255,255, //Text 2
							x_start, y_start + y, msg+2, font_type, max_string);
					break;
				default:
					menu_print_font(screen, 0x40,0x40,0x40,
							x_start, y_start + y, msg, font_type, max_string);
					break;							
				}
			}
			else if (msg[0] == ']')
				menu_print_font(screen, 255,0,0, //Non selected menu entry starting with ']' (tape browser)
							x_start, y_start + y, msg+1, font_type, max_string);
			
			else /* Otherwise white */
				menu_print_font(screen, 255,255,255, //Non selected menu entry
						x_start, y_start + y, msg, font_type, max_string);
			if (IS_SUBMENU(msg))
			{
				submenu_t *p_submenu = find_submenu(p_menu, i);
				int n_pipe = 0;
				int n;

				for (n=0; msg[n] != '\0'; n++)
				{
					/* Underline the selected entry */
					if (msg[n] == '|')
					{
						int16_t n_chars;

						for (n_chars = 1; msg[n+n_chars] && msg[n+n_chars] != '|'; n_chars++);

						n_pipe++;
						if (p_submenu->sel == n_pipe-1)
						{
							int w;
							int h;

							if (TTF_SizeText(p_menu->p_font, "Z", &w, &h) < 0)
							{
								fprintf(stderr, "%s\n", TTF_GetError());
								exit(1);
							}

							r = (SDL_Rect){ x_start + (n+1) * w-2/RATIO, y_start + (i+ 1 - p_menu->start_entry_visible) *line_height -8/RATIO, (n_chars - 1) * w, 2/RATIO};
							if (p_menu->cur_sel == i-1)
								SDL_FillRect(screen, &r,
										SDL_MapRGB(screen->format, 255,0,0)); //Underline selected text
							else
								SDL_FillRect(screen, &r,
										SDL_MapRGB(screen->format, 255,255,255));//Underline non selected text
							break;
						}
					}
				}
			}
		}
	}
	
	if ((draw_scr)&&(RATIO==1)&&ordenador.show_preview) //Only in 640 mode
	{
	r.x = 367;
	r.y = 39;
	r.w = 2;
	r.h = 423;
	
	SDL_FillRect(screen, &r, SDL_MapRGB(screen->format, 255, 255, 0)); //Frame for scr preview
	r.x = 369;
	r.y = 249;
	r.w = 270;
	r.h = 2;
	
	SDL_FillRect(screen, &r, SDL_MapRGB(screen->format, 255, 255, 0)); //Frame for scr preview
	
	if ((!selected_file)||(selected_file[0] == '[')) return; //No dir
	

	draw_scr_file(375,48, selected_file,0);
	draw_scr_file(375,260, selected_file,1);
	}	
}

static int get_next_seq_y(menu_t *p_menu, int v, int dy, int cicle)
{
	if (v + dy < 0)
	 {if (cicle) return (p_menu->n_entries - 1); else return 0;}
	 
	if (v + dy > p_menu->n_entries - 1)
		{if (cicle) return 0; else return (p_menu->n_entries - 1);}
	return v + dy;
}

static void select_next(menu_t *p_menu, int dx, int dy, int cicle)
{
	int next;


	p_menu->cur_sel = get_next_seq_y(p_menu, p_menu->cur_sel, dy, cicle);
	next = get_next_seq_y(p_menu, p_menu->cur_sel, dy + 1, cicle);
	if (IS_SUBMENU(p_menu->pp_msgs[p_menu->cur_sel])&&(dy!=1)&&(dy!=-1)) p_menu->cur_sel--;
	if (p_menu->pp_msgs[p_menu->cur_sel][0] == ' ' ||
			p_menu->pp_msgs[p_menu->cur_sel][0] == '#' ||
			IS_SUBMENU(p_menu->pp_msgs[p_menu->cur_sel]) )
		select_next(p_menu, dx, dy, cicle);

	/* If the next is a submenu */
	if (dx != 0 && IS_SUBMENU(p_menu->pp_msgs[next]))
	{
		submenu_t *p_submenu = find_submenu(p_menu, next);

		p_submenu->sel = (p_submenu->sel + dx) < 0 ? p_submenu->n_entries - 1 :
		(p_submenu->sel + dx) % p_submenu->n_entries;
	}
	else if (dx == -1 && !strcmp(p_menu->pp_msgs[0], "[..]"))
		p_menu->cur_sel = 0;
}

static void select_one(menu_t *p_menu, int sel)
{
	if (sel >= p_menu->n_entries)
		sel = 0;
	p_menu->cur_sel = sel;
	if (p_menu->pp_msgs[p_menu->cur_sel][0] == ' ' ||
			p_menu->pp_msgs[p_menu->cur_sel][0] == '#' ||
			IS_SUBMENU(p_menu->pp_msgs[p_menu->cur_sel]))
		select_next(p_menu, 0, 1 , 1);
}

/*
static int is_submenu_title(menu_t *p_menu, int n)
{
	if (n+1 >= p_menu->n_entries)
		return 0;
	else
		return IS_SUBMENU(p_menu->pp_msgs[n+1]);
}
*/

static void menu_init_internal(menu_t *p_menu, const char *title,
		TTF_Font *p_font, const char **pp_msgs,
		int16_t x1, int16_t y1, int16_t x2, int16_t y2)
{
	int submenu;
	int j;

	memset(p_menu, 0, sizeof(menu_t));

	p_menu->pp_msgs = pp_msgs;
	p_menu->p_font = p_font;
	p_menu->x1 = x1;
	p_menu->y1 = y1;
	p_menu->x2 = x2;
	p_menu->y2 = y2;

	p_menu->text_w = 0;
	p_menu->n_submenus = 0;
	strcpy(p_menu->title, title);

	for (p_menu->n_entries = 0; p_menu->pp_msgs[p_menu->n_entries]; p_menu->n_entries++)
	{
		int text_w_font;

		/* Is this a submenu? */
		if (IS_SUBMENU(p_menu->pp_msgs[p_menu->n_entries]))
		{
			p_menu->n_submenus++;
			continue; /* Length of submenus is unimportant */
		}

		if (TTF_SizeText(p_font, p_menu->pp_msgs[p_menu->n_entries], &text_w_font, NULL) != 0)
		{
			fprintf(stderr, "%s\n", TTF_GetError());
			exit(1);
		}
		if (text_w_font > p_menu->text_w)
			p_menu->text_w = text_w_font;
	}
	if (p_menu->text_w > p_menu->x2 - p_menu->x1)
		p_menu->text_w = p_menu->x2 - p_menu->x1;

	if ( !(p_menu->p_submenus = (submenu_t *)malloc(sizeof(submenu_t) * p_menu->n_submenus)) )
	{
		perror("malloc failed!\n");
		exit(1);
	}

	j=0;
	submenu = 0;
	for (; j < p_menu->n_entries; j++)
	{
		if (IS_SUBMENU(p_menu->pp_msgs[j]))
		{
			int n;

			p_menu->p_submenus[submenu].index = j;
			p_menu->p_submenus[submenu].sel = 0;
			p_menu->p_submenus[submenu].n_entries = 0;
			for (n=0; p_menu->pp_msgs[j][n] != '\0'; n++)
			{
				if (p_menu->pp_msgs[j][n] == '|')
					p_menu->p_submenus[submenu].n_entries++;
			}
			submenu++;
		}
	}
	p_menu->text_h = p_menu->n_entries * (TTF_FontHeight(p_font) + TTF_FontHeight(p_font) / 2);
}

static void menu_fini(menu_t *p_menu)
{
	free(p_menu->p_submenus);
}

uint32_t menu_wait_key_press()
{
	SDL_Event ev;
	uint32_t keys = 0;

	while (1)
	{
		int i, hats, nr;
		SDL_Joystick *joy;
		static int joy_keys_changed;
		static int joy_keys_last;
		static int joy_bottons_last[2][8];
		SDL_JoystickUpdate();
		
		#ifdef HW_DOL
		int SDL_PrivateMouseMotion(Uint8 buttonstate, int relative, Sint16 x, Sint16 y);
		if (SDL_JoystickGetAxis(ordenador.joystick_sdl[0], 2) > 16384) SDL_PrivateMouseMotion(0,1,4/RATIO,0); //C-stick Horizontal axix
		if (SDL_JoystickGetAxis(ordenador.joystick_sdl[0], 2) < -16384) SDL_PrivateMouseMotion(0,1,-4/RATIO,0); //C-stick Horizontal axix
		if (SDL_JoystickGetAxis(ordenador.joystick_sdl[0], 3) > 16384) SDL_PrivateMouseMotion(0,1,0,4/RATIO); //C-stick vertical axix
		if (SDL_JoystickGetAxis(ordenador.joystick_sdl[0], 3) < -16384) SDL_PrivateMouseMotion(0,1,0,-4/RATIO); //C-stick vertical axix
		#endif
		
		/* Wii-specific, sorry */
		for (nr = 0; nr < ordenador.joystick_number; nr++) {
			joy = ordenador.joystick_sdl[nr];
			if (!joy)
				continue;
	
			hats = SDL_JoystickNumHats (joy); 
			for (i = 0; i < hats; i++) {
				Uint8 v = SDL_JoystickGetHat (joy, i);

				if (v & SDL_HAT_UP)
					keys |= KEY_UP;
				if (v & SDL_HAT_DOWN)
					keys |= KEY_DOWN;
				if (v & SDL_HAT_LEFT)
					keys |= KEY_LEFT;
				if (v & SDL_HAT_RIGHT)
					keys |= KEY_RIGHT;
			}
			
			Sint16 axis0 = SDL_JoystickGetAxis(joy, 0);
			Sint16 axis1 = SDL_JoystickGetAxis(joy, 1);
			
			if ( axis0 < -15000 )  keys |= KEY_LEFT;
			else if (axis0 > 15000 )  keys |= KEY_RIGHT;
			
			if (axis1 < -15000 )  keys |= KEY_UP;
			else if( axis1 > 15000 )  keys |= KEY_DOWN;	
				
			
			if ((!SDL_JoystickGetButton(joy, 0) && joy_bottons_last[nr][0]) ||      /* A */
					(!SDL_JoystickGetButton(joy, 3) && joy_bottons_last[nr][1]) ||  /* 2 */
					(!SDL_JoystickGetButton(joy, 9) && joy_bottons_last[nr][2]) ||  /* CA */
					(!SDL_JoystickGetButton(joy, 10) && joy_bottons_last[nr][3]))   /* CB */
				keys |= KEY_SELECT;
			if ((!SDL_JoystickGetButton(joy, 2) && joy_bottons_last[nr][4]) ||      /* 1 */
					(!SDL_JoystickGetButton(joy, 11) && joy_bottons_last[nr][5]) || /* CX */
					(!SDL_JoystickGetButton(joy, 12) && joy_bottons_last[nr][6])||   /* CY */
					(!SDL_JoystickGetButton(joy, 1) && joy_bottons_last[nr][7]))   /* B */
				keys |= KEY_ESCAPE;
			if (SDL_JoystickGetButton(joy, 5) != 0 ||      /* + */
					SDL_JoystickGetButton(joy, 18) != 0)   /* C+ */
				keys |= KEY_PAGEUP;
			if (SDL_JoystickGetButton(joy, 4) != 0 ||      /* - */
					SDL_JoystickGetButton(joy, 17) != 0)   /* C- */
				keys |= KEY_PAGEDOWN;
		
		joy_bottons_last[nr][0]=SDL_JoystickGetButton(joy, 0) ;   /* A */
		joy_bottons_last[nr][1]	=SDL_JoystickGetButton(joy, 3) ;  /* 2 */
		joy_bottons_last[nr][2]	=SDL_JoystickGetButton(joy, 9) ;  /* CA */
		joy_bottons_last[nr][3]	=SDL_JoystickGetButton(joy, 10) ; /* CB */
		joy_bottons_last[nr][4]	=SDL_JoystickGetButton(joy, 2) ;  /* 1 */
		joy_bottons_last[nr][5]	=SDL_JoystickGetButton(joy, 11) ; /* CX */
		joy_bottons_last[nr][6]	=SDL_JoystickGetButton(joy, 12) ; /* CY */
		joy_bottons_last[nr][7]	=SDL_JoystickGetButton(joy, 1) ; /* B */
		}
		
		joy_keys_changed = keys != joy_keys_last;
		joy_keys_last = keys;
		if (!joy_keys_changed)
			keys = 0;

		if (SDL_PollEvent(&ev))
		{
			switch(ev.type)
			{
			case SDL_KEYDOWN:
				switch (ev.key.keysym.sym)
				{
				case SDLK_UP:
					keys |= KEY_UP;
					break;
				case SDLK_DOWN:
					keys |= KEY_DOWN;
					break;
				case SDLK_LEFT:
					keys |= KEY_LEFT;
					break;
				case SDLK_RIGHT:
					keys |= KEY_RIGHT;
					break;
				case SDLK_PAGEDOWN:
					keys |= KEY_PAGEDOWN;
					break;
				case SDLK_PAGEUP:
					keys |= KEY_PAGEUP;
					break;
				case SDLK_HOME:
				case SDLK_ESCAPE:
					keys |= KEY_ESCAPE;
					break;
				default:
					break;
				}
				break;
			case SDL_KEYUP:
				switch (ev.key.keysym.sym)
				{
				case SDLK_RETURN:
				case SDLK_SPACE:
					keys |= KEY_SELECT;
					break;
				default:
					break;
				}
				break;
			case SDL_QUIT:
				exit(0);
				break;
			#ifndef GEKKO
			case SDL_MOUSEBUTTONDOWN:
				if (ev.button.button==SDL_BUTTON_LEFT) keys |= KEY_SELECT;
			break;
			#endif
			default:
					break;
			}
		}

		if (keys != 0)
			break;
		SDL_Delay(20);
	}
	return keys;
}

extern char curdir[MAX_PATH_LENGTH];

static int menu_select_internal(SDL_Surface *screen,
		menu_t *p_menu, int *p_submenus, int sel,
		void (*select_next_cb)(menu_t *p, void *data),
		void *select_next_cb_data, int font_type, int draw_scr)
{
	int ret = -1;
	int i;

	for (i = 0; i < p_menu->n_submenus; i++)
		p_menu->p_submenus[i].sel = p_submenus[i];

	while(1)
	{
		SDL_Rect r = {p_menu->x1, p_menu->y1,
				p_menu->x2 - p_menu->x1, p_menu->y2 - p_menu->y1};
		SDL_Rect r_int = {p_menu->x1+2/RATIO, p_menu->y1+2/RATIO,
				p_menu->x2 - p_menu->x1-4/RATIO, p_menu->y2 - p_menu->y1-4/RATIO};	
	
		uint32_t keys;
		int sel_last = p_menu->cur_sel;

		SDL_FillRect(screen, &r, SDL_MapRGB(screen->format, 255, 255, 0));
		SDL_FillRect(screen, &r_int, SDL_MapRGB(screen->format, 0, 0, 0));
		
		if (strcmp(p_menu->title, "Select block")&&strcmp(p_menu->title, "Select program")&&strcmp(p_menu->title, "Select snapshot")
		&&strncmp(p_menu->title, "Selected file:",14)&&strcmp(p_menu->title, "Select file"))
		{
			SDL_Rect dst_rect = {410/RATIO, 70/RATIO, 0, 0};
			if (RATIO == 1) SDL_BlitSurface(image_stripes, NULL, screen, &dst_rect);
			else SDL_BlitSurface(image_stripes_small, NULL, screen, &dst_rect);
		}
		
		menu_draw(screen, p_menu, 0, font_type, draw_scr);
		SDL_Flip(screen);

		keys = menu_wait_key_press();
		
		quit_thread = 1;

		if (keys & KEY_UP)
			{select_next(p_menu, 0, -1, 1);play_click(0);}
		else if (keys & KEY_DOWN)
			{select_next(p_menu, 0, 1, 1);play_click(0);}
		else if (keys & KEY_PAGEUP)
			{select_next(p_menu, 0, -19, 0);play_click(0);}
		else if (keys & KEY_PAGEDOWN)
			{select_next(p_menu, 0, 19, 0);play_click(0);}
		else if (keys & KEY_LEFT)
			{
			if (draw_scr) 
				ordenador.show_preview = 1;
			else
				{select_next(p_menu, -1, 0 ,1);play_click(0);}
			}
		else if (keys & KEY_RIGHT)
			{
			if (draw_scr) 
				ordenador.show_preview = 0;
			else
				{select_next(p_menu, 1, 0 ,1);play_click(0);}
			}
		else if (keys & KEY_ESCAPE)
			{play_click(2);break;}
		else if (keys & KEY_SELECT)
		{
			ret = p_menu->cur_sel;
			int i;

			for (i=0; i<p_menu->n_submenus; i++)
				p_submenus[i] = p_menu->p_submenus[i].sel;
			play_click(1);	
			break;
		}
		/* Invoke the callback when an entry is selected */
		if (sel_last != p_menu->cur_sel &&
				select_next_cb != NULL)
			select_next_cb(p_menu, select_next_cb_data);
	}

	//SDL_FillRect(screen, 0, SDL_MapRGB(screen->format, 0, 0, 0));
	return ret;
}

int menu_select_sized(const char *title, const char **msgs, int *submenus, int sel,
		int x, int y, int x2, int y2,
		void (*select_next_cb)(menu_t *p, void *data),
		void *select_next_cb_data, int font_type, int draw_scr)

{
	menu_t menu;
	int out;

	if (FULL_DISPLAY_X == 640)
	{
	if (font_type == FONT_ALT) menu_init_internal(&menu, title, menu_font_alt_large, msgs, x, y, x2, y2);
	else menu_init_internal(&menu, title, menu_font_large, msgs, x, y, x2, y2);
	}
	else
	{
	if (font_type == FONT_ALT) menu_init_internal(&menu, title, menu_font_alt_small, msgs, x, y, x2, y2);
	else menu_init_internal(&menu, title, menu_font_small, msgs, x, y, x2, y2);
	}

	if (sel >= 0)
		select_one(&menu, sel);
	out = menu_select_internal(real_screen, &menu, submenus, sel,
			select_next_cb, select_next_cb_data, font_type, draw_scr);

	menu_fini(&menu);

	return out;
}

int menu_select_title(const char *title, const char **msgs, int *submenus)
{
	SDL_FillRect(real_screen, 0, SDL_MapRGB(real_screen->format, 0, 0, 0));
	return menu_select_sized(title, msgs, submenus, 0,
			0, 48/RATIO, FULL_DISPLAY_X, FULL_DISPLAY_Y-48/RATIO,
			NULL, NULL, FONT_NORM, 0);
}

int menu_select(const char **msgs, int *submenus)
{
	return menu_select_title("", msgs, submenus);
}

static const char *menu_select_file_internal_zip(char *path,
		int x, int y, int x2, int y2, const char *selected_file, int draw_scr)
{
	const char **file_list = get_file_list_zip(path);
	char *sel;
	const char *ptr_selected_file;
	int opt;
	int i;
	int err;
	char buf[80];
	
	if (file_list == NULL) {free(path); return NULL;}

	if (selected_file) 
	{
		ptr_selected_file= strrchr(selected_file,'/');
		if (ptr_selected_file) ptr_selected_file++;
		else ptr_selected_file = selected_file;
		snprintf(buf,80,"Selected file:%s",ptr_selected_file);
		opt = menu_select_sized(buf, file_list, NULL, 0, x, y, x2, y2, NULL, NULL, FONT_ALT, draw_scr);
	}
	else opt = menu_select_sized("Select file", file_list, NULL, 0, x, y, x2, y2, NULL, NULL ,FONT_ALT, draw_scr);
	
	sel = NULL;
			
	if (opt >= 0) sel = strdup(file_list[opt]);

	/* Cleanup everything - file_list is NULL-terminated */
        for ( i = 0; file_list[i]; i++ )
        	free((void*)file_list[i]);
        free(file_list);
		
	if (opt < 0) {free(path); return NULL;}	

	if (!sel) {free(path); return NULL;}
	
	if (opt==0||opt==1) {free(path); return sel;} //"None" or "[..]"
		
  	unzFile uf = unzOpen(path);
	
	if (unzLocateFile (uf, sel, 1)!= UNZ_OK) {printf ("File not found in zip/n"); unzClose(path); free(path);free (sel);return NULL;}
	
	free(path); //It does need anymore
	
	char* filename_withoutpath;
	char* write_filename;
    char* p;
	
	p = filename_withoutpath = sel;
    while ((*p) != '\0')
    {
        if (((*p)=='/') || ((*p)=='\\'))
            filename_withoutpath = p+1;
        p++;
    }
	
	if ((*filename_withoutpath)=='\0') filename_withoutpath = sel;
	
	write_filename = (char*)malloc(strlen(path_tmp) + strlen(filename_withoutpath) + 4);
	
	snprintf(write_filename, strlen(path_tmp) + strlen(filename_withoutpath) + 4, "%s/%s", path_tmp, filename_withoutpath);
	
	err = unzOpenCurrentFile(uf);
        if (err!=UNZ_OK)
        {
            printf("error %d with zipfile in unzOpenCurrentFile\n",err);
        }

	void* buf2;
    unsigned int size_buf = 8192;
	FILE *fout=NULL;
	
    buf2 = (void*)malloc(size_buf);
    if (buf2==NULL)
    {
        printf("Error allocating memory\n");
		free(sel);
		free(write_filename);
        return NULL;
    }

	//unlink(write_filename);
	
	fout=fopen(write_filename,"wb"); 

	if (fout!=NULL)
        {
            printf("Extracting: %s in %s\n",sel, write_filename);

            do
            {
                err = unzReadCurrentFile(uf,buf2,size_buf);
                if (err<0)
                {
                    printf("Error %d with zipfile in unzReadCurrentFile\n",err);
                    break;
                }
                if (err>0)
                    if (fwrite(buf2,err,1,fout)!=1)
                    {
                        printf("Error in writing extracted file\n");
                        err=UNZ_ERRNO;
                        break;
                    }
            }
            while (err>0);
            
			if (fout) fclose(fout);
        }
	
	unzCloseCurrentFile(uf);
	unzClose (uf);
	free(buf2);
	free(sel);
	
	return write_filename;
}

const char **get_file_list_browser(unsigned int tape_pos, unsigned int *block_pos)
{
	unsigned int loop;
	char **browser_list_menu;
	
	*block_pos=0;
	
	browser_list_menu = (char**)malloc((MAX_BROWSER_ITEM+1) * sizeof(char*));
	browser_list_menu[0] = NULL;

	for(loop=0;browser_list[loop]!=NULL;loop++)
	{
	browser_list_menu[loop]=malloc(24+36+9);
	if (browser_list[loop]->position==tape_pos)
		{
		sprintf(browser_list_menu[loop],"]%04d %s   %s",loop,browser_list[loop]->block_type, browser_list[loop]->info);
		*block_pos=loop;
		}
	else
		sprintf(browser_list_menu[loop],"%04d %s   %s",loop,browser_list[loop]->block_type, browser_list[loop]->info);
	}
	browser_list_menu[loop]=NULL;
	return (const char **) browser_list_menu;
}

const char **get_file_list_browser_rzx()
{
	unsigned int loop;
	char **browser_list_menu;
	
	
	browser_list_menu = (char**)malloc((MAX_RZX_BROWSER_ITEM+1) * sizeof(char*));
	browser_list_menu[0] = NULL;

	for(loop=0;rzx_browser_list[loop].position!=0;loop++)
	{
	browser_list_menu[loop]=malloc(48);
	if (rzx_browser_list[loop].position==last_snapshot_position)
		{
		sprintf(browser_list_menu[loop],"]%04d Frames: %u",loop, rzx_browser_list[loop].frames_count);
		}
	else
		sprintf(browser_list_menu[loop],"%04d Frames: %u",loop, rzx_browser_list[loop].frames_count);
	}
	browser_list_menu[loop]=NULL;
	return (const char **) browser_list_menu;
}

const char **get_file_list_select_block()
{
	unsigned int loop;
	char **block_list_menu;
	
	block_list_menu = (char**)malloc((MAX_SELECT_ITEM+1) * sizeof(char*));
	block_list_menu[0] = NULL;

	for(loop=0;block_select_list[loop]!=NULL;loop++)
	{
		block_list_menu[loop]=malloc(32);
		sprintf(block_list_menu[loop],"%02d %s",loop, block_select_list[loop]->info);
	}
	block_list_menu[loop]=NULL;
	return (const char **) block_list_menu;
}

static const char *menu_select_file_internal(char *dir_path,
		int x, int y, int x2, int y2, const char *selected_file, int draw_scr, unsigned int tape_pos)
{
	const char **file_list; 
	char *sel;
	char *out;
	char *out_zip;
	const char *ptr_selected_file;
	char *updir;
	int opt;
	int i;
	char buf[80];
	unsigned int block_pos;
	
	block_pos = 0;
	
	if (!strcmp(dir_path,"browser")) file_list =  get_file_list_browser(tape_pos, &block_pos);  
	else if (!strcmp(dir_path,"browser_rzx")) file_list =  get_file_list_browser_rzx();
	else if (!strcmp(dir_path,"select_block")) file_list = get_file_list_select_block();  
    else file_list = get_file_list(dir_path);
	
	if (file_list == NULL)
		return NULL;
		
	if (!strcmp(dir_path,"browser")) opt = menu_select_sized("Select block", file_list, NULL, block_pos, x, y, x2, y2, NULL, NULL ,FONT_ALT, 0);
	else if (!strcmp(dir_path,"browser_rzx")) opt = menu_select_sized("Select snapshot", file_list, NULL, 0, x, y, x2, y2, NULL, NULL ,FONT_ALT, 1);
	else if (!strcmp(dir_path,"select_block")) opt = menu_select_sized("Select program", file_list, NULL, 0, x, y, x2, y2, NULL, NULL ,FONT_ALT, 0);
	else if (selected_file) 
	{
		ptr_selected_file= strrchr(selected_file,'/');
		if (ptr_selected_file) ptr_selected_file++;
		else ptr_selected_file = selected_file;
		snprintf(buf,80,"Selected file:%s",ptr_selected_file);
		opt = menu_select_sized(buf, file_list, NULL, 0, x, y, x2, y2, NULL, NULL, FONT_ALT, draw_scr);
	}
	else opt = menu_select_sized("Select file", file_list, NULL, 0, x, y, x2, y2, NULL, NULL ,FONT_ALT, draw_scr);
	
	sel = NULL;	
	
	if (opt >= 0 ) sel = strdup(file_list[opt]);

	/* Cleanup everything - file_list is NULL-terminated */
        for ( i = 0; file_list[i]; i++ )
        	free((void*)file_list[i]);
        free(file_list);
	
	if (opt < 0)
		return NULL;

	if (!sel)
		return NULL;
		
	if (!strcmp(dir_path,"browser")||!strcmp(dir_path,"select_block")||!strcmp(dir_path,"browser_rzx")) return sel;	
		
	if (!strcmp(sel,"[..]")) //selected "[..]"
	{
		free((void*)sel);
		updir=strrchr(dir_path,'/');
		if (updir!=NULL)  // found "/"
		{
			*updir=0; //trunk dir_path at last /
			if (strrchr(dir_path,'/')==NULL) {*updir='/'; *(updir+1)=0;} //check if it was root
		}
		
		return menu_select_file(dir_path, selected_file, draw_scr);
	}		
        /* If this is a folder, enter it recursively */
        if (sel[0] == '[')
        {
        	int len = strlen(sel);

        	/* Remove trailing ] */
        	sel[len-1] = '\0';
			if ((strlen(dir_path) + len) < MAX_PATH_LENGTH) 
			{
			strcat(dir_path, "/");
			strcat(dir_path, sel+1);
			}
			else
			{
			free((void*)sel);
			return NULL;
			}
			
        	/* We don't need this anymore */
        	free((void*)sel);
        	
        	return menu_select_file(dir_path, selected_file, draw_scr);
        }
		
		

	out = (char*)malloc(strlen(dir_path) + strlen(sel) + 4);
	snprintf(out, strlen(dir_path) + strlen(sel) + 4,
			"%s/%s", dir_path, sel);

	free(sel);
	
	if ((ext_matches(out, ".zip")||ext_matches(out, ".ZIP"))&&(tmpismade))
	{out_zip = (char *) menu_select_file_internal_zip (out, x, y, x2, y2, selected_file, draw_scr);
	if (!out_zip) return NULL;
	if(!strcmp(out_zip,"[..]")) 
		{
		free(out_zip);
		return menu_select_file_internal (dir_path, x, y, x2, y2, selected_file, draw_scr, 0);
		}
	else return out_zip;
	}
    else return out;
}

const char *menu_select_file(char *dir_path,const char *selected_file, int draw_scr)
{
	if (dir_path == NULL)
		dir_path = "";
	return menu_select_file_internal(dir_path,
			0, 18/RATIO, FULL_DISPLAY_X, FULL_DISPLAY_Y - 18/RATIO, selected_file, draw_scr, 0);
}

const char *menu_select_browser(unsigned int tape_pos)
{
	return menu_select_file_internal("browser",
			0, 18/RATIO, FULL_DISPLAY_X, FULL_DISPLAY_Y - 18/RATIO, NULL, 0, tape_pos);
}

const char *menu_select_browser_rzx()
{
	return menu_select_file_internal("browser_rzx",
			0, 18/RATIO, FULL_DISPLAY_X, FULL_DISPLAY_Y - 18/RATIO, NULL, 1, 0);
}

const char *menu_select_tape_block()
{
	SDL_FillRect(screen, 0, SDL_MapRGB(screen->format, 0, 0, 0));
	return menu_select_file_internal("select_block",
			0, 18/RATIO, FULL_DISPLAY_X, FULL_DISPLAY_Y - 18/RATIO, NULL, 0, 0);
}

/*
static TTF_Font *read_font(const char *path, int font_type)
{
	TTF_Font *out;
	SDL_RWops *rw;
	Uint8 *data = (Uint8*)malloc(1 * 1024*1024);
	FILE *fp = fopen(path, "rb");

	if (!data) {
		fprintf(stderr, "Malloc failed\n");
		exit(1);
	}
	if (!fp) {
		fprintf(stderr, "Could not open font\n");
		exit(1);
	}
	fread(data, 1, 1 * 1024 * 1024, fp);
	rw = SDL_RWFromMem(data, 1 * 1024 * 1024);
	if (!rw) 
	{
		fprintf(stderr, "Could not create RW: %s\n", SDL_GetError());
		exit(1);
	}
	out = TTF_OpenFontRW(rw, 1, font_type);
	if (!out)
	{
		fprintf(stderr, "Unable to open font %s\n", path);
		exit(1);
	}
	fclose(fp);

	return out;
}
*/
void font_init()
{
	char *font_path,*font_path2;
	
	TTF_Init();
	
	font_path=myfile("fbzx/ZX_Spectrum.ttf");
	font_path2=myfile("fbzx/FreeMono.ttf");

	menu_font_large = TTF_OpenFont(font_path, 16);//Used for menu
	menu_font_alt_large = TTF_OpenFont(font_path2, 20); //Used for file selection
	menu_font_small = TTF_OpenFont(font_path, 8);
	menu_font_alt_small = TTF_OpenFont(font_path2, 10);
	
	free(font_path);
	free(font_path2);
}

void font_fini()
{
	TTF_CloseFont(menu_font_alt_large); 
	TTF_CloseFont(menu_font_large);
	TTF_CloseFont(menu_font_alt_small);
	TTF_CloseFont(menu_font_small);
	
	TTF_Quit();	
}

void menu_init(SDL_Surface *screen)
{
	FILE *fichero;
	int i;
	
	for(i=0; i<3; i++)
	{
		switch (i)
		{
#ifdef GEKKO
			case 0:
			fichero=myfopen("fbzx/menu_navigation_BE.raw","rb"); //Menu up, down, left, right
			break;
		
			case 1:
			fichero=myfopen("fbzx/select_BE.raw","rb"); //Menu select
			break;
			
			case 2:
			fichero=myfopen("fbzx/unselect_BE.raw","rb"); //Menu unselect
			break;
#else
			case 0:
			fichero=myfopen("fbzx/menu_navigation_LE.raw","rb"); //Menu up, down, left, right
			break;
		
			case 1:
			fichero=myfopen("fbzx/select_LE.raw","rb"); //Menu select
			break;
			
			case 2:
			fichero=myfopen("fbzx/unselect_LE.raw","rb"); //Menu unselect
			break;
#endif			
		}
		
		
		if(fichero==NULL) {
			printf("Can't open button click wav file: %d\n", i);
			exit(1);
			}
		
		fseek (fichero, 0, SEEK_END);
		len_click_buffer[i]=ftell (fichero);
		fseek (fichero, 0, SEEK_SET);
	
		click_buffer_pointer[i]= (int *) malloc(len_click_buffer[i]);
	
		if(click_buffer_pointer[i]==NULL) {
			printf("Can't allocate click wav buffer: %d\n",i);
			exit(1);
			}
		
		fread(click_buffer_pointer[i], 1, len_click_buffer[i], fichero); 	
	
		fclose(fichero);
	}
	
	char *image_path;
	
	image_path=myfile("fbzx/stripes.png");
	tmp_surface=IMG_Load(image_path);
	free(image_path);
	if (tmp_surface == NULL) {printf("Impossible to load stripes image\n"); exit(1);}
	image_stripes=SDL_DisplayFormat(tmp_surface);
	SDL_FreeSurface (tmp_surface);	
	
	image_path=myfile("fbzx/stripes_small.png");
	tmp_surface=IMG_Load(image_path);
	free(image_path);
	if (tmp_surface == NULL) {printf("Impossible to load stripes small image\n"); exit(1);}
	image_stripes_small=SDL_DisplayFormat(tmp_surface);
	SDL_FreeSurface (tmp_surface);

	real_screen = screen;
	is_inited = 1;
}

void menu_deinit()
{
	free(click_buffer_pointer[0]);
	free(click_buffer_pointer[1]);
	free(click_buffer_pointer[2]);
	SDL_FreeSurface (image_stripes);
	SDL_FreeSurface (image_stripes_small);
	real_screen = 0;
	is_inited = 0;
}

int menu_is_inited(void)
{
	return is_inited;
}

//Sound must be reseted before calling this function (except for ASND)
void play_click(int sound)
{
	if (!ordenador.gui_volume) return;
#ifdef GEKKO	
	if (sound_type == SOUND_ASND)
	{
		ASND_SetVoice(2,VOICE_STEREO_16BIT_BE,ordenador.freq,0, click_buffer_pointer[sound],len_click_buffer[sound],
			ordenador.gui_volume*40, ordenador.gui_volume*40, NULL);
		return;
	}
#endif	
	int inc;
	int len_click_buffer_norm = len_click_buffer[sound]/ordenador.increment;
	
	for(inc=0; inc< len_click_buffer_norm-ordenador.buffer_len; inc+=ordenador.buffer_len)
	{
		memcpy(ordenador.current_buffer, click_buffer_pointer[sound]+inc, ordenador.buffer_len*ordenador.increment);
		sound_play();
	}
	
	memcpy(ordenador.current_buffer, click_buffer_pointer[sound] + inc, (len_click_buffer_norm - inc)*ordenador.increment);
	//memset(ordenador.current_buffer + len_click_buffer_norm - inc,0, (inc-len_click_buffer_norm + ordenador.buffer_len)*ordenador.increment);
	sound_play();
}


int ask_value_sdl(int *final_value,int y_coord,int max_value) {

	unsigned char nombre2[50];
	unsigned char *videomem;
	int ancho,value,tmp,retorno;
	struct virtkey *virtualkey;
	unsigned int sdl_key;

	videomem=screen->pixels;
	ancho=screen->w;

	
	value=0;
	do {
		retorno=-1;
		sprintf (nombre2, " %d\177 ", value);
		print_string (videomem, nombre2, -1, y_coord, 15, 0, ancho);
		
		VirtualKeyboard.sel_x = 64;
		VirtualKeyboard.sel_y = 155;
		
		virtualkey = get_key();
		if (virtualkey == NULL) return(0);
		if (virtualkey->sdl_code==1) {play_click(2); break; }//done, retorno -1
		
		play_click(1);
		sdl_key = virtualkey->sdl_code;
		
		switch (sdl_key) {
		case SDLK_BACKSPACE:
			value/=10;
		break;
		case SDLK_ESCAPE:
			retorno=0;
		break;
		case SDLK_RETURN:
			retorno=1;
		break;
		case SDLK_0:
			tmp=value * 10;
			if (tmp <= max_value) {
				value=tmp;
			}
		break;
		case SDLK_1:
			tmp=1+value * 10;
			if (tmp <= max_value) {
				value=tmp;
			}
		break;
		case SDLK_2:
			tmp=2+value * 10;
			if (tmp <= max_value) {
				value=tmp;
			}
		break;
		case SDLK_3:
			tmp=3+value * 10;
			if (tmp <= max_value) {
				value=tmp;
			}
		break;
		case SDLK_4:
			tmp=4+value * 10;
			if (tmp <= max_value) {
				value=tmp;
			}
		break;
		case SDLK_5:
			tmp=5+value * 10;
			if (tmp <= max_value) {
				value=tmp;
			}
		break;
		case SDLK_6:
			tmp=6+value * 10;
			if (tmp <= max_value) {
				value=tmp;
			}
		break;
		case SDLK_7:
			tmp=7+value * 10;
			if (tmp <= max_value) {
				value=tmp;
			}
		break;
		case SDLK_8:
			tmp=8+value * 10;
			if (tmp <= max_value) {
				value=tmp;
			}
		break;
		case SDLK_9:
			tmp=9+value * 10;
			if (tmp <= max_value) {
				value=tmp;
			}
		break;
		}
	} while (retorno==-1);

	*final_value=value;

	return (retorno);
}

int ask_filename_sdl(char *nombre_final,int y_coord,char *extension, char *path, char *name) {

	int longitud,retorno;
	unsigned char nombre[37],nombre2[38];
	char *ptr;

	unsigned char *videomem;
	int ancho;
	
	struct virtkey *virtualkey;
	unsigned int sdl_key;

	videomem=screen->pixels;
	ancho=screen->w;

	retorno=0;
	
	if (!name||(strlen(name)>36)) 
		{
		nombre[0]=127;
		nombre[1]=0;
		}
	else
		{
		strcpy(nombre,name);
		ptr = strrchr (nombre, '.');
		if (ptr) //remove the extension 
			{
			*ptr = 127;
			*(ptr+1) = 0;
			}
		else
		nombre[strlen(nombre)-1]=127;
		nombre[strlen(nombre)]=0;
		}
	
	longitud=strlen(nombre)-1;
	

	do {
		sprintf (nombre2, " %s.%s ", nombre,extension);
		print_string (videomem, nombre2, -1, y_coord, 15, 0, ancho);
		
		VirtualKeyboard.sel_x = 64;
		VirtualKeyboard.sel_y = 155;
		
		virtualkey = get_key();
		if (virtualkey == NULL) return(2);
		
		play_click(1);
		
		sdl_key = virtualkey->sdl_code;
		
		switch (sdl_key) {
		case SDLK_BACKSPACE:
			if (longitud > 0) {
				nombre[longitud]=0;
				longitud--;
				nombre[longitud]=127;
			}
		break;
		case SDLK_ESCAPE:
			retorno=2;
		break;
		case SDLK_RETURN:
			retorno=1;
		break;
		case SDLK_a:
			if (longitud < 30) {
				nombre[longitud++]='a';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		case SDLK_b:
			if (longitud < 30) {
				nombre[longitud++]='b';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		case SDLK_c:
			if (longitud < 30) {
				nombre[longitud++]='c';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		case SDLK_d:
			if (longitud < 30) {
				nombre[longitud++]='d';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		case SDLK_e:
			if (longitud < 30) {
				nombre[longitud++]='e';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		case SDLK_f:
			if (longitud < 30) {
				nombre[longitud++]='f';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		case SDLK_g:
			if (longitud < 30) {
				nombre[longitud++]='g';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		case SDLK_h:
			if (longitud < 30) {
				nombre[longitud++]='h';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		case SDLK_i:
			if (longitud < 30) {
				nombre[longitud++]='i';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		case SDLK_j:
			if (longitud < 30) {
				nombre[longitud++]='j';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		case SDLK_k:
			if (longitud < 30) {
				nombre[longitud++]='k';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		case SDLK_l:
			if (longitud < 30) {
				nombre[longitud++]='l';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		case SDLK_m:
			if (longitud < 30) {
				nombre[longitud++]='m';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		case SDLK_n:
			if (longitud < 30) {
				nombre[longitud++]='n';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		case SDLK_o:
			if (longitud < 30) {
				nombre[longitud++]='o';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		case SDLK_p:
			if (longitud < 30) {
				nombre[longitud++]='p';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		case SDLK_q:
			if (longitud < 30) {
				nombre[longitud++]='q';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		case SDLK_r:
			if (longitud < 30) {
				nombre[longitud++]='r';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		case SDLK_s:
			if (longitud < 30) {
				nombre[longitud++]='s';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		case SDLK_t:
			if (longitud < 30) {
				nombre[longitud++]='t';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		case SDLK_u:
			if (longitud < 30) {
				nombre[longitud++]='u';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		case SDLK_v:
			if (longitud < 30) {
				nombre[longitud++]='v';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		case SDLK_w:
			if (longitud < 30) {
				nombre[longitud++]='w';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		case SDLK_x:
			if (longitud < 30) {
				nombre[longitud++]='x';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		case SDLK_y:
			if (longitud < 30) {
				nombre[longitud++]='y';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		case SDLK_z:
			if (longitud < 30) {
				nombre[longitud++]='z';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		case SDLK_0:
			if (longitud < 30) {
				nombre[longitud++]='0';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		case SDLK_1:
			if (longitud < 30) {
				nombre[longitud++]='1';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		case SDLK_2:
			if (longitud < 30) {
				nombre[longitud++]='2';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		case SDLK_3:
			if (longitud < 30) {
				nombre[longitud++]='3';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		case SDLK_4:
			if (longitud < 30) {
				nombre[longitud++]='4';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		case SDLK_5:
			if (longitud < 30) {
				nombre[longitud++]='5';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		case SDLK_6:
			if (longitud < 30) {
				nombre[longitud++]='6';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		case SDLK_7:
			if (longitud < 30) {
				nombre[longitud++]='7';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		case SDLK_8:
			if (longitud < 30) {
				nombre[longitud++]='8';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		case SDLK_9:
			if (longitud < 30) {
				nombre[longitud++]='9';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		case SDLK_MINUS:
			if (longitud < 30) {
				nombre[longitud++]='-';
				nombre[longitud]=127;
				nombre[longitud + 1]=0;
			}
		break;
		}
	} while (!retorno);

	nombre[longitud]=0; // erase cursor

	longitud=strlen(path);
	if((path[longitud-1]!='/')&&(longitud>1))
		sprintf(nombre_final,"%s/%s.%s",path,nombre,extension); // name
	else
		sprintf(nombre_final,"%s%s.%s",path,nombre,extension);

	return (retorno);
}
