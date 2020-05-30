/* DroidCam & DroidCamX (C) 2010-
 * https://github.com/aramg
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <string.h>
#include <sys/types.h>
#include <gtk/gtk.h>
#include <X11/Xlib.h>

#include "common.h"
#include "settings.h"
#include "connection.h"
#include "decoder.h"
#include "icon.h"

/* Globals */
GtkWidget *menu;
GtkWidget *infoText;
GtkWidget *audioCheckbox;
GtkEntry * ipEntry;
GtkEntry * portEntry;
GtkButton *start_button;
GThread* hVideoThread;
GThread* hAudioThread;

int a_running = 0;
int v_running = 0;
int thread_cmd = 0;
struct settings g_settings = {0};

extern char snd_device[32];
extern char v4l2_device[32];

void * AudioThreadProc(void * args);
void * VideoThreadProc(void * args);

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

static void Stop(void)
{
	a_running = 0;
	v_running = 0;
	dbgprint("join\n");
	if (hVideoThread) {
		g_thread_join(hVideoThread);
		hVideoThread = NULL;
	}
	if (hAudioThread) {
		g_thread_join(hAudioThread);
		hAudioThread = NULL;
	}
}

static void Start(void)
{
	char * ip = NULL;
	SOCKET s = INVALID_SOCKET;
	int port = atoi(gtk_entry_get_text(portEntry));

	if (g_settings.connection == CB_RADIO_ADB) {
		if (CheckAdbDevices(port) != 8) return;
		ip = "127.0.0.1";
	} else if (g_settings.connection == CB_RADIO_WIFI) {
		ip = (char*)gtk_entry_get_text(ipEntry);
	}

	// wifi or USB
	if (ip != NULL) {
		if (strlen(ip) < 7 || port <= 0 || port > 65535) {
			MSG_ERROR("You must enter the correct IP address (and port) to connect to.");
			return;
		}

		gtk_button_set_label(start_button, "Please wait");
		s = connect_droidcam(ip, port);
		if (s == INVALID_SOCKET) {
			gtk_button_set_label(start_button, "Connect");
			return;
		}
		strncpy(g_settings.ip, ip, sizeof(g_settings.ip));
	}
	g_settings.port = port;

	hVideoThread = g_thread_new(NULL, VideoThreadProc, (void*)s);
	if (s != INVALID_SOCKET && g_settings.audio) {
		a_running = 1;
		hAudioThread = g_thread_new(NULL, AudioThreadProc, NULL);
	}
	gtk_button_set_label(start_button, "Stop");
	gtk_widget_set_sensitive(GTK_WIDGET(ipEntry), FALSE);
	gtk_widget_set_sensitive(GTK_WIDGET(portEntry), FALSE);
	gtk_widget_set_sensitive(GTK_WIDGET(audioCheckbox), FALSE);
	SaveSettings(&g_settings);
}

/* Messages */
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
	gboolean audioBox = TRUE;
	char * text = NULL;

_up:
	dbgprint("the_cb=%d\n", cb);
	switch (cb) {
		case CB_BUTTON:
			if (v_running || a_running) {
				Stop();
				cb = (int)g_settings.connection;
				goto _up;
			}
#if 1
			Start();
#else
			decoder_show_test_image();
#endif
		break;
		case CB_WIFI_SRVR:
			g_settings.connection = CB_WIFI_SRVR;
			text = "Prepare";
			ipEdit = FALSE;
			audioBox = FALSE;
		break;
		case CB_RADIO_WIFI:
			g_settings.connection = CB_RADIO_WIFI;
			text = "Connect";
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
			g_settings.audio = (int) gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(audioCheckbox));
			dbgprint("audio=%d\n", g_settings.audio);
		break;
	}

	if (text != NULL && v_running == 0){
		gtk_button_set_label(start_button, text);
		gtk_widget_set_sensitive(GTK_WIDGET(ipEntry), ipEdit);
		gtk_widget_set_sensitive(GTK_WIDGET(portEntry), portEdit);
		gtk_widget_set_sensitive(GTK_WIDGET(audioCheckbox), audioBox);
	}
}

int main(int argc, char *argv[])
{
	char info[128];
	char port[16];
	GtkWidget *window;
	GtkWidget *hbox, *hbox2;
	GtkWidget *vbox;
	GtkWidget *radios[CB_RADIO_COUNT];
	GtkWidget *widget; // generic stuff

    GtkRequisition widget_size;
    gint total_width = 0;

	// init threads
	XInitThreads();
	gtk_init(&argc, &argv);

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), "DroidCam Client");
	gtk_container_set_border_width(GTK_CONTAINER(window), 1);
	gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_NONE);
	gtk_container_set_border_width(GTK_CONTAINER(window), 10);
//	gtk_widget_set_size_request(window, 250, 120);
	gtk_window_set_icon(GTK_WINDOW(window), gdk_pixbuf_new_from_resource("/com/dev47apps/droidcam/icon.png", NULL));

 {
	GtkAccelGroup *gtk_accel = gtk_accel_group_new ();
	GClosure *closure = g_cclosure_new(G_CALLBACK(accel_callback), (gpointer)(CB_CONTROL_AF-10), NULL);
	gtk_accel_group_connect(gtk_accel, gdk_keyval_from_name("a"), GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE, closure);

	closure = g_cclosure_new(G_CALLBACK(accel_callback), (gpointer)(CB_CONTROL_LED-10), NULL);
	gtk_accel_group_connect(gtk_accel, gdk_keyval_from_name("l"), GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE, closure);

	closure = g_cclosure_new(G_CALLBACK(accel_callback), (gpointer)(CB_CONTROL_ZOUT-10), NULL);
	gtk_accel_group_connect(gtk_accel, gdk_keyval_from_name("minus"), 0, GTK_ACCEL_VISIBLE, closure);

	closure = g_cclosure_new(G_CALLBACK(accel_callback), (gpointer)(CB_CONTROL_ZIN-10), NULL);
	gtk_accel_group_connect(gtk_accel, gdk_keyval_from_name("plus"), 0, GTK_ACCEL_VISIBLE, closure);

	closure = g_cclosure_new(G_CALLBACK(accel_callback), (gpointer)(CB_CONTROL_ZIN-10), NULL);
	gtk_accel_group_connect(gtk_accel, gdk_keyval_from_name("equal"), 0, GTK_ACCEL_VISIBLE, closure);

	gtk_window_add_accel_group(GTK_WINDOW(window), gtk_accel);
 }
	menu = gtk_menu_new();

	widget = gtk_menu_item_new_with_label("DroidCamX Commands:");
	gtk_menu_shell_append (GTK_MENU_SHELL(menu), widget);
	gtk_widget_show (widget);
	gtk_widget_set_sensitive(widget, 0);

	widget = gtk_menu_item_new_with_label("Auto-Focus (Ctrl+A)");
	gtk_menu_shell_append (GTK_MENU_SHELL(menu), widget);
	gtk_widget_show (widget);
	g_signal_connect(widget, "activate", G_CALLBACK(the_callback), (gpointer)CB_CONTROL_AF);

	widget = gtk_menu_item_new_with_label("Toggle LED Flash (Ctrl+L)");
	gtk_menu_shell_append (GTK_MENU_SHELL(menu), widget);
	gtk_widget_show (widget);
	g_signal_connect(widget, "activate", G_CALLBACK(the_callback), (gpointer)CB_CONTROL_LED);

	widget = gtk_menu_item_new_with_label("Zoom In (+)");
	gtk_menu_shell_append (GTK_MENU_SHELL(menu), widget);
	gtk_widget_show (widget);
	g_signal_connect(widget, "activate", G_CALLBACK(the_callback), (gpointer)CB_CONTROL_ZIN);

	widget = gtk_menu_item_new_with_label("Zoom Out (-)");
	gtk_menu_shell_append (GTK_MENU_SHELL(menu), widget);
	gtk_widget_show (widget);
	g_signal_connect(widget, "activate", G_CALLBACK(the_callback), (gpointer)CB_CONTROL_ZOUT);

	vbox = gtk_vbox_new(FALSE, 5);
	hbox = gtk_hbox_new(FALSE, 50);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

	// info text
	hbox2 = gtk_hbox_new(FALSE, 1);
	infoText = gtk_label_new(NULL);
	gtk_box_pack_start(GTK_BOX(hbox2), infoText, FALSE, FALSE, 2);

	widget = gtk_alignment_new(0,0,1,1);
	gtk_container_add(GTK_CONTAINER(widget), hbox2);
	gtk_box_pack_start(GTK_BOX(vbox), widget, FALSE, FALSE, 0);

	gtk_container_add(GTK_CONTAINER(window), vbox);

	// Toggle buttons
	vbox = gtk_vbox_new(FALSE, 1);

	widget = gtk_radio_button_new_with_label(NULL, "WiFi / LAN");
    gtk_widget_size_request(widget, &widget_size);
	gtk_widget_set_size_request(widget, widget_size.width, widget_size.height + 10);
	g_signal_connect(widget, "toggled", G_CALLBACK(the_callback), (gpointer)CB_RADIO_WIFI);
	gtk_box_pack_start(GTK_BOX(vbox), widget, FALSE, FALSE, 0);
	radios[CB_RADIO_WIFI] = widget;

	widget = gtk_radio_button_new_with_label(gtk_radio_button_group(GTK_RADIO_BUTTON(widget)), "Wifi Server Mode");
    gtk_widget_size_request(widget, &widget_size);
	gtk_widget_set_size_request(widget, widget_size.width + 10, widget_size.height + 10);
	g_signal_connect(widget, "toggled", G_CALLBACK(the_callback), (gpointer)CB_WIFI_SRVR);
	gtk_box_pack_start(GTK_BOX(vbox), widget, FALSE, FALSE, 0);
	radios[CB_WIFI_SRVR] = widget;

	widget = gtk_radio_button_new_with_label(gtk_radio_button_group(GTK_RADIO_BUTTON(widget)), "USB (over adb)");
    gtk_widget_size_request(widget, &widget_size);
	gtk_widget_set_size_request(widget, widget_size.width, widget_size.height + 10);
	g_signal_connect(widget, "toggled", G_CALLBACK(the_callback), (gpointer)CB_RADIO_ADB);
	gtk_box_pack_start(GTK_BOX(vbox), widget, FALSE, FALSE, 0);
	radios[CB_RADIO_ADB] = widget;

	widget = gtk_check_button_new_with_label("Enable Audio");
	g_signal_connect(widget, "toggled", G_CALLBACK(the_callback), (gpointer)CB_AUDIO);
	gtk_box_pack_start(GTK_BOX(vbox), widget, FALSE, FALSE, 5);
	audioCheckbox = widget;

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

	ipEntry = (GtkEntry*)widget;
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

	portEntry = (GtkEntry*)widget;
	gtk_box_pack_start(GTK_BOX(hbox2), widget, FALSE, FALSE, 0);

	widget = gtk_alignment_new(0,0,0,0);
	gtk_container_add(GTK_CONTAINER(widget), hbox2);
	gtk_box_pack_start(GTK_BOX(vbox), widget, FALSE, FALSE, 0);

	hbox2 = gtk_hbox_new(FALSE, 1);
	widget = gtk_button_new_with_label("Connect");

    gtk_widget_size_request(widget, &widget_size);
	gtk_widget_set_size_request(widget, widget_size.width + 40, widget_size.height + 10);

	g_signal_connect(widget, "clicked", G_CALLBACK(the_callback), (gpointer) CB_BUTTON);
	gtk_box_pack_start(GTK_BOX(hbox2), widget, FALSE, FALSE, 0);
	start_button = (GtkButton*)widget;

	widget = gtk_alignment_new(1,0,0,0);
	gtk_container_add(GTK_CONTAINER(widget), hbox2);
	gtk_box_pack_start(GTK_BOX(vbox), widget, FALSE, FALSE, 2);

	gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);

	g_signal_connect(window, "destroy", G_CALLBACK (gtk_main_quit), NULL);
	gtk_widget_show_all(window);

	LoadSettings(&g_settings);
	snprintf(port, sizeof(port), "%d", g_settings.port);
	gtk_entry_set_text(ipEntry, g_settings.ip);
	gtk_entry_set_text(portEntry, port);

	if (g_settings.connection < CB_RADIO_COUNT)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radios[g_settings.connection]), TRUE);

	if (g_settings.audio)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(audioCheckbox), TRUE);

	if ( decoder_init() )
	{
		// add info about devices
		snprintf(info, sizeof(info), "Client v" APP_VER_STR ", Video: %s, Audio: %s",
			v4l2_device, snd_device);
		gtk_label_set_text(GTK_LABEL(infoText), info);

		// set the font size
		PangoAttrList *attrlist = pango_attr_list_new();
		PangoAttribute *attr = pango_attr_size_new_absolute(12 * PANGO_SCALE);
		pango_attr_list_insert(attrlist, attr);
		gtk_label_set_attributes(GTK_LABEL(infoText), attrlist);
		pango_attr_list_unref(attrlist);

		// main loop
		gdk_threads_enter();
		gtk_main();
		gdk_threads_leave();
		Stop();
		decoder_fini();
		connection_cleanup();
	}

	return 0;
}
