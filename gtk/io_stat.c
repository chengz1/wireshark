/* io_stat.c
 * io_stat   2002 Ronnie Sahlberg
 *
 * $Id: io_stat.c,v 1.23 2003/09/02 08:27:33 sahlberg Exp $
 *
 * Ethereal - Network traffic analyzer
 * By Gerald Combs <gerald@ethereal.com>
 * Copyright 1998 Gerald Combs
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */


#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>

#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif

#include <string.h>

#include <ctype.h>

#include <gtk/gtk.h>
#include "gtkglobals.h"
#include "menu.h"
#include "epan/epan_dissect.h"
#include "epan/packet_info.h"
#include "epan/filesystem.h"
#include "../tap.h"
#include "../register.h"
#include "simple_dialog.h"
#include "../globals.h"
#include "../color.h"
#include "compat_macros.h"

/* filter prefs dialog */
#include "filter_prefs.h"
/* filter prefs dialog */

void protect_thread_critical_region(void);
void unprotect_thread_critical_region(void);

#define MAX_GRAPHS 5

#define MAX_YSCALE 22
#define AUTO_MAX_YSCALE 0
static guint32 yscale_max[MAX_YSCALE] = {AUTO_MAX_YSCALE, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000, 200000, 500000, 1000000, 2000000, 5000000, 10000000, 20000000, 50000000};

#define MAX_PIXELS_PER_TICK 4
#define DEFAULT_PIXELS_PER_TICK 2
static guint32 pixels_per_tick[MAX_PIXELS_PER_TICK] = {1, 2, 5, 10};

#define MAX_COUNT_TYPES 3
#define COUNT_TYPE_ADVANCED 2
static char *max_count_types[MAX_COUNT_TYPES] = {"frames/tick", "bytes/tick", "advanced..."};

/* unit is in ms */
#define MAX_TICK_VALUES 4
#define DEFAULT_TICK_VALUE 2
static guint max_tick_values[MAX_TICK_VALUES] = { 10, 100, 1000, 10000 };

#define MAX_CALC_TYPES 5
#define CALC_TYPE_SUM	0
#define CALC_TYPE_COUNT	1
#define CALC_TYPE_MAX	2
#define CALC_TYPE_MIN	3
#define CALC_TYPE_AVG	4
static char *max_calc_types[MAX_CALC_TYPES] = {"SUM(*)", "COUNT(*)", "MAX(*)", "MIN(*)", "AVG(*)"};


typedef struct _io_stat_item_t {
	struct _io_stat_item_t *next;
	struct _io_stat_item_t *prev;
	guint32 time;	/* this is number of ms since start of capture */
	guint32 frames; /* always calculated, will hold number of packets/frames*/
	guint32 bytes;
	gint32 int_max;
	gint32 int_min;
	gint32 int_tot;
	nstime_t time_max;
	nstime_t time_min;
	nstime_t time_tot;
} io_stat_item_t;

typedef struct _io_stat_calc_type_t {
	struct _io_stat_graph_t *gio;
	int calc_type;
} io_stat_calc_type_t;

typedef struct _io_stat_graph_t {
	struct _io_stat_t *io;
	struct _io_stat_item_t *counts;
	int display;
	GtkWidget *display_button;
	GtkWidget *color_button;
	GtkWidget *filter_button;
	GtkWidget *advanced_buttons;
	int calc_type;
	io_stat_calc_type_t calc_types[MAX_CALC_TYPES];
	int hf_index;
	GtkWidget *calc_field;
	GdkColor color;
	GdkGC *gc;
	construct_args_t *args;
	GtkWidget *filter_bt;
} io_stat_graph_t;

typedef struct _io_stat_yscale_t {
	struct _io_stat_t *io;
	int yscale;
} io_stat_yscale_t;

typedef struct _io_stat_pixels_per_tick_t {
	struct _io_stat_t *io;
	int pixels_per_tick;
} io_stat_pixels_per_tick_t;

typedef struct _io_stat_count_type_t {
	struct _io_stat_t *io;
	int count_type;
} io_stat_count_type_t;

typedef struct _io_stat_tick_interval_t {
	struct _io_stat_t *io;
	int interval;
} io_stat_tick_interval_t;


typedef struct _io_stat_t {
	int needs_redraw;
	gint32 interval;    /* measurement interval in ms */
	guint32 last_interval; 
	guint32 max_interval;

	struct _io_stat_graph_t graphs[MAX_GRAPHS];
	struct _io_stat_yscale_t yscale[MAX_YSCALE];
	struct _io_stat_pixels_per_tick_t pixelspertick[MAX_PIXELS_PER_TICK];
	struct _io_stat_count_type_t counttype[MAX_COUNT_TYPES];
	struct _io_stat_tick_interval_t tick_val[MAX_TICK_VALUES];
	GtkWidget *window;
	GtkWidget *draw_area;
	GdkPixmap *pixmap;
	GtkAdjustment *scrollbar_adjustment;
	GtkWidget *scrollbar;
	int pixmap_width;
	int pixmap_height;
	int pixels_per_tick;
	int max_y_units;
	int count_type;
} io_stat_t;	

#if GTK_MAJOR_VERSION < 2
GtkRcStyle *rc_style;
GdkColormap *colormap;
#endif



static void init_io_stat_window(io_stat_t *io);

/* Hash table to keep track of widget to io_stat_t mappings.
   Did it this way since i could not find a clean way to associate private 
   data with a widget using the API */
static GHashTable *io_stat_widget_table=NULL;
static guint
io_stat_widget_hash(gconstpointer k)
{
	guint32 frame = (guint32)k;

	return frame;
}
static gint
io_stat_widget_equal(gconstpointer k1, gconstpointer k2)
{
	guint32 frame1 = (guint32)k1;
	guint32 frame2 = (guint32)k2;

	return frame1==frame2;
}



static void
gtk_iostat_reset(void *g)
{
	io_stat_graph_t *gio=g;
	io_stat_item_t *it;
	char title[256];

	gio->io->needs_redraw=1;

	while(gio->counts->next){
		it=gio->counts->next;
		gio->counts->next=it->next;
		g_free(it);
	}

	gio->counts->prev=gio->counts;
	gio->counts->next=NULL;
	gio->counts->time=0;
	gio->counts->frames=0;
	gio->counts->bytes=0;
	gio->counts->int_max=0;
	gio->counts->int_min=0;
	gio->counts->int_tot=0;

	gio->io->last_interval=0xffffffff;
	gio->io->max_interval=0;

	snprintf(title, 255, "IO-Stat: %s", get_basename(cfile.filename));
	gtk_window_set_title(GTK_WINDOW(gio->io->window), title);
}


static int
gtk_iostat_packet(void *g, packet_info *pinfo, epan_dissect_t *edt, void *dummy _U_)
{
	io_stat_graph_t *git=g;
	io_stat_item_t *it;
	nstime_t time_delta;
	guint32 adjusted_time;

	/* we sometimes get called when git is disabled.
	   this is a bug since the tap listener should be removed first */
	if(!git->display){
		return 0;
	}

	git->io->needs_redraw=1;

	/* the prev item before the main one is always the last interval we saw packets for */
	it=git->counts->prev;

	/* XXX for the time being, just ignore all frames that are in the past.
	   should be fixed in the future but hopefully it is uncommon */
	time_delta.secs=pinfo->fd->rel_secs;
	time_delta.nsecs=pinfo->fd->rel_usecs*1000;
	if(time_delta.nsecs<0){
		time_delta.secs--;
		time_delta.nsecs+=1000000000;
	}
	if(time_delta.secs<0){
		return TRUE;
	}
	/* time since start of capture in ms */
	adjusted_time=time_delta.secs*1000+time_delta.nsecs/1000000;

	/* timestamps jumped backwards, just ignore the packet.
	   if this is common someone can fix this later */
	if(adjusted_time<it->time){
		return TRUE;
	}


	/* we have moved into a new interval, we need to create a new struct */
	if(adjusted_time>=(it->time+git->io->interval)){
		it->next=g_malloc(sizeof(io_stat_item_t));
		it->next->prev=it;
		it->next->next=NULL;
		it=it->next;
		git->counts->prev=it;

		/* set time of new counter struct of adjusted_time rounded
		   to multiple of intervals */
		it->time=(adjusted_time/git->io->interval)*git->io->interval;
		it->frames=0;
		it->bytes=0;
		it->int_max=0;		
		it->int_min=0;		
		it->int_tot=0;
		it->time_max.secs=0;
		it->time_max.nsecs=0;
		it->time_min.secs=0;
		it->time_min.nsecs=0;
		it->time_tot.secs=0;
		it->time_tot.nsecs=0;
		if(it->time>git->io->max_interval){
			git->io->max_interval=it->time;
		}
	}

	/* it will now give us the current structure to use to store the data in */

	if(git->io->count_type==COUNT_TYPE_ADVANCED){
		GPtrArray *gp;
		guint i;

		gp=proto_get_finfo_ptr_array(edt->tree, git->hf_index);
		if(!gp){
			return FALSE;
		}

		if(it->frames==0){
			int new_int;
			nstime_t *new_time;

			switch(proto_registrar_get_ftype(git->hf_index)){
			case FT_UINT8:
			case FT_UINT16:
			case FT_UINT24:
			case FT_UINT32:
			case FT_INT8:
			case FT_INT16:
			case FT_INT24:
			case FT_INT32:
				new_int=fvalue_get_integer(((field_info *)gp->pdata[0])->value);
				it->int_max=new_int;
				it->int_min=new_int;
				it->int_tot=0;
				break;
			case FT_RELATIVE_TIME:
				new_time=fvalue_get(((field_info *)gp->pdata[0])->value);
				it->time_max.secs=new_time->secs;
				it->time_max.nsecs=new_time->nsecs;
				it->time_min.secs=new_time->secs;
				it->time_min.nsecs=new_time->nsecs;
				it->time_tot.secs=0;
				it->time_tot.nsecs=0;
			}
		}
		for(i=0;i<gp->len;i++){
			int new_int;
			nstime_t *new_time;

			switch(proto_registrar_get_ftype(git->hf_index)){
			case FT_UINT8:
			case FT_UINT16:
			case FT_UINT24:
			case FT_UINT32:
			case FT_INT8:
			case FT_INT16:
			case FT_INT24:
			case FT_INT32:
				new_int=fvalue_get_integer(((field_info *)gp->pdata[i])->value);

				if(new_int>it->int_max){
					it->int_max=new_int;
				}
				if(new_int<it->int_min){
					it->int_min=new_int;
				}
				it->int_tot+=new_int;
				break;
			case FT_RELATIVE_TIME:
				new_time=fvalue_get(((field_info *)gp->pdata[0])->value);

				if( (new_time->secs>it->time_max.secs)
				||( (new_time->secs==it->time_max.secs)
				  &&(new_time->nsecs>it->time_max.nsecs))){
					it->time_max.secs=new_time->secs;
					it->time_max.nsecs=new_time->nsecs;
				}
				if( (new_time->secs<it->time_min.secs)
				||( (new_time->secs==it->time_min.secs)
				  &&(new_time->nsecs<it->time_min.nsecs))){
					it->time_min.secs=new_time->secs;
					it->time_min.nsecs=new_time->nsecs;
				}
				it->time_tot.secs+=new_time->secs;
				it->time_tot.nsecs+=new_time->nsecs;
				if(it->time_tot.nsecs>=1000000000){
					it->time_tot.nsecs-=1000000000;
					it->time_tot.secs++;
				}

			}
		}
	}

	it->frames++;
	it->bytes+=pinfo->fd->pkt_len;
	
	return TRUE;
}


static guint32
get_it_value(io_stat_t *io, io_stat_item_t *it, int adv_type, int calc_type)
{
	guint32 value=0;

	switch(io->count_type){
	case 0:
		value=it->frames;
		break;
	case 1:
		value=it->bytes;
		break;
	case COUNT_TYPE_ADVANCED:
		switch(adv_type){
		case FT_UINT8:
		case FT_UINT16:
		case FT_UINT24:
		case FT_UINT32:
		case FT_INT8:
		case FT_INT16:
		case FT_INT24:
		case FT_INT32:
			switch(calc_type){
			case CALC_TYPE_SUM:
				value=it->int_tot;
				break;
			case CALC_TYPE_COUNT:
				value=it->frames;
				break;
			case CALC_TYPE_MAX:
				value=it->int_max;
				break;
			case CALC_TYPE_MIN:
				value=it->int_min;
				break;
			case CALC_TYPE_AVG:
				if(it->frames){
					value=it->int_tot/it->frames;
				} else {
					value=0;
				}
				break;
			default:
				break;
			}
			break;
		case FT_RELATIVE_TIME:
			switch(calc_type){
			case CALC_TYPE_COUNT:
				value=it->frames;
				break;
			case CALC_TYPE_MAX:
				value=it->time_max.secs*1000000+it->time_max.nsecs/1000;
				break;
			case CALC_TYPE_MIN:
				value=it->time_min.secs*1000000+it->time_min.nsecs/1000;
				break;
			case CALC_TYPE_AVG:
				if(it->frames){
#ifdef G_HAVE_UINT64
					guint64 tmp;
#else
					guint32 tmp;
#endif
					tmp=it->time_tot.secs;
					tmp=tmp*1000000+it->time_tot.nsecs/1000;
					value=tmp/it->frames;
				} else {
					value=0;
				}
				break;
			default:
				break;
			}
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
	return value;
}

static void
gtk_iostat_draw(void *g)
{
	io_stat_graph_t *git=g;
	io_stat_t *io;
	io_stat_item_t *it;
	int i;
	guint32 value;
	guint32 last_interval, first_interval, interval_delta, delta_multiplier;
	gint32 current_interval;
	guint32 max_y;
	guint32 left_x_border;
	guint32 right_x_border;
	guint32 top_y_border;
	guint32 bottom_y_border;
#if GTK_MAJOR_VERSION < 2
	GdkFont *font;
#else
        PangoLayout  *layout;
#endif
	guint32 label_width, label_height;
	guint32 draw_width, draw_height;
	char label_string[15];

	io=git->io;
#if GTK_MAJOR_VERSION <2
	font = io->draw_area->style->font;
#endif

	if(!git->io->needs_redraw){
		return;
	}
	git->io->needs_redraw=0;

	/* if we havent specified the last_interval via the gui,
	   then just pick the most recent one */
	if(io->last_interval==0xffffffff){
		last_interval=io->max_interval;
	} else {
		last_interval=io->last_interval;
	}
	

	if(io->max_y_units==AUTO_MAX_YSCALE){
		guint32 max_value=0;
		for(i=0;i<MAX_GRAPHS;i++){
			int adv_type=0;

			if( (!io->graphs[i].display) || (!io->graphs[i].counts) ){
				continue;
			}

			if(io->count_type==COUNT_TYPE_ADVANCED){
				adv_type=proto_registrar_get_ftype(io->graphs[i].hf_index);
			}

			for(it=io->graphs[i].counts;it;it=it->next){
				value=get_it_value(io, it, adv_type, io->graphs[i].calc_type);
				if(value>=max_value){
					max_value=value;
				}
			}
		}
		max_y=yscale_max[MAX_YSCALE-1];
		for(i=MAX_YSCALE-1;i>0;i--){
			if(max_value<yscale_max[i]){
				max_y=yscale_max[i];
			}
		}
	} else {
		max_y=io->max_y_units;
	}


	/* just assume that max_y will be the longest string */
	sprintf(label_string,"%d", max_y);
#if GTK_MAJOR_VERSION < 2
        label_width=gdk_string_width(font, label_string);
        label_height=gdk_string_height(font, label_string);
#else
        layout = gtk_widget_create_pango_layout(io->draw_area, label_string);
        pango_layout_get_pixel_size(layout, &label_width, &label_height);
#endif

	left_x_border=10;
	right_x_border=label_width+20;
	top_y_border=10;
	bottom_y_border=label_height+20;

	draw_width=io->pixmap_width-right_x_border-left_x_border;
	draw_height=io->pixmap_height-top_y_border-bottom_y_border;


	/* clear out old plot */
        gdk_draw_rectangle(io->pixmap,
                           io->draw_area->style->white_gc,
                           TRUE,
                           0, 0,
                           io->draw_area->allocation.width,
                           io->draw_area->allocation.height);

	/* plot the y-scale */
	gdk_draw_line(io->pixmap, io->draw_area->style->black_gc,
		io->pixmap_width-right_x_border+1, 
		top_y_border,
		io->pixmap_width-right_x_border+1, 
		io->pixmap_height-bottom_y_border);
	for(i=0;i<=10;i++){
		int xwidth, lwidth;

		xwidth=5;
		if(!(i%5)){
			xwidth=10;
		}
		gdk_draw_line(io->pixmap, io->draw_area->style->black_gc, 
			io->pixmap_width-right_x_border+1, 
			io->pixmap_height-bottom_y_border-draw_height*i/10, 
			io->pixmap_width-right_x_border+1+xwidth, 
			io->pixmap_height-bottom_y_border-draw_height*i/10);
		sprintf(label_string,"%d", max_y*i/10);
#if GTK_MAJOR_VERSION < 2
                lwidth=gdk_string_width(font, label_string);
                gdk_draw_string(io->pixmap,
                                font,
                                io->draw_area->style->black_gc,
                                io->pixmap_width-right_x_border+15+label_width-lwidth,
                                io->pixmap_height-bottom_y_border-draw_height*i/10+label_height/2,
                                label_string);
#else
                pango_layout_set_text(layout, label_string, -1);
                pango_layout_get_pixel_size(layout, &lwidth, NULL);
		gdk_draw_layout(io->pixmap,
                                io->draw_area->style->black_gc,
                                io->pixmap_width-right_x_border+15+label_width-lwidth,
                                io->pixmap_height-bottom_y_border-draw_height*i/10-label_height/2,
                                layout);
#endif
	}

	/* plot the x-scale */
	gdk_draw_line(io->pixmap, io->draw_area->style->black_gc, left_x_border, io->pixmap_height-bottom_y_border+1, io->pixmap_width-right_x_border+1, io->pixmap_height-bottom_y_border+1);

	if((last_interval/io->interval)>draw_width/io->pixels_per_tick+1){
		first_interval=(last_interval/io->interval)-draw_width/io->pixels_per_tick+1;
		first_interval*=io->interval;
	} else {
		first_interval=0;
	}


	interval_delta=1;
	delta_multiplier=5;
	while(interval_delta<((last_interval-first_interval)/10)){
		interval_delta*=delta_multiplier;
		if(delta_multiplier==5){
			delta_multiplier=2;
		} else {
			delta_multiplier=5;
		}
	}


	for(current_interval=last_interval;current_interval>(gint32)first_interval;current_interval=current_interval-io->interval){
		int x, xlen;

		/* if pixels_per_tick is <5, only draw every 10 ticks */
		if((io->pixels_per_tick<10) && (current_interval%(10*io->interval))){
			continue;
		}

		if(current_interval%interval_delta){
			xlen=5;
		} else {
			xlen=10;
		}

		x=draw_width+left_x_border-((last_interval-current_interval)/io->interval)*io->pixels_per_tick;
		gdk_draw_line(io->pixmap, io->draw_area->style->black_gc, 
			x-1-io->pixels_per_tick/2,
			io->pixmap_height-bottom_y_border+1, 
			x-1-io->pixels_per_tick/2,
			io->pixmap_height-bottom_y_border+xlen+1);

		if(xlen==10){
			int lwidth;
			if(io->interval>=1000){
				sprintf(label_string,"%d", current_interval/1000);
			} else if(io->interval>=100){
				sprintf(label_string,"%d.%1d", current_interval/1000,(current_interval/100)%10);
			} else if(io->interval>=10){
				sprintf(label_string,"%d.%2d", current_interval/1000,(current_interval/10)%100);
			} else {
				sprintf(label_string,"%d.%3d", current_interval/1000,current_interval%1000);
			}
#if GTK_MAJOR_VERSION < 2
                        lwidth=gdk_string_width(font, label_string);
                        gdk_draw_string(io->pixmap,
                                        font,
                                        io->draw_area->style->black_gc,
                                        x-1-io->pixels_per_tick/2-lwidth/2,
                                        io->pixmap_height-bottom_y_border+15+label_height,
                                        label_string);
#else
                        pango_layout_set_text(layout, label_string, -1);
                        pango_layout_get_pixel_size(layout, &lwidth, NULL);
                        gdk_draw_layout(io->pixmap,
                                        io->draw_area->style->black_gc,
                                        x-1-io->pixels_per_tick/2-lwidth/2,
                                        io->pixmap_height-bottom_y_border+15,
                                        layout);
#endif
		}

	}
#if GTK_MAJOR_VERSION >= 2
        g_object_unref(G_OBJECT(layout));
#endif


	/* loop over all items */
	for(i=MAX_GRAPHS-1;i>=0;i--){
		int adv_type=0;
		int first_drawed=1;

		if( (!io->graphs[i].display) || (!io->graphs[i].counts) ){
			continue;
		}

		if(io->count_type==COUNT_TYPE_ADVANCED){
			adv_type=proto_registrar_get_ftype(io->graphs[i].hf_index);
		}

		/* loop over all intervals for the item */
		for(it=io->graphs[i].counts;it;it=it->next){
			guint32 startx,starty, nexty;
			guint32 val=0;
			guint32 next_val=0;

			/* skip it if is outside the graph */
			if(it->time<first_interval){
				continue;
			}
			if(it->time>last_interval){
				continue;
			}

			val=get_it_value(io, it, adv_type, io->graphs[i].calc_type);
			if(it->next){
				next_val=get_it_value(io, it->next, adv_type, io->graphs[i].calc_type);
			}


			startx=draw_width-1-io->pixels_per_tick*((last_interval-it->time)/io->interval+1);
			if(val>max_y){
				starty=0;
			} else {
				starty=draw_height-1-(val*draw_height)/max_y;
			}
			if(next_val>max_y){
				nexty=0;
			} else {
				nexty=draw_height-1-(next_val*draw_height)/max_y;
			}


			/* just skip intervals that ar all or partially outside
			   the draw_area */
			if(startx <=0){
				continue;
			}


			/* the first seen data for this graph might be 
			 * somewhere in the middle of the capture,  make sure 
			 * we start drawing a proper line at zero from the 
			 * left edge of the graph up until the measurement 
			 * point.
			 */
			if(first_drawed){
				first_drawed=0;
				gdk_draw_line(io->pixmap, io->graphs[i].gc, 10, draw_height-1+10, startx+10, draw_height-1+10);
				gdk_draw_line(io->pixmap, io->graphs[i].gc, startx+10, draw_height-1+10, startx+10, starty+10);
			}

			gdk_draw_line(io->pixmap, io->graphs[i].gc, startx+10, starty+10, startx+io->pixels_per_tick-1+10, starty+10);

			/* if there is no next measured interval but he have 
			   not reached last_interval yet, just draw 0 for the
			   rest of the graph */
			if( (!it->next) && (it->time!=last_interval) ){
				gdk_draw_line(io->pixmap, io->graphs[i].gc, startx+io->pixels_per_tick-1+10, starty+10, startx+io->pixels_per_tick-1+10, draw_height-1+10);
				gdk_draw_line(io->pixmap, io->graphs[i].gc, startx+io->pixels_per_tick-1+10, draw_height-1+10, draw_width-1+10, draw_height-1+10);
				continue;
			}

			if(!it->next){
				continue;
			}

			/* if there is data in next interval, connect to it */
			if((it->time+io->interval)==it->next->time){
				gdk_draw_line(io->pixmap, io->graphs[i].gc, startx+io->pixels_per_tick-1+10, starty+10, startx+io->pixels_per_tick-1+10, nexty+10);
				continue;
			}

			/* there is a gap until the next interval, we need to
			   draw a bit more to connect to it. */
			if((it->time+io->interval)<it->next->time){
				int nextx;
				nextx=draw_width-1-io->pixels_per_tick*((last_interval-it->next->time)/io->interval+1);
				gdk_draw_line(io->pixmap, io->graphs[i].gc, startx+io->pixels_per_tick-1+10, starty+10, startx+io->pixels_per_tick-1+10, draw_height-1+10);
				gdk_draw_line(io->pixmap, io->graphs[i].gc, startx+io->pixels_per_tick-1+10, draw_height-1+10, nextx-1+10, draw_height-1+10);
				gdk_draw_line(io->pixmap, io->graphs[i].gc, nextx-1+10, draw_height-1+10, nextx-1+10, nexty+10);
				continue;
			}
			
		}
	}

	gdk_draw_pixmap(io->draw_area->window,
			io->draw_area->style->fg_gc[GTK_WIDGET_STATE(io->draw_area)],
			io->pixmap,
			0, 0,
			0, 0,
			io->pixmap_width, io->pixmap_height);


	/* update the scrollbar */
	io->scrollbar_adjustment->upper=io->max_interval;
	io->scrollbar_adjustment->step_increment=(last_interval-first_interval)/10;
	io->scrollbar_adjustment->page_increment=(last_interval-first_interval);
	if((last_interval-first_interval)*100 < io->max_interval){
		io->scrollbar_adjustment->page_size=io->max_interval/100;
	} else {
		io->scrollbar_adjustment->page_size=(last_interval-first_interval);
	}
	io->scrollbar_adjustment->value=last_interval-io->scrollbar_adjustment->page_size;
	gtk_adjustment_changed(io->scrollbar_adjustment);
	gtk_adjustment_value_changed(io->scrollbar_adjustment);
}


static void
gtk_iostat_init(char *optarg _U_)
{
	io_stat_t *io;
	int i=0;
	static color_t col[MAX_GRAPHS] = {
		{0,	0x0000,	0x0000,	0x0000},
		{0,	0xffff,	0x0000,	0x0000},
		{0,	0x0000,	0xffff,	0x0000},
		{0,	0x0000,	0x0000,	0xffff},
		{0,	0xffff,	0x5000,	0xffff}
	};
	GString *error_string;

	io=g_malloc(sizeof(io_stat_t));
	io->needs_redraw=1;
	io->interval=1000;
	io->window=NULL;
	io->draw_area=NULL;
	io->pixmap=NULL;
	io->scrollbar=NULL;
	io->scrollbar_adjustment=NULL;
	io->pixmap_width=500;
	io->pixmap_height=200;
	io->pixels_per_tick=5;
	io->max_y_units=AUTO_MAX_YSCALE;
	io->count_type=0;
	io->last_interval=0xffffffff;
	io->max_interval=0;

	for(i=0;i<MAX_GRAPHS;i++){
		io->graphs[i].counts=g_malloc(sizeof(io_stat_item_t));
		io->graphs[i].counts->prev=io->graphs[i].counts;
		io->graphs[i].counts->next=NULL;
		io->graphs[i].counts->time=0;
		io->graphs[i].counts->frames=0;
		io->graphs[i].counts->bytes=0;
		io->graphs[i].counts->int_max=0;
		io->graphs[i].counts->int_min=0;
		io->graphs[i].counts->int_tot=0;
		io->graphs[i].gc=NULL;
		io->graphs[i].color.pixel=col[i].pixel;
		io->graphs[i].color.red=col[i].red;
		io->graphs[i].color.green=col[i].green;
		io->graphs[i].color.blue=col[i].blue;
		io->graphs[i].display=i?0:1;
		io->graphs[i].display_button=NULL;
		io->graphs[i].color_button=NULL;
		io->graphs[i].filter_button=NULL;
		io->graphs[i].advanced_buttons=NULL;
		io->graphs[i].io=io;

		io->graphs[i].args=g_malloc(sizeof(construct_args_t));
		io->graphs[i].args->title = NULL;
		io->graphs[i].args->wants_apply_button=TRUE;
		io->graphs[i].args->activate_on_ok=TRUE;

		io->graphs[i].filter_bt=NULL;
	}

	error_string=register_tap_listener("frame", &io->graphs[0], NULL, gtk_iostat_reset, gtk_iostat_packet, gtk_iostat_draw);
	if(error_string){
		fprintf(stderr, "ethereal: Can't attach io_stat tap: %s\n",
		    error_string->str);
		g_string_free(error_string, TRUE);
		g_free(io->graphs[0].counts);
		io->graphs[0].counts=NULL;
		io->graphs[0].display=0;
		io->graphs[0].display_button=NULL;
		io->graphs[0].filter_button=NULL;
		io->graphs[0].advanced_buttons=NULL;
		exit(10);
	}			
		
	/* build the GUI */
	init_io_stat_window(io);

	redissect_packets(&cfile);
	io->needs_redraw=1;
	gtk_iostat_draw(&io->graphs[0]);
}


















static gint
quit(GtkWidget *widget, GdkEventExpose *event _U_)
{
	int i;
	io_stat_t *io;

	io=g_hash_table_lookup(io_stat_widget_table, (void*)widget);
	if(io){
		g_hash_table_remove(io_stat_widget_table, (void*)widget);
	}

	for(i=0;i<MAX_GRAPHS;i++){
		io_stat_item_t *it;
		protect_thread_critical_region();
		remove_tap_listener(&io->graphs[i]);
		while((it=io->graphs[i].counts)){
			io->graphs[i].counts=io->graphs[i].counts->next;
			g_free(it);
		}
		unprotect_thread_critical_region();

		free(io->graphs[i].args->title);
		io->graphs[i].args->title=NULL;

		g_free(io->graphs[i].args);
		io->graphs[i].args=NULL;
	}
	g_free(io);

	return TRUE;
}

/* create a new backing pixmap of the appropriate size */
static gint
configure_event(GtkWidget *widget, GdkEventConfigure *event _U_)
{
	int i;
	io_stat_t *io;

	io=g_hash_table_lookup(io_stat_widget_table, (void*)widget);
	if(!io){
		exit(10);
	}

	if(io->pixmap){
		gdk_pixmap_unref(io->pixmap);
		io->pixmap=NULL;
	}

	io->pixmap=gdk_pixmap_new(widget->window,
			widget->allocation.width,
			widget->allocation.height,
			-1);
	io->pixmap_width=widget->allocation.width;
	io->pixmap_height=widget->allocation.height;

	gdk_draw_rectangle(io->pixmap,
			widget->style->white_gc,
			TRUE,
			0, 0,
			widget->allocation.width,
			widget->allocation.height);

	/* set up the colors and the GC structs for this pixmap */
	for(i=0;i<MAX_GRAPHS;i++){
		io->graphs[i].gc=gdk_gc_new(io->pixmap);
#if GTK_MAJOR_VERSION < 2
		colormap = gtk_widget_get_colormap (widget);
		if (!gdk_color_alloc (colormap, &io->graphs[i].color)){
			g_warning ("Couldn't allocate color");
		}

		gdk_gc_set_foreground(io->graphs[i].gc, &io->graphs[i].color);
#else
		gdk_gc_set_rgb_fg_color(io->graphs[i].gc, &io->graphs[i].color);
#endif
		
	}

	io->needs_redraw=1;
	gtk_iostat_draw(&io->graphs[0]);
	return TRUE;
}

static gint
scrollbar_changed(GtkWidget *widget _U_, gpointer data)
{
	io_stat_t *io=(io_stat_t *)data;
	guint32 mi;

	mi=io->scrollbar_adjustment->value+io->scrollbar_adjustment->page_size;
	if(io->last_interval==mi){
		return TRUE;
	}
	if( (io->last_interval==0xffffffff)
	&&  (mi==io->max_interval) ){
		return TRUE;
	}

	io->last_interval=(mi/io->interval)*io->interval;
	io->needs_redraw=1;
	gtk_iostat_draw(&io->graphs[0]);

	return TRUE;
}

/* redraw the screen from the backing pixmap */
static gint
expose_event(GtkWidget *widget, GdkEventExpose *event)
{
	io_stat_t *io;
	io=g_hash_table_lookup(io_stat_widget_table, (void*)widget);
	if(!io){
		exit(10);
	}


	gdk_draw_pixmap(widget->window,
			widget->style->fg_gc[GTK_WIDGET_STATE(widget)],
			io->pixmap,
			event->area.x, event->area.y,
			event->area.x, event->area.y,
			event->area.width, event->area.height);

	return FALSE;
}


static void
create_draw_area(io_stat_t *io, GtkWidget *box)
{
	io->draw_area=gtk_drawing_area_new();
	SIGNAL_CONNECT(io->draw_area, "destroy", quit, io);
	g_hash_table_insert(io_stat_widget_table, (void*)io->draw_area, (void*)io);

	WIDGET_SET_SIZE(io->draw_area, io->pixmap_width, io->pixmap_height);

	/* signals needed to handle backing pixmap */
	SIGNAL_CONNECT(io->draw_area, "expose_event", expose_event, NULL);
	SIGNAL_CONNECT(io->draw_area, "configure_event", configure_event, io);

	gtk_widget_show(io->draw_area);
	gtk_box_pack_start(GTK_BOX(box), io->draw_area, TRUE, TRUE, 0);

	/* create the associated scrollbar */
	io->scrollbar_adjustment=(GtkAdjustment *)gtk_adjustment_new(0,0,0,0,0,0);
	io->scrollbar=gtk_hscrollbar_new(io->scrollbar_adjustment);
	gtk_widget_show(io->scrollbar);
	gtk_box_pack_start(GTK_BOX(box), io->scrollbar, FALSE, FALSE, 0);
	SIGNAL_CONNECT(io->scrollbar_adjustment, "value_changed", scrollbar_changed, io);
}


static void
tick_interval_select(GtkWidget *item _U_, gpointer key)
{
	io_stat_tick_interval_t *tiv =(io_stat_tick_interval_t *)key;

	tiv->io->interval=tiv->interval;
	redissect_packets(&cfile);
	tiv->io->needs_redraw=1;
	gtk_iostat_draw(&tiv->io->graphs[0]);
}

static void
pixels_per_tick_select(GtkWidget *item _U_, gpointer key)
{
	io_stat_pixels_per_tick_t *ppt=(io_stat_pixels_per_tick_t *)key;

	ppt->io->pixels_per_tick=ppt->pixels_per_tick;
	ppt->io->needs_redraw=1;
	gtk_iostat_draw(&ppt->io->graphs[0]);
}

static void 
create_pixels_per_tick_menu_items(io_stat_t *io, GtkWidget *menu)
{
	char str[5];
	GtkWidget *menu_item;
	int i;

	for(i=0;i<MAX_PIXELS_PER_TICK;i++){
		sprintf(str,"%d", pixels_per_tick[i]);
		menu_item=gtk_menu_item_new_with_label(str);
		io->pixelspertick[i].io=io;
		io->pixelspertick[i].pixels_per_tick=pixels_per_tick[i];
		SIGNAL_CONNECT(menu_item, "activate", pixels_per_tick_select, &io->pixelspertick[i]);
		gtk_widget_show(menu_item);
		gtk_menu_append(GTK_MENU(menu), menu_item);
	}
	gtk_menu_set_active(GTK_MENU(menu), DEFAULT_PIXELS_PER_TICK);
	return;
}


static void
yscale_select(GtkWidget *item _U_, gpointer key)
{
	io_stat_yscale_t *ys=(io_stat_yscale_t *)key;

	ys->io->max_y_units=ys->yscale;
	ys->io->needs_redraw=1;
	gtk_iostat_draw(&ys->io->graphs[0]);
}

static void 
create_tick_interval_menu_items(io_stat_t *io, GtkWidget *menu)
{
	char str[15];
	GtkWidget *menu_item;
	int i;

	for(i=0;i<MAX_TICK_VALUES;i++){
		if(max_tick_values[i]>=1000){
			sprintf(str,"%d sec", max_tick_values[i]/1000);
		} else if(max_tick_values[i]>=100){
			sprintf(str,"0.%1d sec", (max_tick_values[i]/100)%10);
		} else if(max_tick_values[i]>=10){
			sprintf(str,"0.%02d sec", (max_tick_values[i]/10)%10);
		} else {
			sprintf(str,"0.%03d sec", (max_tick_values[i])%10);
		}

		menu_item=gtk_menu_item_new_with_label(str);
		io->tick_val[i].io=io;
		io->tick_val[i].interval=max_tick_values[i];
		SIGNAL_CONNECT(menu_item, "activate", tick_interval_select, &io->tick_val[i]);
		gtk_widget_show(menu_item);
		gtk_menu_append(GTK_MENU(menu), menu_item);
	}
	gtk_menu_set_active(GTK_MENU(menu), DEFAULT_TICK_VALUE);
	return;
}

static void 
create_yscale_max_menu_items(io_stat_t *io, GtkWidget *menu)
{
	char str[15];
	GtkWidget *menu_item;
	int i;

	for(i=0;i<MAX_YSCALE;i++){
		if(yscale_max[i]==AUTO_MAX_YSCALE){
			strcpy(str,"Auto");
		} else {
			sprintf(str,"%d", yscale_max[i]);
		}
		menu_item=gtk_menu_item_new_with_label(str);
		io->yscale[i].io=io;
		io->yscale[i].yscale=yscale_max[i];
		SIGNAL_CONNECT(menu_item, "activate", yscale_select, &io->yscale[i]);
		gtk_widget_show(menu_item);
		gtk_menu_append(GTK_MENU(menu), menu_item);
	}
	return;
}

static void
count_type_select(GtkWidget *item _U_, gpointer key)
{
	io_stat_count_type_t *ct=(io_stat_count_type_t *)key;

	ct->io->count_type=ct->count_type;

	if(ct->io->count_type==COUNT_TYPE_ADVANCED){
		int i;
		for(i=0;i<MAX_GRAPHS;i++){
			ct->io->graphs[i].display=0;
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ct->io->graphs[i].display_button), ct->io->graphs[i].display);
			gtk_widget_show(ct->io->graphs[i].advanced_buttons);
/* redraw the entire window so teh unhidden widgets show up, hopefully */
{GdkRectangle update_rect;
update_rect.x=0;
update_rect.y=0;
update_rect.width=ct->io->window->allocation.width;
update_rect.height=ct->io->window->allocation.height;
gtk_widget_draw(ct->io->window, &update_rect);
}
		}
	} else {
		int i;
		for(i=0;i<MAX_GRAPHS;i++){
			gtk_widget_hide(ct->io->graphs[i].advanced_buttons);
		}
	}

	ct->io->needs_redraw=1;
	gtk_iostat_draw(&ct->io->graphs[0]);
}

static void 
create_frames_or_bytes_menu_items(io_stat_t *io, GtkWidget *menu)
{
	GtkWidget *menu_item;
	int i;

	for(i=0;i<MAX_COUNT_TYPES;i++){
		menu_item=gtk_menu_item_new_with_label(max_count_types[i]);
		io->counttype[i].io=io;
		io->counttype[i].count_type=i;
		SIGNAL_CONNECT(menu_item, "activate", count_type_select, &io->counttype[i]);
		gtk_widget_show(menu_item);
		gtk_menu_append(GTK_MENU(menu), menu_item);
	}
	return;
}

static void
create_ctrl_menu(io_stat_t *io, GtkWidget *box, char *name, void (*func)(io_stat_t *io, GtkWidget *menu))
{
	GtkWidget *hbox;
	GtkWidget *label;
	GtkWidget *option_menu;
	GtkWidget *menu;

	hbox=gtk_hbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(box), hbox);
	gtk_box_set_child_packing(GTK_BOX(box), hbox, FALSE, FALSE, 0, GTK_PACK_START);
	gtk_widget_show(hbox);

	label=gtk_label_new(name);
	gtk_widget_show(label);
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

	option_menu=gtk_option_menu_new();
	menu=gtk_menu_new();
	(*func)(io, menu);
	gtk_option_menu_set_menu(GTK_OPTION_MENU(option_menu), menu);
	gtk_box_pack_end(GTK_BOX(hbox), option_menu, FALSE, FALSE, 0);
	gtk_widget_show(option_menu);
}

static void
create_ctrl_area(io_stat_t *io, GtkWidget *box)
{
	GtkWidget *vbox;

	vbox=gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(box), vbox);
	gtk_box_set_child_packing(GTK_BOX(box), vbox, FALSE, FALSE, 0, GTK_PACK_END);
	gtk_widget_show(vbox);

	create_ctrl_menu(io, vbox, "Unit:", create_frames_or_bytes_menu_items);
	create_ctrl_menu(io, vbox, "Tick Interval:", create_tick_interval_menu_items);
	create_ctrl_menu(io, vbox, "Pixels Per Tick:", create_pixels_per_tick_menu_items);
	create_ctrl_menu(io, vbox, "Y-scale:", create_yscale_max_menu_items);

	return;
}


static gint
filter_callback(GtkWidget *widget _U_, io_stat_graph_t *gio)
{
	char *filter;
	int i;
	header_field_info *hfi;

	/* first check if the field string is valid */
	if(gio->io->count_type==COUNT_TYPE_ADVANCED){
		char *field;
		field=(char *)gtk_entry_get_text(GTK_ENTRY(gio->calc_field));
		/* warn and bail out if there was no field specified */
		if(field==NULL || field[0]==0){
			simple_dialog(ESD_TYPE_WARN, NULL, "'%s' is not a valid field name", field);
			gio->display=0;
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gio->display_button), gio->display);
			gio->io->needs_redraw=1;
			gtk_iostat_draw(gio);
			return 0;
		}
		/* warn and bail out if the field could not be found */
		hfi=proto_registrar_get_byname(field);
		if(hfi==NULL){
			simple_dialog(ESD_TYPE_WARN, NULL, "'%s' is not a valid field name", field);
			gio->display=0;
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gio->display_button), gio->display);
			gio->io->needs_redraw=1;
			gtk_iostat_draw(gio);
			return 0;
		}
		gio->hf_index=hfi->id;
		/* check that the type is compatible */
		switch(hfi->type){
		case FT_UINT8:
		case FT_UINT16:
		case FT_UINT24:
		case FT_UINT32:
		case FT_INT8:
		case FT_INT16:
		case FT_INT24:
		case FT_INT32:
			/* these types support all calculations */
			break;
		case FT_RELATIVE_TIME:
			/* this type only support COUNT, MAX, MIN, AVG */
			switch(gio->calc_type){
			case CALC_TYPE_COUNT:
			case CALC_TYPE_MAX:
			case CALC_TYPE_MIN:
			case CALC_TYPE_AVG:
				break;
			default:
				simple_dialog(ESD_TYPE_WARN, NULL, "%s is not supported for %s's type", max_calc_types[gio->calc_type], field);
				gio->display=0;
				gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gio->display_button), gio->display);
				gio->io->needs_redraw=1;
				gtk_iostat_draw(gio);
				return 0;
			}
			break;
		default:
			if(gio->calc_type!=CALC_TYPE_COUNT){
				simple_dialog(ESD_TYPE_WARN, NULL, "Stat calculations is not supported for %s's type", field);
				gio->display=0;
				gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gio->display_button), gio->display);
				gio->io->needs_redraw=1;
				gtk_iostat_draw(gio);
				return 0;
			}
		}


	}

	/* first check if the filter string is valid.  Do this by just trying
	   to register, deregister a dummy listener. */
	filter=(char *)gtk_entry_get_text(GTK_ENTRY(gio->filter_button));
	if(register_tap_listener("frame", &filter, filter, NULL, NULL, NULL)){
		simple_dialog(ESD_TYPE_WARN, NULL, "%s is not a valid filter string", filter);
		if(gio->display){
			gio->display=0;
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gio->display_button), gio->display);
		}
		protect_thread_critical_region();
		remove_tap_listener(gio);
		unprotect_thread_critical_region();

		gio->io->needs_redraw=1;
		gtk_iostat_draw(gio);

		return 0;
	}
	/* just remove the dummy again */
	protect_thread_critical_region();
	remove_tap_listener(&filter);
	unprotect_thread_critical_region();

	/* this graph is not active, just update display and redraw */
	if(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gio->display_button))){
		gio->display=0;
		gio->io->needs_redraw=1;
		gtk_iostat_draw(gio);
		return 0;
	}

	/* ok, we have a valid filter and the graph is active.
	   first just try to delete any previous settings and then apply
	   the new ones.
	*/
	protect_thread_critical_region();
	remove_tap_listener(gio);
	unprotect_thread_critical_region();
	
	gio->display=1;

	/* only register the draw routine for the first gio. */
	for(i=0;i<MAX_GRAPHS;i++){
		gtk_iostat_reset(&gio->io->graphs[i]);
	}
	register_tap_listener("frame", gio, filter, gtk_iostat_reset, gtk_iostat_packet, gtk_iostat_draw);
	redissect_packets(&cfile);
	gio->io->needs_redraw=1;
	gtk_iostat_draw(gio);

	return 0;
}


static void
calc_type_select(GtkWidget *item _U_, gpointer key)
{
	io_stat_calc_type_t *ct=(io_stat_calc_type_t *)key;

	ct->gio->calc_type=ct->calc_type;

	/* disable the graph */
	ct->gio->display=0;
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ct->gio->display_button), ct->gio->display);

	ct->gio->io->needs_redraw=1;
	gtk_iostat_draw(&ct->gio->io->graphs[0]);
}


static void 
create_calc_types_menu_items(io_stat_graph_t *gio, GtkWidget *menu)
{
	GtkWidget *menu_item;
	int i;

	for(i=0;i<MAX_CALC_TYPES;i++){
		gio->calc_types[i].gio=gio;
		gio->calc_types[i].calc_type=i;
		menu_item=gtk_menu_item_new_with_label(max_calc_types[i]);
		SIGNAL_CONNECT(menu_item, "activate", calc_type_select, &gio->calc_types[i]);
		gtk_widget_show(menu_item);
		gtk_menu_append(GTK_MENU(menu), menu_item);
	}
	return;
}


static void
create_advanced_menu(io_stat_graph_t *gio, GtkWidget *box, char *name, void (*func)(io_stat_graph_t *io, GtkWidget *menu))
{
	GtkWidget *hbox;
	GtkWidget *label;
	GtkWidget *option_menu;
	GtkWidget *menu;

	hbox=gtk_hbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(box), hbox);
	gtk_box_set_child_packing(GTK_BOX(box), hbox, FALSE, FALSE, 0, GTK_PACK_START);
	gtk_widget_show(hbox);

	label=gtk_label_new(name);
	gtk_widget_show(label);
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

	option_menu=gtk_option_menu_new();
	menu=gtk_menu_new();
	(*func)(gio, menu);
	gtk_option_menu_set_menu(GTK_OPTION_MENU(option_menu), menu);
	gtk_box_pack_end(GTK_BOX(hbox), option_menu, FALSE, FALSE, 0);
	gtk_widget_show(option_menu);
}


static gint
field_callback(GtkWidget *widget _U_, io_stat_graph_t *gio)
{
	gio->display=0;
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gio->display_button), gio->display);
	gio->io->needs_redraw=1;
	gtk_iostat_draw(gio);
	return 0;
}


static void
create_advanced_field(io_stat_graph_t *gio, GtkWidget *box)
{

	gio->calc_field=gtk_entry_new_with_max_length(30);
	gtk_box_pack_start(GTK_BOX(box), gio->calc_field, FALSE, FALSE, 0);
	gtk_widget_show(gio->calc_field);
	SIGNAL_CONNECT(gio->filter_button, "activate", field_callback, gio);
}


static void
create_advanced_box(io_stat_graph_t *gio, GtkWidget *box)
{
	GtkWidget *hbox;

	hbox=gtk_hbox_new(FALSE, 0);
	gio->advanced_buttons=hbox;
	gtk_container_add(GTK_CONTAINER(box), hbox);
	gtk_box_set_child_packing(GTK_BOX(box), hbox, FALSE, FALSE, 0, GTK_PACK_START);
	gtk_widget_hide(hbox);

	create_advanced_menu(gio, hbox, "Calc:", create_calc_types_menu_items);
	create_advanced_field(gio, hbox);
}


static void
filter_button_clicked(GtkWidget *w, gpointer uio)
{
	io_stat_graph_t *gio=(io_stat_graph_t *)uio;

	display_filter_construct_cb(w, gio->args);
	return;
}

static void
create_filter_box(io_stat_graph_t *gio, GtkWidget *box, int num)
{
	GtkWidget *hbox;
	GtkWidget *label;
        char str[256];

	hbox=gtk_hbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(box), hbox);
	gtk_box_set_child_packing(GTK_BOX(box), hbox, FALSE, FALSE, 0, GTK_PACK_START);
	gtk_widget_show(hbox);


	sprintf(str, "Filter:%d", num);
	label=gtk_label_new(str);
	gtk_widget_show(label);
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

	gio->display_button=gtk_toggle_button_new();
	gtk_box_pack_start(GTK_BOX(hbox), gio->display_button, FALSE, FALSE, 0);
	gtk_widget_show(gio->display_button);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gio->display_button), gio->display);
	SIGNAL_CONNECT(gio->display_button, "toggled", filter_callback, gio);



	label=gtk_label_new("   Color:");
	gtk_widget_show(label);
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

	gio->color_button=gtk_toggle_button_new();
	gtk_box_pack_start(GTK_BOX(hbox), gio->color_button, FALSE, FALSE, 0);
	gtk_widget_show(gio->color_button);

#if GTK_MAJOR_VERSION < 2
	rc_style = gtk_rc_style_new ();
	rc_style->bg[GTK_STATE_NORMAL] = gio->color;
	rc_style->color_flags[GTK_STATE_NORMAL] |= GTK_RC_BG;
	rc_style->bg[GTK_STATE_ACTIVE] = gio->color;
	rc_style->color_flags[GTK_STATE_ACTIVE] |= GTK_RC_BG;
	rc_style->bg[GTK_STATE_PRELIGHT] = gio->color;
	rc_style->color_flags[GTK_STATE_PRELIGHT] |= GTK_RC_BG;
	rc_style->bg[GTK_STATE_SELECTED] = gio->color;
	rc_style->color_flags[GTK_STATE_SELECTED] |= GTK_RC_BG;
	rc_style->bg[GTK_STATE_INSENSITIVE] = gio->color;
	rc_style->color_flags[GTK_STATE_INSENSITIVE] |= GTK_RC_BG;
	gtk_widget_modify_style (gio->color_button, rc_style);
	gtk_rc_style_unref (rc_style);
#else
	gtk_widget_modify_bg(gio->color_button, GTK_STATE_NORMAL, &gio->color); 
	gtk_widget_modify_bg(gio->color_button, GTK_STATE_ACTIVE, &gio->color); 
	gtk_widget_modify_bg(gio->color_button, GTK_STATE_PRELIGHT, &gio->color); 
	gtk_widget_modify_bg(gio->color_button, GTK_STATE_SELECTED, &gio->color); 
	gtk_widget_modify_bg(gio->color_button, GTK_STATE_INSENSITIVE, &gio->color); 
#endif
/*	gtk_signal_connect(GTK_OBJECT(gio->display_button), "toggled", GTK_SIGNAL_FUNC(filter_callback), gio);*/


	/* filter prefs dialog --- comment out static label */
	/* label=gtk_label_new("   Filter:"); */

	/* filter prefs dialog */
	label=gtk_label_new("   ");
	gtk_widget_show(label);
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
	gio->filter_bt = gtk_button_new_with_label("Filter:");

	sprintf(str, "Ethereal: Display Filter  IO-Stat (Filter:%d)", num);
	if(gio->args->title){
		free(gio->args->title);
	}
	gio->args->title=strdup(str);	

	SIGNAL_CONNECT(gio->filter_bt, "clicked", filter_button_clicked, gio);
	SIGNAL_CONNECT(gio->filter_bt, "destroy", filter_button_destroy_cb, NULL);

	gtk_box_pack_start(GTK_BOX(hbox), gio->filter_bt, FALSE, TRUE, 0);
	gtk_widget_show(gio->filter_bt);

	gio->filter_button=gtk_entry_new_with_max_length(256);

	/* filter prefs dialog */
	OBJECT_SET_DATA(gio->filter_bt, E_FILT_TE_PTR_KEY, gio->filter_button);
	/* filter prefs dialog */

	gtk_box_pack_start(GTK_BOX(hbox), gio->filter_button, FALSE, FALSE, 0);
	gtk_widget_show(gio->filter_button);
	SIGNAL_CONNECT(gio->filter_button, "activate", filter_callback, gio);

	create_advanced_box(gio, hbox);

	return;
}

static void
create_filter_area(io_stat_t *io, GtkWidget *box)
{
	GtkWidget *vbox;
	int i;

	vbox=gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(box), vbox);
	gtk_box_set_child_packing(GTK_BOX(box), vbox, FALSE, FALSE, 0, GTK_PACK_START);
	gtk_widget_show(vbox);

	for(i=0;i<MAX_GRAPHS;i++){
		create_filter_box(&io->graphs[i], vbox, i+1);
	}

	return;
}


static void 
init_io_stat_window(io_stat_t *io)
{
	GtkWidget *vbox;
	GtkWidget *hbox;
	char title[256];

	/* create the main window */
	io->window=gtk_window_new(GTK_WINDOW_TOPLEVEL);

	gtk_widget_set_name(io->window, "I/O Statistics");

	vbox=gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(io->window), vbox);
	gtk_widget_show(vbox);

	create_draw_area(io, vbox);

	hbox=gtk_hbox_new(FALSE, 0);
	gtk_box_pack_end(GTK_BOX(vbox), hbox, FALSE, FALSE, 5);
	gtk_box_set_child_packing(GTK_BOX(vbox), hbox, FALSE, FALSE, 0, GTK_PACK_START);
	gtk_widget_show(hbox);

	create_filter_area(io, hbox);
	create_ctrl_area(io, hbox);

	gtk_widget_show(io->window);
	snprintf(title, 255, "IO-Stat: %s", get_basename(cfile.filename));
	gtk_window_set_title(GTK_WINDOW(io->window), title);
}


static void 
gtk_iostat_cb(GtkWidget *w _U_, gpointer d _U_)
{
	gtk_iostat_init(NULL);
}




void
register_tap_listener_gtk_iostat(void)
{
	io_stat_widget_table = g_hash_table_new(io_stat_widget_hash,
			io_stat_widget_equal);
	register_ethereal_tap("io,stat", gtk_iostat_init);
}

void
register_tap_menu_gtkiostat(void)
{
	register_tap_menu_item("IO/IO-Stat", gtk_iostat_cb);
}
