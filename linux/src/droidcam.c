/* DroidCam & DroidCamX (C) 2010-
 * Author: Aram G. (dev47@dev47apps.com)
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * Use at your own risk. See README file for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/limits.h>
#include <gtk/gtk.h>

#include <errno.h>
#include <string.h>

#include "common.h"
#include "connection.h"
#include "decoder.h"
#include "icon.h"

enum callbacks {
	CB_BUTTON = 0,
	CB_RADIO_WIFI,
	CB_RADIO_BTH,
	CB_RADIO_ADB,
	CB_WIFI_SRVR,
	CB_AUDIO,
	CB_BTN_OTR,
};

enum control_code {
	CB_CONTROL_ZIN = 16,  // 6
	CB_CONTROL_ZOUT, // 7
	CB_CONTROL_AF,  // 8
	CB_CONTROL_LED, // 9
};

struct settings {
	GtkEntry * ipEntry;
	GtkEntry * portEntry;
	GtkButton * button;
	int width, height;
	char connection; // Connection type
	char mirror;
	char audio;
};

/* Globals */
GtkWidget *menu;
GThread* hVideoThread;
int v_running = 0;
int thread_cmd = 0;
int wifi_srvr_mode = 0;
struct settings g_settings = {0};

extern int m_width, m_height, m_format;

/* Helper Functions */
void ShowError(const char * title, const char * msg)
{
	if (hVideoThread != NULL)
		gdk_threads_enter();

	GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", msg);
	gtk_window_set_title(GTK_WINDOW(dialog), title);
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);

	if (hVideoThread != NULL)
		gdk_threads_leave();
}

static void LoadSaveSettings(int load)
{
	char buf[PATH_MAX];
	FILE * fp;

	snprintf(buf, sizeof(buf), "%s/.droidcam/settings", getenv("HOME"));
	fp = fopen(buf, (load) ? "r" : "w");

	if (load) { // Set Defaults
		g_settings.connection = CB_RADIO_WIFI;
		gtk_entry_set_text(g_settings.ipEntry, "");
		gtk_entry_set_text(g_settings.portEntry, "4747");
		g_settings.width = 320;
		g_settings.height = 240;
	}
	if (!fp){
		printf("settings error (%s): %d '%s'\n", buf, errno, strerror(errno));
		return;
	}

	if (load)
	{
		if(fgets(buf, sizeof(buf), fp)){
			sscanf(buf, "%d-%d", &g_settings.width, &g_settings.height);
		}
		if(fgets(buf, sizeof(buf), fp)){
			buf[strlen(buf)-1] = '\0';
			gtk_entry_set_text(g_settings.ipEntry, buf);
		}

		if(fgets(buf, sizeof(buf), fp)) {
			gtk_entry_set_text(g_settings.portEntry, buf);
		}

	}
	else {
		fprintf(fp, "%d-%d\n%s\n%s", g_settings.width, g_settings.height, gtk_entry_get_text(g_settings.ipEntry), gtk_entry_get_text(g_settings.portEntry));
	}
	fclose(fp);
}

/* Audio Thread */
#if 0
void * AudioThreadProc(void * args)
{
	char stream_buf[AUDIO_INBUF_SZ + 16]; // padded so libavcodec detects the end
	SOCKET audioSocket = (SOCKET)lpParam;
	dbgprint("Audio Thread Started\n");
	a_running = true;
	..
	// Send HTTP request
	strcpy(stream_buf, AUDIO_REQ);
	if ( SendRecv(1, stream_buf, sizeof(AUDIO_REQ), audioSocket) <= 0){
		MSG_ERROR("Connection lost! (Audio)");
		goto _out;
	}
	// Recieve headers
	memset(stream_buf, 0, 8);
	if ( SendRecv(0, stream_buf, 5, audioSocket) <= 0 ){
		MSG_ERROR("Connection reset (audio)!\nDroidCam is probably busy with another client.");
		goto _out;
	}
	..
	dbgprint("Starting audio stream .. \n");
	memset(stream_buf, 0, sizeof(stream_buf));
	while (a_running) {
		if ( SendRecv(0, stream_buf, AUDIO_INBUF_SZ, audioSocket) == FALSE 
			|| DecodeAudio(stream_buf, AUDIO_INBUF_SZ) == FALSE) 
			break;
	}
_out:
	//cleanup
	dbgprint("Audio Thread Exiting\n");
	return TRUE;
}
#endif

/* Video Thread */
void * VideoThreadProc(void * args)
{
	char stream_buf[VIDEO_INBUF_SZ + 16]; // padded so libavcodec detects the end
	SOCKET videoSocket = (SOCKET) args;
	int keep_waiting = 0, no_errors;
	dbgprint("Video Thread Started s=%d\n", videoSocket);
	v_running = 1;

_wait:
	no_errors = 0;
	// We are the server
	if (videoSocket == INVALID_SOCKET)
	{
		videoSocket = (g_settings.connection == CB_RADIO_BTH)
			? accept_bth_connection()
			: accept_inet_connection(atoi(gtk_entry_get_text(g_settings.portEntry)));

		if (videoSocket == INVALID_SOCKET)
			goto _out;

		keep_waiting = 1;
	}
	else
	{
		int L = sprintf(stream_buf, VIDEO_REQ, g_settings.width, g_settings.height);
		if ( SendRecv(1, stream_buf, L, videoSocket) <= 0 ){
			MSG_ERROR("Connection lost!");
			goto _out;
		}
		dbgprint("Sent request, ");
	}
	memset(stream_buf, 0, sizeof(stream_buf));

	if ( SendRecv(0, stream_buf, 5, videoSocket) <= 0 ){
		MSG_ERROR("Connection reset!\nDroidCam is probably busy with another client");
		goto _out;
	}

	if ( decoder_prepare_video(stream_buf) == FALSE )
		goto _out;

	while (v_running != 0){
		if (thread_cmd != 0) {
			int L = sprintf(stream_buf, OTHER_REQ, thread_cmd);
			SendRecv(1, stream_buf, L, videoSocket);
			thread_cmd = 0;
		}
		if ( SendRecv(0, stream_buf, VIDEO_INBUF_SZ, videoSocket) == FALSE || DecodeVideo(stream_buf, VIDEO_INBUF_SZ) == FALSE)
			break;
	}

	if (v_running && keep_waiting){
		no_errors = 1;
	}

_out:

	dbgprint("disconnect\n");
	disconnect(videoSocket);
	decoder_cleanup();

	if (no_errors && v_running && keep_waiting){
		videoSocket = INVALID_SOCKET;
		goto _wait;
	}

	connection_cleanup();

	// gdk_threads_enter();
	// gtk_widget_set_sensitive(GTK_WIDGET(g_settings.button), TRUE);
	// gdk_threads_leave();
	dbgprint("Video Thread End\n");
	return 0;
}

static void StopVideo()
{
	v_running = 0;
	if (hVideoThread != NULL)
	{
		dbgprint("Waiting for videothread..\n");
		g_thread_join(hVideoThread);
		dbgprint("videothread joined\n");
		hVideoThread = NULL;
		//gtk_widget_set_sensitive(GTK_WIDGET(g_settings.button), TRUE);
	}
}

/* Messages */
/*
static gint button_press_event(GtkWidget *widget, GdkEvent *event){
	if (event->type == GDK_BUTTON_PRESS){
		GdkEventButton *bevent = (GdkEventButton *) event;
		//if (bevent->button == 3)
		gtk_menu_popup (GTK_MENU(menu), NULL, NULL, NULL, NULL,
						bevent->button, bevent->time);
		return TRUE;
	}
	return FALSE;
}
*/
static gboolean
accel_callback( GtkAccelGroup  *group,
		  GObject		*obj,
		  guint		   keyval,
		  GdkModifierType mod,
		  gpointer		user_data)
{
	if(v_running == 1 && thread_cmd ==0 && m_format != VIDEO_FMT_H263){
		thread_cmd = (int) user_data;
	}
}


static void the_callback(GtkWidget* widget, gpointer extra)
{
	int cb = (int) extra;
	gboolean ipEdit = TRUE;
	gboolean portEdit = TRUE;
	char * text = NULL;

_up:
	dbgprint("the_cb=%d\n", cb);
	switch (cb) {
		case CB_BUTTON:
			if (v_running)
			{
				StopVideo();
				cb = (int)g_settings.connection;
				goto _up;
			}
			else // START
			{
				char * ip = NULL;
				SOCKET s = INVALID_SOCKET;
				int port = atoi(gtk_entry_get_text(g_settings.portEntry));
				LoadSaveSettings(0); // Save

				if (g_settings.connection == CB_RADIO_ADB)
					ip = "127.0.0.1";

				else if (g_settings.connection == CB_RADIO_WIFI && wifi_srvr_mode == 0)
					ip = gtk_entry_get_text(g_settings.ipEntry);

				if (ip != NULL) // Not Bluetooth or "Server Mode", so connect first
				{
					if (strlen(ip) < 7 || port < 1024) {
						MSG_ERROR("You must enter the correct IP address (and port) to connect to.");
						break;
					}
					gtk_button_set_label(g_settings.button, "Please wait");
					s = connectDroidCam(ip, port);

					if (s == INVALID_SOCKET)
					{
						dbgprint("failed");
						gtk_button_set_label(g_settings.button, "Connect");
						break;
					}
				}

				hVideoThread = g_thread_create(VideoThreadProc, (void*)s, TRUE, NULL);
				gtk_button_set_label(g_settings.button, "Stop");
				//gtk_widget_set_sensitive(GTK_WIDGET(g_settings.button), FALSE);

				gtk_widget_set_sensitive(GTK_WIDGET(g_settings.ipEntry), FALSE);
				gtk_widget_set_sensitive(GTK_WIDGET(g_settings.portEntry), FALSE);
			}
		break;
		case CB_WIFI_SRVR:
			wifi_srvr_mode = !wifi_srvr_mode;
			if (g_settings.connection != CB_RADIO_WIFI)
				break;
		// else : fall through
		case CB_RADIO_WIFI:
		g_settings.connection = CB_RADIO_WIFI;
		if (wifi_srvr_mode){
			text = "Prepare";
			ipEdit = FALSE;
		}
		else {
			text = "Connect";
		}
		break;
		case CB_RADIO_BTH:
			g_settings.connection = CB_RADIO_BTH;
			text = "Prepare";
			ipEdit   = FALSE;
			portEdit = FALSE;
		break;
		case CB_RADIO_ADB:
			g_settings.connection = CB_RADIO_ADB;
			text = "Connect";
			ipEdit = FALSE;
		break;
		case CB_BTN_OTR:
			gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, 0, NULL);
		break;
		case CB_CONTROL_ZIN  :
		case CB_CONTROL_ZOUT :
		case CB_CONTROL_AF   :
		case CB_CONTROL_LED  :
		if(v_running == 1 && thread_cmd ==0 && m_format != VIDEO_FMT_H263){
			thread_cmd =  cb - 10;
		}
		break;
		case CB_AUDIO:
			g_settings.audio = !g_settings.audio;
		break;
	}

	if (text != NULL && v_running == 0){
		gtk_button_set_label(g_settings.button, text);
		gtk_widget_set_sensitive(GTK_WIDGET(g_settings.ipEntry), ipEdit);
		gtk_widget_set_sensitive(GTK_WIDGET(g_settings.portEntry), portEdit);
	}
}


int main(int argc, char *argv[])
{
	GtkWidget *window;
	GtkWidget *hbox, *hbox2;
	GtkWidget *vbox;
	GtkWidget *widget; // generic stuff

	// init threads
	g_thread_init(NULL);
	gdk_threads_init();
	gtk_init(&argc, &argv);

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), "DroidCam Client");
	gtk_container_set_border_width(GTK_CONTAINER(window), 1);
	gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_NONE);
	gtk_container_set_border_width(GTK_CONTAINER(window), 10);
//	gtk_widget_set_size_request(window, 250, 120);
	gtk_window_set_icon(GTK_WINDOW(window), gdk_pixbuf_new_from_inline(-1, icon_inline, FALSE, NULL));

 {
	GtkAccelGroup *gtk_accel = gtk_accel_group_new ();
	GClosure *closure = g_cclosure_new(G_CALLBACK(accel_callback), (gpointer)(CB_CONTROL_AF-10), NULL);
	gtk_accel_group_connect(gtk_accel, gdk_keyval_from_name("a"), GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE, closure);

	closure = g_cclosure_new(G_CALLBACK(accel_callback), (gpointer)(CB_CONTROL_LED-10), NULL);
	gtk_accel_group_connect(gtk_accel, gdk_keyval_from_name("l"), GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE, closure);

	closure = g_cclosure_new(G_CALLBACK(accel_callback), (gpointer)(CB_CONTROL_ZOUT-10), NULL);
	gtk_accel_group_connect(gtk_accel, gdk_keyval_from_name("minus"), 0, GTK_ACCEL_VISIBLE, closure);

	closure = g_cclosure_new(G_CALLBACK(accel_callback), (gpointer)(CB_CONTROL_ZIN-10), NULL);
	gtk_accel_group_connect(gtk_accel, gdk_keyval_from_name("equal"), 0, GTK_ACCEL_VISIBLE, closure);

	gtk_window_add_accel_group(GTK_WINDOW(window), gtk_accel);
 }
	menu = gtk_menu_new();

	widget = gtk_menu_item_new_with_label("DroidCamX Commands:");
	gtk_menu_append (GTK_MENU(menu), widget);
	gtk_widget_show (widget);
	gtk_widget_set_sensitive(widget, 0);

	widget = gtk_menu_item_new_with_label("Auto-Focus (Ctrl+A)");
	gtk_menu_append (GTK_MENU(menu), widget);
	gtk_widget_show (widget);
	g_signal_connect(widget, "activate", G_CALLBACK(the_callback), (gpointer)CB_CONTROL_AF);

	widget = gtk_menu_item_new_with_label("Toggle LED Flash (Ctrl+L)");
	gtk_menu_append (GTK_MENU(menu), widget);
	gtk_widget_show (widget);
	g_signal_connect(widget, "activate", G_CALLBACK(the_callback), (gpointer)CB_CONTROL_LED);

	widget = gtk_menu_item_new_with_label("Zoom In (+)");
   	gtk_menu_append (GTK_MENU(menu), widget);
	gtk_widget_show (widget);
	g_signal_connect(widget, "activate", G_CALLBACK(the_callback), (gpointer)CB_CONTROL_ZIN);

	widget = gtk_menu_item_new_with_label("Zoom Out (-)");
	gtk_menu_append (GTK_MENU(menu), widget);
	gtk_widget_show (widget);
	g_signal_connect(widget, "activate", G_CALLBACK(the_callback), (gpointer)CB_CONTROL_ZOUT);

	hbox = gtk_hbox_new(FALSE, 50);
	gtk_container_add(GTK_CONTAINER(window), hbox);

	// Toggle buttons
	vbox = gtk_vbox_new(FALSE, 1);

	widget = gtk_radio_button_new_with_label(NULL, "WiFi / LAN");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(widget), TRUE);
	g_signal_connect(widget, "toggled", G_CALLBACK(the_callback), (gpointer)CB_RADIO_WIFI);
	gtk_box_pack_start(GTK_BOX(vbox), widget, FALSE, FALSE, 0);
	widget = gtk_radio_button_new_with_label(gtk_radio_button_group(GTK_RADIO_BUTTON(widget)), "Bluetooth");
	g_signal_connect(widget, "toggled", G_CALLBACK(the_callback), (gpointer)CB_RADIO_BTH);
	gtk_box_pack_start(GTK_BOX(vbox), widget, FALSE, FALSE, 0);
	widget = gtk_radio_button_new_with_label(gtk_radio_button_group(GTK_RADIO_BUTTON(widget)), "ADB");
	g_signal_connect(widget, "toggled", G_CALLBACK(the_callback), (gpointer)CB_RADIO_ADB);
	gtk_box_pack_start(GTK_BOX(vbox), widget, FALSE, FALSE, 0);

	/* TODO: Figure out audio
	widget = gtk_check_button_new_with_label("Enable Audio");
	g_signal_connect(widget, "toggled", G_CALLBACK(the_callback), (gpointer)CB_AUDIO);
	gtk_box_pack_start(GTK_BOX(vbox), widget, FALSE, FALSE, 5);
	*/

	widget = gtk_check_button_new_with_label("WiFi Server Mode");
	g_signal_connect(widget, "toggled", G_CALLBACK(the_callback), (gpointer)CB_WIFI_SRVR);
	gtk_box_pack_start(GTK_BOX(vbox), widget, FALSE, FALSE, 5);

	hbox2 = gtk_hbox_new(FALSE, 1);
	widget = gtk_button_new_with_label("...");
	gtk_widget_set_size_request(widget, 40, 30);
	g_signal_connect(widget, "clicked", G_CALLBACK(the_callback), CB_BTN_OTR);
	gtk_box_pack_start(GTK_BOX(hbox2), widget, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox2, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);

	// IP/Port/Button

	vbox = gtk_vbox_new(FALSE, 5);

	hbox2 = gtk_hbox_new(FALSE, 1);
	gtk_box_pack_start(GTK_BOX(hbox2), gtk_label_new("Phone IP:"), FALSE, FALSE, 0);
	widget = gtk_entry_new_with_max_length(16);
	gtk_widget_set_size_request(widget, 120, 30);
	g_settings.ipEntry = (GtkEntry*)widget;
	gtk_box_pack_start(GTK_BOX(hbox2), widget, FALSE, FALSE, 0);

	widget = gtk_alignment_new(0,0,0,0);
	gtk_container_add(GTK_CONTAINER(widget), hbox2);
	gtk_box_pack_start(GTK_BOX(vbox), widget, FALSE, FALSE, 0);

	hbox2 = gtk_hbox_new(FALSE, 1);
	gtk_box_pack_start(GTK_BOX(hbox2), gtk_label_new("DroidCam Port:"), FALSE, FALSE, 0);
	widget = gtk_entry_new_with_max_length(5);
	gtk_widget_set_size_request(widget, 60, 30);
	g_settings.portEntry = (GtkEntry*)widget;
	gtk_box_pack_start(GTK_BOX(hbox2), widget, FALSE, FALSE, 0);

	widget = gtk_alignment_new(0,0,0,0);
	gtk_container_add(GTK_CONTAINER(widget), hbox2);
	gtk_box_pack_start(GTK_BOX(vbox), widget, FALSE, FALSE, 0);

	hbox2 = gtk_hbox_new(FALSE, 1);
	widget = gtk_button_new_with_label("Connect");
	gtk_widget_set_size_request(widget, 80, 30);
	g_signal_connect(widget, "clicked", G_CALLBACK(the_callback), CB_BUTTON);
	gtk_box_pack_start(GTK_BOX(hbox2), widget, FALSE, FALSE, 0);
	g_settings.button = (GtkButton*)widget;

	widget = gtk_alignment_new(1,0,0,0);
	gtk_container_add(GTK_CONTAINER(widget), hbox2);
	gtk_box_pack_start(GTK_BOX(vbox), widget, FALSE, FALSE, 10);

	gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);

	g_signal_connect(window, "destroy", G_CALLBACK (gtk_main_quit), NULL);
	gtk_widget_show_all(window);

	LoadSaveSettings(1); // Load
	if ( decoder_init(g_settings.width, g_settings.height) )
	{
		gdk_threads_enter();
		gtk_main();
		gdk_threads_leave();

		if (v_running == 1) StopVideo();

		decoder_fini();
		connection_cleanup();
	}

	return 0;
}
