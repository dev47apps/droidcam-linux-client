/* DroidCam & DroidCamX (C) 2010-
 * https://github.com/aramg
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <gtk/gtk.h>
#include <X11/Xlib.h>

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

static int CheckAdbDevices(int port){
	char buf[256];
	int haveDevice = 0;

	system("adb start-server");
	FILE* pipe = popen("adb devices", "r");
	if (!pipe) {
		goto _exit;
	}

	while (!feof(pipe)) {
		dbgprint("->");
		if (fgets(buf, sizeof(buf), pipe) == NULL) break;
		dbgprint("Got line: %s", buf);

		if (strstr(buf, "List of") != NULL){
			haveDevice = 2;
			continue;
		}
		if (haveDevice == 2) {
			if (strstr(buf, "offline") != NULL){
				haveDevice = 4;
				break;
			}
			if (strstr(buf, "device") != NULL && strstr(buf, "??") == NULL){
				haveDevice = 8;
				break;
			}
		}
	}
	pclose(pipe);
	#define TAIL "Please refer to the website for manual adb setup info."
	if (haveDevice == 0 || haveDevice == 1) {
		MSG_ERROR("adb program not detected. " TAIL);
	}
	else if (haveDevice == 2) {
		MSG_ERROR("No devices detected. " TAIL);
	}
	else if (haveDevice == 4) {
		system("adb kill-server");
		MSG_ERROR("Device is offline. Try re-attaching device.");
	}
	else if (haveDevice == 8) {
		sprintf(buf, "adb forward tcp:%d tcp:%d", port, port);
		system(buf);
	}
_exit:
	dbgprint("haveDevice = %d\n", haveDevice);
	return haveDevice;
}

static void LoadSaveSettings(int load) {
	char buf[PATH_MAX];
	struct stat st = {0};
	FILE * fp;

	int version;

	// Set Defaults
	if (load) {
		g_settings.connection = CB_RADIO_WIFI;
		gtk_entry_set_text(g_settings.ipEntry, "");
		gtk_entry_set_text(g_settings.portEntry, "4747");
	}

	snprintf(buf, sizeof(buf), "%s/.droidcam", getenv("HOME"));
	if (stat(buf, &st) == -1) {
		mkdir(buf, 0700);
	}

	snprintf(buf, sizeof(buf), "%s/.droidcam/settings", getenv("HOME"));
	fp = fopen(buf, (load) ? "r" : "w");
	if (!fp) return;

	if (load) {
		version = 0;
		if(fgets(buf, sizeof(buf), fp)){
			sscanf(buf, "v%d", &version);
		}

		if (version != 1) {
			return;
		}

		if(fgets(buf, sizeof(buf), fp)){
			buf[strlen(buf)-1] = '\0';
			gtk_entry_set_text(g_settings.ipEntry, buf);
		}

		if(fgets(buf, sizeof(buf), fp)) {
			buf[strlen(buf)-1] = '\0';
			gtk_entry_set_text(g_settings.portEntry, buf);
		}
	}
	else {
		fprintf(fp, "v1\n%s\n%s\n",
			gtk_entry_get_text(g_settings.ipEntry),
			gtk_entry_get_text(g_settings.portEntry));
	}
	fclose(fp);
}

/* Video Thread */
void * VideoThreadProc(void * args)
{
	char buf[32];
	SOCKET videoSocket = (SOCKET) args;
	int keep_waiting = 0;
	dbgprint("Video Thread Started s=%d\n", videoSocket);
	v_running = 1;

server_wait:
	if (videoSocket == INVALID_SOCKET) {
		videoSocket = accept_connection(atoi(gtk_entry_get_text(g_settings.portEntry)));
		if (videoSocket == INVALID_SOCKET) { goto early_out; }
		keep_waiting = 1;
	}

	{
		int len = snprintf(buf, sizeof(buf), VIDEO_REQ, decoder_get_video_width(), decoder_get_video_height());
		if (SendRecv(1, buf, len, videoSocket) <= 0){
			MSG_ERROR("Error sending request, DroidCam might be busy with another client.");
			goto early_out;
		}
	}

	memset(buf, 0, sizeof(buf));
	if (SendRecv(0, buf, 9, videoSocket) <= 0) {
		MSG_ERROR("Connection reset!\nIs the app running?");
		goto early_out;
	}

	if (decoder_prepare_video(buf) == FALSE) {
		goto early_out;
	}

	while (v_running != 0){
		if (thread_cmd != 0) {
			int len = sprintf(buf, OTHER_REQ, thread_cmd);
			SendRecv(1, buf, len, videoSocket);
			thread_cmd = 0;
		}

		int frameLen;
		struct jpg_frame_s *f = decoder_get_next_frame();
		if (SendRecv(0, buf, 4, videoSocket) == FALSE) break;
		make_int4(frameLen, buf[0], buf[1], buf[2], buf[3]);
		f->length = frameLen;
		if (SendRecv(0, (char*)f->data, frameLen, videoSocket) == FALSE)
		    break;

	}

early_out:
	dbgprint("disconnect\n");
	disconnect(videoSocket);
	decoder_cleanup();

	if (v_running && keep_waiting){
		videoSocket = INVALID_SOCKET;
		goto server_wait;
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
	if(v_running == 1 && thread_cmd ==0){
		thread_cmd = (int) user_data;
	}
	return TRUE;
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
#if 1
				char * ip = NULL;
				SOCKET s = INVALID_SOCKET;
				int port = atoi(gtk_entry_get_text(g_settings.portEntry));
				LoadSaveSettings(0); // Save

				if (g_settings.connection == CB_RADIO_ADB) {
					if (CheckAdbDevices(port) != 8) return;
					ip = "127.0.0.1";
				} else if (g_settings.connection == CB_RADIO_WIFI && wifi_srvr_mode == 0) {
					ip = (char*)gtk_entry_get_text(g_settings.ipEntry);
				}

				if (ip != NULL) // Not Bluetooth or "Server Mode", so connect first
				{
					if (strlen(ip) < 7 || port < 1024) {
						MSG_ERROR("You must enter the correct IP address (and port) to connect to.");
						break;
					}
					gtk_button_set_label(g_settings.button, "Please wait");
					s = connect_droidcam(ip, port);

					if (s == INVALID_SOCKET)
					{
						dbgprint("failed");
						gtk_button_set_label(g_settings.button, "Connect");
						break;
					}
				}

				hVideoThread = g_thread_new(NULL, VideoThreadProc, (gpointer)s);
				gtk_button_set_label(g_settings.button, "Stop");
				//gtk_widget_set_sensitive(GTK_WIDGET(g_settings.button), FALSE);

				gtk_widget_set_sensitive(GTK_WIDGET(g_settings.ipEntry), FALSE);
				gtk_widget_set_sensitive(GTK_WIDGET(g_settings.portEntry), FALSE);
#else
				decoder_show_test_image();
#endif
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
			gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, 0, 0);
		break;
		case CB_CONTROL_ZIN  :
		case CB_CONTROL_ZOUT :
		case CB_CONTROL_AF   :
		case CB_CONTROL_LED  :
		if(v_running == 1 && thread_cmd ==0){
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

    GtkRequisition widget_size;
    gint total_width = 0;

	// init threads
	XInitThreads();
	gtk_init(&argc, &argv);
	memset(&g_settings, 0, sizeof(struct settings));

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

    gtk_widget_size_request(widget, &widget_size);
	gtk_widget_set_size_request(widget, widget_size.width, widget_size.height + 10);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(widget), TRUE);
	g_signal_connect(widget, "toggled", G_CALLBACK(the_callback), (gpointer)CB_RADIO_WIFI);
	gtk_box_pack_start(GTK_BOX(vbox), widget, FALSE, FALSE, 0);
	widget = gtk_radio_button_new_with_label(gtk_radio_button_group(GTK_RADIO_BUTTON(widget)), "Wifi Server Mode");

    gtk_widget_size_request(widget, &widget_size);
	gtk_widget_set_size_request(widget, widget_size.width, widget_size.height + 10);

	g_signal_connect(widget, "toggled", G_CALLBACK(the_callback), (gpointer)CB_WIFI_SRVR);
	// gtk_box_pack_start(GTK_BOX(vbox), widget, FALSE, FALSE, 0);
	// widget = gtk_radio_button_new_with_label(gtk_radio_button_group(GTK_RADIO_BUTTON(widget)), "Bluetooth");
	// g_signal_connect(widget, "toggled", G_CALLBACK(the_callback), (gpointer)CB_RADIO_BTH);
	gtk_box_pack_start(GTK_BOX(vbox), widget, FALSE, FALSE, 0);
	widget = gtk_radio_button_new_with_label(gtk_radio_button_group(GTK_RADIO_BUTTON(widget)), "USB (over adb)");

    gtk_widget_size_request(widget, &widget_size);
	gtk_widget_set_size_request(widget, widget_size.width, widget_size.height + 10);

	g_signal_connect(widget, "toggled", G_CALLBACK(the_callback), (gpointer)CB_RADIO_ADB);
	gtk_box_pack_start(GTK_BOX(vbox), widget, FALSE, FALSE, 0);

	/* TODO: Figure out audio
	widget = gtk_check_button_new_with_label("Enable Audio");
	g_signal_connect(widget, "toggled", G_CALLBACK(the_callback), (gpointer)CB_AUDIO);
	gtk_box_pack_start(GTK_BOX(vbox), widget, FALSE, FALSE, 5);
	*/

	hbox2 = gtk_hbox_new(FALSE, 1);
	widget = gtk_button_new_with_label("...");

    gtk_widget_size_request(widget, &widget_size);
	gtk_widget_set_size_request(widget, widget_size.width + 40, widget_size.height);

	g_signal_connect(widget, "clicked", G_CALLBACK(the_callback), (gpointer)CB_BTN_OTR);
	gtk_box_pack_start(GTK_BOX(hbox2), widget, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox2, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);

	// IP/Port/Button

	vbox = gtk_vbox_new(FALSE, 5);

	hbox2 = gtk_hbox_new(FALSE, 1);

    widget = gtk_label_new("Phone IP:");
    gtk_widget_size_request(widget, &widget_size);
    // Add `Phone IP` label widget with to the total.
    total_width += widget_size.width;

	gtk_box_pack_start(GTK_BOX(hbox2), widget, FALSE, FALSE, 0);
	widget = gtk_entry_new_with_max_length(16);

    gtk_widget_size_request(widget, &widget_size);
    gtk_widget_set_size_request(widget, widget_size.width + 10, widget_size.height + 10);
    // Add `Phone IP` input widget with to the total.
    total_width += widget_size.width + 10;

	g_settings.ipEntry = (GtkEntry*)widget;
	gtk_box_pack_start(GTK_BOX(hbox2), widget, FALSE, FALSE, 0);

	widget = gtk_alignment_new(0,0,0,0);
	gtk_container_add(GTK_CONTAINER(widget), hbox2);
	gtk_box_pack_start(GTK_BOX(vbox), widget, FALSE, FALSE, 0);

    hbox2 = gtk_hbox_new(FALSE, 1);
    widget = gtk_label_new("DroidCam Port:");
    gtk_widget_size_request(widget, &widget_size);
    // Subtract the width of `DroidCam Port:` label widget
    total_width -= widget_size.width;

	gtk_box_pack_start(GTK_BOX(hbox2), widget, FALSE, FALSE, 0);
	widget = gtk_entry_new_with_max_length(5);
    gtk_widget_size_request(widget, &widget_size);
    gtk_widget_set_size_request(widget, total_width, widget_size.height + 10);

	g_settings.portEntry = (GtkEntry*)widget;
	gtk_box_pack_start(GTK_BOX(hbox2), widget, FALSE, FALSE, 0);

	widget = gtk_alignment_new(0,0,0,0);
	gtk_container_add(GTK_CONTAINER(widget), hbox2);
	gtk_box_pack_start(GTK_BOX(vbox), widget, FALSE, FALSE, 0);

	hbox2 = gtk_hbox_new(FALSE, 1);
	widget = gtk_button_new_with_label("Connect");

    gtk_widget_size_request(widget, &widget_size);
	gtk_widget_set_size_request(widget, widget_size.width + 40, widget_size.height + 10);

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
	if ( decoder_init() )
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
