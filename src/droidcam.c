/* DroidCam & DroidCamX (C) 2010-2021
 * https://github.com/dev47apps
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <string.h>
#include <sys/types.h>
#include <gtk/gtk.h>
#include <X11/Xlib.h>
#include <libappindicator/app-indicator.h>

#include "common.h"
#include "settings.h"
#include "connection.h"
#include "decoder.h"

/* Globals */
GtkWidget *menu;
GtkWidget *menuButton;
GtkWidget *wbMenu;
GtkWidget *wbButton;
GtkWidget *elButton;
GtkWidget *infoText;
GtkWidget *audioCheckbox;
GtkWidget *videoCheckbox;
GtkEntry * ipEntry;
GtkEntry * portEntry;
GtkButton *start_button;
GThread* hVideoThread;
GThread* hAudioThread;
GThread* hDecodeThread;

char *v4l2_dev = 0;
int a_running = 0;
int v_running = 0;
int thread_cmd = 0;

struct settings g_settings = {0};

extern const char *thread_cmd_val_str;
extern char snd_device[32];
extern char v4l2_device[32];
const char *APP_ICON_FILE = "/opt/droidcam-icon.png";

void * AudioThreadProc(void * args);
void * VideoThreadProc(void * args);
void * DecodeThreadProc(void * args);

const char* wb_options[] = {
	"Automatic",
	"Incandescent",
	"Warm Fluorescent",
	"Twilight",
	"Fluorescent",
	"Daylight",
	"Cloudy Daylight",
	"Shade",
};

const char* wb_values[] = {
	"auto",
	"incandescent",
	"warm-fluorescent",
	"twilight",
	"fluorescent",
	"daylight",
	"cloudy-daylight",
	"shade",
};

/* Helper Functions */
char title[256];
char msg[256];

gboolean ShowError_GTK(gpointer data)
{
	GtkWidget *dialog = gtk_message_dialog_new(NULL,
		(GtkDialogFlags)(GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL),
		GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", msg);
	gtk_window_set_title(GTK_WINDOW(dialog), title);
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
	return FALSE;
}

void ShowError(const char* in_title, const char* in_msg)
{
	strncpy(msg, in_msg, sizeof(msg) - 1);
	msg[sizeof(msg) - 1] = '\0';
	strncpy(title, in_title, sizeof(title) - 1);
	title[sizeof(title) - 1] = '\0';
	gdk_threads_add_idle(ShowError_GTK, NULL);
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
	if (hDecodeThread) {
		g_thread_join(hDecodeThread);
		hDecodeThread = NULL;
	}
	FreeUSB();
	gtk_widget_set_sensitive(GTK_WIDGET(elButton), FALSE);
	gtk_widget_set_sensitive(GTK_WIDGET(wbButton), FALSE);
	gtk_widget_set_sensitive(GTK_WIDGET(menuButton), FALSE);
}

static void Start(void)
{
	const char* ip = NULL;
	SOCKET s = INVALID_SOCKET;
	int port = strtoul(gtk_entry_get_text(portEntry), NULL, 10);

	if (port <= 0 || port > 65535) {
		MSG_ERROR("Invalid Port value");
		return;
	}
	g_settings.port = port;

	if (g_settings.connection == CB_WIFI_SRVR) {
		v_running = 1;
		hVideoThread = g_thread_new(NULL, VideoThreadProc, (void*) (SOCKET_PTR) s);
		hDecodeThread = g_thread_new(NULL, DecodeThreadProc, NULL);
		goto EARLY_OUT;
	}

	if (!g_settings.audio && !g_settings.video) {
		MSG_ERROR("Both Audio and Video are disabled");
		return;
	}

	if (g_settings.connection == CB_RADIO_ADB) {
		if (CheckAdbDevices(port) < 0) return;
		ip = "127.0.0.1";
	} else if (g_settings.connection == CB_RADIO_IOS) {
		s = CheckiOSDevices(port);
		if (s <= 0) {
			gtk_button_set_label(start_button, "Connect");
			return;
		}
	} else if (g_settings.connection == CB_RADIO_WIFI) {
		ip = (char*)gtk_entry_get_text(ipEntry);
	} else {
		MSG_ERROR("Internal error: Invalid connection mode");
		return;
	}

	// wifi or USB
	if (ip != NULL) {
		if (strlen(ip) < 7) {
			MSG_ERROR("Invalid IP value");
			return;
		}

		gtk_button_set_label(start_button, "Please wait");
		s = Connect(ip, port);
		if (s == INVALID_SOCKET) {
			gtk_button_set_label(start_button, "Connect");
			return;
		}
		strncpy(g_settings.ip, ip, sizeof(g_settings.ip) - 1);
		g_settings.ip[sizeof(g_settings.ip) - 1] = '\0';
	}

	if (g_settings.video) {
		v_running = 1;
		hVideoThread = g_thread_new(NULL, VideoThreadProc, (void*) (SOCKET_PTR) s);
		hDecodeThread = g_thread_new(NULL, DecodeThreadProc, NULL);
	} else {
		disconnect(s);
	}

	if (g_settings.audio) {
		a_running = 1;
		hAudioThread = g_thread_new(NULL, AudioThreadProc, NULL);
	}

EARLY_OUT:
	gtk_button_set_label(start_button, "Stop");
	gtk_widget_set_sensitive(GTK_WIDGET(ipEntry), FALSE);
	gtk_widget_set_sensitive(GTK_WIDGET(portEntry), FALSE);
	gtk_widget_set_sensitive(GTK_WIDGET(audioCheckbox), FALSE);
	gtk_widget_set_sensitive(GTK_WIDGET(videoCheckbox), FALSE);
	gtk_widget_set_sensitive(GTK_WIDGET(elButton), TRUE);
	gtk_widget_set_sensitive(GTK_WIDGET(wbButton), TRUE);
	gtk_widget_set_sensitive(GTK_WIDGET(menuButton), TRUE);
}

/* Messages */

// app indicator callbacks
static void hide_window(GtkWidget* widget, gpointer extra) {
	GtkWindow* window = GTK_WINDOW(extra);
	gtk_widget_hide(GTK_WIDGET(window));
}

static void show_window(GtkWidget* widget, gpointer extra) {
	GtkWindow* window = GTK_WINDOW(extra);
	gtk_widget_show(GTK_WIDGET(window));
}

static void exit_window(GtkWidget* widget, gpointer extra) {
	GtkWindow* window = GTK_WINDOW(extra);
	gtk_window_close(window);
}

// generic callback
static void the_callback(GtkWidget* widget, gpointer extra)
{
	int cb = (uintptr_t) extra;
	gboolean ipEdit = TRUE;
	gboolean portEdit = TRUE;
	gboolean audioBox = TRUE;
	gboolean videoBox = TRUE;
	gboolean active = FALSE;
	const char* text = NULL;

_up:
	dbgprint("the_callback=%d\n", cb);
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
			videoBox = FALSE;
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
		case CB_RADIO_IOS:
			g_settings.connection = CB_RADIO_IOS;
			text = "Connect";
			ipEdit = FALSE;
		break;
		case CB_BTN_OTR:
			gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, 0, 0);
			// TODO drop support for older OSs and use
			// gtk_menu_popup_at_pointer(GTK_MENU(menu), NULL);
		break;
		case CB_BTN_WB:
			gtk_menu_popup(GTK_MENU(wbMenu), NULL, NULL, NULL, NULL, 0, 0);
		break;
		case CB_BTN_EL:
			if (v_running != 1 || thread_cmd != 0) {
				gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(elButton), FALSE);
				break;
			}

			active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(elButton));
			thread_cmd = (active) ? CB_CONTROL_EL_ON : CB_CONTROL_EL_OFF;
		break;
		case CB_AUDIO:
			g_settings.audio = (int) gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(audioCheckbox));
			dbgprint("audio=%d\n", g_settings.audio);
		break;
		case CB_VIDEO:
			g_settings.video = (int) gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(videoCheckbox));
			dbgprint("video=%d\n", g_settings.video);
		break;
	}

	if (text != NULL && v_running == 0){
		gtk_button_set_label(start_button, text);
		gtk_widget_set_sensitive(GTK_WIDGET(ipEntry), ipEdit);
		gtk_widget_set_sensitive(GTK_WIDGET(portEntry), portEdit);
		gtk_widget_set_sensitive(GTK_WIDGET(audioCheckbox), audioBox);
		gtk_widget_set_sensitive(GTK_WIDGET(videoCheckbox), videoBox);
	}
}

static void controls_callback(GtkWidget* widget, gpointer extra) {
	int cb = (uintptr_t) extra;
	dbgprint("controls_callback=%d\n", cb);
	if (v_running == 0 || thread_cmd != 0) {
		return;
	}
	switch (cb) {
		case CB_CONTROL_ZOOM_IN:
		case CB_CONTROL_ZOOM_OUT:
		case CB_CONTROL_AF:
		case CB_CONTROL_LED:
			thread_cmd = cb;
		break;
		case CB_H_FLIP:
			g_settings.horizontal_flip = decoder_horizontal_flip();
		break;
		case CB_V_FLIP:
			g_settings.vertical_flip = decoder_vertical_flip();
		break;
	}
}

// wbMenu callback
static void wb_callback(GtkWidget* widget, gpointer extra) {
	int cb = (uintptr_t) extra;
	dbgprint("wb_callback=%d\n", cb);
	if (cb < ARRAY_LEN(wb_options) && v_running == 1 && thread_cmd == 0) {
		thread_cmd_val_str = wb_values[cb];
		thread_cmd = CB_CONTROL_WB;
	}
}

// keyboard shortcuts callback
static gboolean accel_callback(GtkAccelGroup  *group, GObject *obj, guint keyval,
	GdkModifierType mod, gpointer extra)
{
	controls_callback(NULL, extra);
	return TRUE;
}

static gboolean delete_window_callback(GtkWidget *widget, GdkEvent *event, gpointer extra)
{
	if ((v_running || a_running) && g_settings.confirm_close) {
		GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(extra),
			(GtkDialogFlags)(GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL),
			GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
			"Connection will be lost.");

		gtk_window_set_title(GTK_WINDOW(dialog), "Are you sure?");
		int rc = gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
		if (rc == GTK_RESPONSE_NO)
			return TRUE; // dont quit
	}

	return FALSE;
}

/* Main */
static void usage(int argc, char *argv[]) {
	fprintf(stderr, "Options:\n"
	" -dev=PATH   Specify v4l2loopback device to use, instead of first available.\n"
	"             Ex: /dev/video5\n"
	"\n"
	" -size=WxH   Specify video size (when using the regular v4l2loopback module)\n"
	"             Ex: 640x480, 1280x720, 1920x1080\n"
	);
}

static void parse_args(int argc, char *argv[]) {
	int i = 1;
	for (; i < argc; i++) {
		if (argv[i][0] == '-' && argv[i][1] == 'd' && argv[i][3] == 'v') {
			if (argv[i][4] != '=' || argv[i][5] == 0)
				goto ERROR;

			v4l2_dev = &argv[i][5];
			continue;
		}
		if (argv[i][0] == '-' && argv[i][1] == 's' && argv[i][3] == 'z') {
			if (sscanf(argv[i], "-size=%dx%d", &g_settings.v4l2_width, &g_settings.v4l2_height) != 2)
				goto ERROR;

			continue;
		}

		if (argv[i][0] == '-' && argv[i][1] == 'h')
			goto ERROR;
	}
	return;

ERROR:
	usage(argc, argv);
	exit(1);
}

// TODO: this is broken on Debian
// Seems AppIndicator's deprecated and there is no real alternatives
// Example discussion: https://github.com/dino/dino/issues/98
#if 1
static void add_indicator(GtkWidget *window) {
	AppIndicator *indicator = app_indicator_new("droidcam", APP_ICON_FILE, APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
	GtkWidget *menu = gtk_menu_new();
	GtkWidget *name_menu_item = gtk_menu_item_new_with_label("Droidcam");
	GtkWidget *show_menu_item = gtk_menu_item_new_with_label("Show");
	GtkWidget *hide_menu_item = gtk_menu_item_new_with_label("Hide");
	GtkWidget *exit_menu_item = gtk_menu_item_new_with_label("Exit");

	gtk_widget_set_sensitive(name_menu_item, 0);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), name_menu_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), show_menu_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), hide_menu_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), exit_menu_item);

	gtk_widget_show_all(menu);
	app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);
	app_indicator_set_menu(indicator, GTK_MENU(menu));

	g_signal_connect(G_OBJECT(hide_menu_item), "activate", G_CALLBACK(hide_window), window);
	g_signal_connect(G_OBJECT(show_menu_item), "activate", G_CALLBACK(show_window), window);
	g_signal_connect(G_OBJECT(exit_menu_item), "activate", G_CALLBACK(exit_window), window);
}
#endif

int main(int argc, char *argv[])
{
	char info[128];
	char port[16];
	GtkWidget *window;
	GtkWidget *grid;
	GtkWidget *radioGroup;
	GtkWidget *menuGrid;
	GtkWidget *radios[CB_RADIO_COUNT];
	GtkWidget *widget; // generic stuff
	GClosure *closure;
	GtkAccelGroup *gtk_accel;

	// init threads
	XInitThreads();
	gtk_init(&argc, &argv);

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), "DroidCam Client");
	gtk_container_set_border_width(GTK_CONTAINER(window), 1);
	gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_NONE);
	gtk_container_set_border_width(GTK_CONTAINER(window), 4);
	gtk_window_set_icon_from_file(GTK_WINDOW(window), APP_ICON_FILE, NULL);

	// keyboard shortcuts
	gtk_accel = gtk_accel_group_new ();
	closure = g_cclosure_new(G_CALLBACK(accel_callback), (gpointer)(CB_CONTROL_AF), NULL);
	gtk_accel_group_connect(gtk_accel, GDK_KEY_A, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE, closure);

	closure = g_cclosure_new(G_CALLBACK(accel_callback), (gpointer)(CB_CONTROL_LED), NULL);
	gtk_accel_group_connect(gtk_accel, GDK_KEY_L, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE, closure);

	closure = g_cclosure_new(G_CALLBACK(accel_callback), (gpointer)(CB_CONTROL_ZOOM_OUT), NULL);
	gtk_accel_group_connect(gtk_accel, GDK_KEY_minus, (GdkModifierType)0, GTK_ACCEL_VISIBLE, closure);

	closure = g_cclosure_new(G_CALLBACK(accel_callback), (gpointer)(CB_CONTROL_ZOOM_OUT), NULL);
	gtk_accel_group_connect(gtk_accel, GDK_KEY_KP_Subtract, (GdkModifierType)0, GTK_ACCEL_VISIBLE, closure);

	closure = g_cclosure_new(G_CALLBACK(accel_callback), (gpointer)(CB_CONTROL_ZOOM_IN), NULL);
	gtk_accel_group_connect(gtk_accel, GDK_KEY_plus, (GdkModifierType)0, GTK_ACCEL_VISIBLE, closure);

	closure = g_cclosure_new(G_CALLBACK(accel_callback), (gpointer)(CB_CONTROL_ZOOM_IN), NULL);
	gtk_accel_group_connect(gtk_accel, GDK_KEY_KP_Add, (GdkModifierType)0, GTK_ACCEL_VISIBLE, closure);

	closure = g_cclosure_new(G_CALLBACK(accel_callback), (gpointer)(CB_CONTROL_ZOOM_IN), NULL);
	gtk_accel_group_connect(gtk_accel, GDK_KEY_equal, (GdkModifierType)0, GTK_ACCEL_VISIBLE, closure);

	closure = g_cclosure_new(G_CALLBACK(accel_callback), (gpointer)(CB_H_FLIP), NULL);
	gtk_accel_group_connect(gtk_accel, GDK_KEY_M, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE, closure);

	closure = g_cclosure_new(G_CALLBACK(accel_callback), (gpointer)(CB_V_FLIP), NULL);
	gtk_accel_group_connect(gtk_accel, GDK_KEY_V, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE, closure);

	gtk_window_add_accel_group(GTK_WINDOW(window), gtk_accel);

	// gui
	menu = gtk_menu_new();
	widget = gtk_menu_item_new_with_label("Auto-Focus (Ctrl+A)");
	gtk_menu_shell_append (GTK_MENU_SHELL(menu), widget);
	gtk_widget_show (widget);
	g_signal_connect(widget, "activate", G_CALLBACK(controls_callback), (gpointer)CB_CONTROL_AF);

	widget = gtk_menu_item_new_with_label("Toggle LED Flash (Ctrl+L)");
	gtk_menu_shell_append (GTK_MENU_SHELL(menu), widget);
	gtk_widget_show (widget);
	g_signal_connect(widget, "activate", G_CALLBACK(controls_callback), (gpointer)CB_CONTROL_LED);

	widget = gtk_menu_item_new_with_label("Zoom In (+)");
	gtk_menu_shell_append (GTK_MENU_SHELL(menu), widget);
	gtk_widget_show (widget);
	g_signal_connect(widget, "activate", G_CALLBACK(controls_callback), (gpointer)CB_CONTROL_ZOOM_IN);

	widget = gtk_menu_item_new_with_label("Zoom Out (-)");
	gtk_menu_shell_append (GTK_MENU_SHELL(menu), widget);
	gtk_widget_show (widget);
	g_signal_connect(widget, "activate", G_CALLBACK(controls_callback), (gpointer)CB_CONTROL_ZOOM_OUT);

	widget = gtk_menu_item_new_with_label("Horizontal Flip / Mirror (Ctrl+M)");
	gtk_menu_shell_append (GTK_MENU_SHELL(menu), widget);
	gtk_widget_show (widget);
	g_signal_connect(widget, "activate", G_CALLBACK(controls_callback), (gpointer)CB_H_FLIP);

	widget = gtk_menu_item_new_with_label("Vertical Flip (Ctrl+V)");
	gtk_menu_shell_append (GTK_MENU_SHELL(menu), widget);
	gtk_widget_show (widget);
	g_signal_connect(widget, "activate", G_CALLBACK(controls_callback), (gpointer)CB_V_FLIP);

	// white-balance menu
	wbMenu = gtk_menu_new();
	for (size_t i = 0; i < ARRAY_LEN(wb_options); i++) {
		widget = gtk_menu_item_new_with_label(wb_options[i]);
		gtk_menu_shell_append (GTK_MENU_SHELL(wbMenu), widget);
		gtk_widget_show(widget);
		g_signal_connect(widget, "activate", G_CALLBACK(wb_callback), (gpointer)i);
	}

	// Create main grid to create left and right column of the UI.
	// +-----------------------------------+
	// |---------------+    +--------------|
	// ||RadioGroup    |    |Input field  ||
	// ||              |    |Input field  ||
	// ||Toggle A/V    |    |      Connect||
	// ||[...]         |    |             ||
	// |---------------+    +--------------|
	// + InfoText                         -+
	// +-----------------------------------+
	grid = gtk_grid_new();

	// Add created grid to main window.
	gtk_container_add(GTK_CONTAINER(window), grid);

	// Columns and rows should be separated a bit.
	gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
	gtk_grid_set_row_spacing(GTK_GRID(grid), 5);

	// Create grid for radio buttons, so they are easy to distinguish from the rest
	// the elements.
	radioGroup = gtk_grid_new();

	// Put radio group as first element of left column.
	gtk_grid_attach(GTK_GRID(grid), radioGroup, 0, 0, 1, 3);

	// Create radio options.
	radios[CB_RADIO_WIFI] = gtk_radio_button_new_with_label(NULL, "WiFi / LAN");
	g_signal_connect(radios[CB_RADIO_WIFI], "toggled", G_CALLBACK(the_callback), (gpointer)CB_RADIO_WIFI);
	gtk_grid_attach(GTK_GRID(radioGroup), radios[CB_RADIO_WIFI], 0, 0, 1, 1);

	radios[CB_WIFI_SRVR] = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(radios[CB_RADIO_WIFI]), "Wifi Server Mode");
	g_signal_connect(radios[CB_WIFI_SRVR], "toggled", G_CALLBACK(the_callback), (gpointer)CB_WIFI_SRVR);
	gtk_grid_attach_next_to(GTK_GRID(radioGroup), radios[CB_WIFI_SRVR], radios[CB_RADIO_WIFI], GTK_POS_BOTTOM, 1, 1);

	radios[CB_RADIO_ADB] = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(radios[CB_WIFI_SRVR]), "USB (Android)");
	g_signal_connect(radios[CB_RADIO_ADB], "toggled", G_CALLBACK(the_callback), (gpointer)CB_RADIO_ADB);
	gtk_grid_attach_next_to(GTK_GRID(radioGroup), radios[CB_RADIO_ADB], radios[CB_WIFI_SRVR], GTK_POS_BOTTOM, 1, 1);

	radios[CB_RADIO_IOS] = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(radios[CB_RADIO_ADB]), "USB (iOS)");
	g_signal_connect(radios[CB_RADIO_IOS], "toggled", G_CALLBACK(the_callback), (gpointer)CB_RADIO_IOS);
	gtk_grid_attach_next_to(GTK_GRID(radioGroup), radios[CB_RADIO_IOS], radios[CB_RADIO_ADB], GTK_POS_BOTTOM, 1, 1);

	// Add toggle button to enable video as 2nd element of left column.
	widget = gtk_check_button_new_with_label("Enable Video");
	g_signal_connect(widget, "toggled", G_CALLBACK(the_callback), (gpointer)CB_VIDEO);
	gtk_grid_attach(GTK_GRID(grid), widget, 0, 3, 1, 1);
	videoCheckbox = widget;

	// Add toggle button to enable audio as 3rd element of left column.
	widget = gtk_check_button_new_with_label("Enable Audio");
	g_signal_connect(widget, "toggled", G_CALLBACK(the_callback), (gpointer)CB_AUDIO);
	gtk_grid_attach(GTK_GRID(grid), widget, 0, 4, 1, 1);
	audioCheckbox = widget;

	// Add [WB] Menu button
	widget = gtk_button_new_with_label("WB");
	gtk_widget_set_tooltip_text(widget, "White-balance");
	g_signal_connect(widget, "clicked", G_CALLBACK(the_callback), (gpointer)CB_BTN_WB);
	wbButton = widget;

	// Put menu button in the grid, so it's not full column width, but smaller.
	menuGrid = gtk_grid_new();
	gtk_grid_attach(GTK_GRID(menuGrid), widget, 0, 0, 1, 1);

	// Add [EL] Menu button
	widget = gtk_toggle_button_new_with_label("EL");
	gtk_widget_set_tooltip_text(widget, "Exposure Locked");
	g_signal_connect(widget, "clicked", G_CALLBACK(the_callback), (gpointer)CB_BTN_EL);
	gtk_grid_attach(GTK_GRID(menuGrid), widget, 1, 0, 1, 1);
	elButton = widget;

	// Add [...] Menu button
	widget = gtk_button_new_with_label("...");
	g_signal_connect(widget, "clicked", G_CALLBACK(the_callback), (gpointer)CB_BTN_OTR);
	gtk_grid_attach(GTK_GRID(menuGrid), widget, 2, 0, 1, 1);
	menuButton = widget;

	// attach the buttons to the column
	gtk_grid_attach(GTK_GRID(grid), menuGrid, 0, 5, 1, 1);

	// Info text goes as last element of left column.
	infoText = gtk_label_new(NULL);
	gtk_grid_attach(GTK_GRID(grid), infoText, 0, 6, 2, 1);

	// Phone IP label.
	widget = gtk_label_new("Phone IP:");
	gtk_label_set_xalign(GTK_LABEL(widget), 1.0);
	gtk_grid_attach(GTK_GRID(grid), widget, 1, 0, 1, 1);

	// And input field for phone IP.
	widget = gtk_entry_new();
	gtk_entry_set_max_length(GTK_ENTRY(widget), 16);
	ipEntry = (GtkEntry*)widget;
	gtk_grid_attach(GTK_GRID(grid), widget, 2, 0, 1, 1);

	// Port label.
	widget = gtk_label_new("DroidCam Port:");
	gtk_label_set_xalign (GTK_LABEL(widget), 1.0);
	gtk_grid_attach(GTK_GRID(grid), widget, 1, 1, 1, 1);

	// Port input field.
	widget = gtk_entry_new();
	gtk_entry_set_max_length(GTK_ENTRY(widget), 5);
	portEntry = (GtkEntry*)widget;
	gtk_grid_attach(GTK_GRID(grid), widget, 2, 1, 1, 1);

	// And finally connect button.
	widget = gtk_button_new_with_label("Connect");
	g_signal_connect(widget, "clicked", G_CALLBACK(the_callback), (gpointer) CB_BUTTON);
	start_button = (GtkButton*)widget;
	gtk_grid_attach(GTK_GRID(grid), widget, 2, 2, 1, 1);

	g_signal_connect(window, "destroy", G_CALLBACK (gtk_main_quit), NULL);
	g_signal_connect(window, "delete-event", G_CALLBACK(delete_window_callback), window);
	gtk_widget_show_all(window);

	Stop(); // reset the UI
	LoadSettings(&g_settings);
	if (argc >= 1) {
		parse_args(argc, argv);
	}

	snprintf(port, sizeof(port), "%d", g_settings.port);
	gtk_entry_set_text(ipEntry, g_settings.ip);
	gtk_entry_set_text(portEntry, port);

	if (g_settings.connection < CB_RADIO_COUNT)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radios[g_settings.connection]), TRUE);

	if (g_settings.audio)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(audioCheckbox), TRUE);

	if (g_settings.video)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(videoCheckbox), TRUE);

	if (decoder_init(v4l2_dev, g_settings.v4l2_width, g_settings.v4l2_height))
	{
		// add info about devices
		snprintf(info, sizeof(info), "Client v" APP_VER_STR ", Video: %s, Audio: %s",
			v4l2_device, snd_device);
		gtk_label_set_text(GTK_LABEL(infoText), info);
		printf("Video: %s\n", v4l2_device);
		printf("Audio: %s\n", snd_device);

		// re-load flip values from last run
		if (g_settings.horizontal_flip)
			decoder_horizontal_flip();

		if (g_settings.vertical_flip)
			decoder_vertical_flip();

		// set the font size
		PangoAttrList *attrlist = pango_attr_list_new();
		PangoAttribute *attr = pango_attr_size_new_absolute(12 * PANGO_SCALE);
		pango_attr_list_insert(attrlist, attr);
		gtk_label_set_attributes(GTK_LABEL(infoText), attrlist);
		pango_attr_list_unref(attrlist);

		// add taskbar widget
		add_indicator(window);

		// main loop
		gtk_main();
		Stop();
		decoder_fini();
		connection_cleanup();
		SaveSettings(&g_settings);
	}

	return 0;
}
