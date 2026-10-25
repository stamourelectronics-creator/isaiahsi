#include <furi.h>
#include <furi_hal_serial.h>
#include <furi_hal_serial_control.h>
#include <furi_hal_bt.h>
#include <furi_hal_usb_hid.h>
#include <furi_ble/profile_interface.h>

typedef struct Bt Bt;
typedef enum {
    BtStatusUnavailable,
    BtStatusOff,
    BtStatusAdvertising,
    BtStatusConnected,
} BtStatus;
typedef void (*BtStatusChangedCallback)(BtStatus status, void* context);
#define RECORD_BT "bt"
extern FuriHalBleProfileBase* bt_profile_start(Bt* bt, const FuriHalBleProfileTemplate* profile_template, FuriHalBleProfileParams params);
extern bool bt_profile_restore_default(Bt* bt);
extern void bt_disconnect(Bt* bt);
extern void bt_set_status_changed_callback(Bt* bt, BtStatusChangedCallback callback, void* context);
extern void bt_keys_storage_set_storage_path(Bt* bt, const char* path);
extern void bt_keys_storage_set_default_path(Bt* bt);
#include <ble_profile/extra_profiles/hid_profile.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_input.h>
#include <gui/modules/text_box.h>
#include <storage/storage.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIEW_MENU 0
#define VIEW_INPUT 1
#define VIEW_REPLY 2
#define PROMPT_SIZE 320
#define RESPONSE_SIZE 1800
#define APP_FOLDER "/ext/apps_data/isaiahai6"
#define HISTORY_PATH APP_DATA_PATH("history.txt")
#define CONTACTS_PATH APP_DATA_PATH("contacts.txt")
#define BLE_KEYS_PATH APP_DATA_PATH(".bt_hid.keys")

typedef enum {
    ModeAiChat,
    ModeBluetoothMessage,
} InputMode;

typedef struct {
    Gui* gui;
    ViewDispatcher* dispatcher;
    Submenu* menu;
    TextInput* input;
    TextBox* text_box;
    Storage* storage;
    FuriHalSerialHandle* uart;
    Bt* bt;
    FuriHalBleProfileBase* ble_profile;
    char prompt[PROMPT_SIZE];
    char response[RESPONSE_SIZE];
    volatile size_t rx_length;
    volatile bool rx_done;
    volatile bool ble_connected;
    bool showing_reply;
    bool editing_input;
    InputMode input_mode;
} IsaiahApp;

static void append_line(IsaiahApp* app, const char* path, const char* label, const char* value) {
    storage_common_mkdir(app->storage, APP_FOLDER);
    File* file = storage_file_alloc(app->storage);
    if(storage_file_open(file, path, FSAM_WRITE, FSOM_OPEN_APPEND)) {
        storage_file_write(file, label, strlen(label));
        storage_file_write(file, ": ", 2);
        storage_file_write(file, value, strlen(value));
        storage_file_write(file, "\n", 1);
        storage_file_close(file);
    }
    storage_file_free(file);
}

static void read_contacts(IsaiahApp* app) {
    app->response[0] = '\0';
    File* file = storage_file_alloc(app->storage);
    if(storage_file_open(file, CONTACTS_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        size_t used = 0;
        while(used < sizeof(app->response) - 1) {
            size_t n = storage_file_read(file, app->response + used, sizeof(app->response) - 1 - used);
            if(!n) break;
            used += n;
        }
        app->response[used] = '\0';
        storage_file_close(file);
    }
    storage_file_free(file);
    if(!app->response[0]) snprintf(app->response, sizeof(app->response), "No contacts saved yet. Use /contact Name|Number.");
}

static void serial_rx_callback(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* context) {
    IsaiahApp* app = context;
    if(!(event & FuriHalSerialRxEventData)) return;
    while(furi_hal_serial_async_rx_available(handle)) {
        uint8_t c = furi_hal_serial_async_rx(handle);
        if(c == '\r') continue;
        if(c == '\n') {
            app->response[app->rx_length] = '\0';
            app->rx_done = true;
            return;
        }
        if(app->rx_length < sizeof(app->response) - 1) app->response[app->rx_length++] = (char)c;
        else {
            app->response[sizeof(app->response) - 1] = '\0';
            app->rx_done = true;
            return;
        }
    }
}

static bool ask_bridge(IsaiahApp* app, const char* prompt) {
    char tx[PROMPT_SIZE + 8];
    int n = snprintf(tx, sizeof(tx), "ASK:%s\n", prompt);
    if(n <= 0 || (size_t)n >= sizeof(tx)) return false;
    app->rx_length = 0;
    app->rx_done = false;
    app->response[0] = '\0';
    furi_hal_serial_tx(app->uart, (const uint8_t*)tx, (size_t)n);
    furi_hal_serial_tx_wait_complete(app->uart);
    uint32_t started = furi_get_tick();
    while(!app->rx_done && (furi_get_tick() - started) < furi_ms_to_ticks(55000)) furi_delay_ms(10);
    if(!app->rx_done) {
        snprintf(app->response, sizeof(app->response), "Timed out. Check ESP32, Wi-Fi and AI API setup.");
        return false;
    }
    if(strncmp(app->response, "REPLY:", 6) == 0) {
        memmove(app->response, app->response + 6, strlen(app->response + 6) + 1);
        return true;
    }
    return false;
}

static void ble_status_changed(BtStatus status, void* context) {
    IsaiahApp* app = context;
    app->ble_connected = (status == BtStatusConnected);
}

static bool start_bluetooth_keyboard(IsaiahApp* app) {
    if(app->ble_profile) {
        furi_hal_bt_start_advertising();
        return true;
    }
    bt_disconnect(app->bt);
    furi_delay_ms(200);
    bt_keys_storage_set_storage_path(app->bt, BLE_KEYS_PATH);
    app->ble_profile = bt_profile_start(app->bt, ble_profile_hid, NULL);
    if(!app->ble_profile) {
        bt_keys_storage_set_default_path(app->bt);
        return false;
    }
    bt_set_status_changed_callback(app->bt, ble_status_changed, app);
    furi_hal_bt_start_advertising();
    return true;
}

static void ble_type_ascii(IsaiahApp* app, const char* text) {
    for(const unsigned char* p = (const unsigned char*)text; *p; p++) {
        if(*p < 32 || *p > 126) continue;
        uint16_t key = HID_ASCII_TO_KEY(*p);
        if(key == HID_KEYBOARD_NONE) continue;
        if(!ble_profile_hid_kb_press(app->ble_profile, key)) {
            furi_delay_ms(20);
            continue;
        }
        furi_delay_ms(18);
        ble_profile_hid_kb_release(app->ble_profile, key);
        furi_delay_ms(18);
    }
}

static void show_reply(IsaiahApp* app, const char* message) {
    snprintf(app->response, sizeof(app->response), "%s", message);
    text_box_set_text(app->text_box, app->response);
    text_box_set_font(app->text_box, TextBoxFontText);
    text_box_set_focus(app->text_box, TextBoxFocusStart);
    app->showing_reply = true;
    app->editing_input = false;
    view_dispatcher_switch_to_view(app->dispatcher, VIEW_REPLY);
}

static void input_result_callback(void* context) {
    IsaiahApp* app = context;
    app->response[0] = '\0';

    if(app->input_mode == ModeBluetoothMessage) {
        if(!app->prompt[0]) {
            show_reply(app, "No message entered.");
            return;
        }
        if(!app->ble_profile || !app->ble_connected) {
            show_reply(app, "iPhone is not connected.\n\nChoose Connect iPhone, pair it with IsaiahAI 6.0 in iOS Bluetooth settings, then try again.");
            return;
        }
        ble_type_ascii(app, app->prompt);
        /* Return is sent only after the user explicitly confirms on the Flipper.
           iOS may insert a newline rather than send, depending on the app/settings. */
        ble_profile_hid_kb_press(app->ble_profile, HID_KEYBOARD_RETURN);
        furi_delay_ms(25);
        ble_profile_hid_kb_release(app->ble_profile, HID_KEYBOARD_RETURN);
        append_line(app, HISTORY_PATH, "IPHONE_MESSAGE_TYPED", app->prompt);
        show_reply(app, "Message typed over Bluetooth.\n\nIf Messages sent it, you're done. If it created a new line, tap Send on the iPhone.\n\nOnly basic English/ASCII characters are supported. Open the intended conversation and focus its message field before sending.");
        return;
    }

    if(!app->prompt[0]) {
        show_reply(app, "Type a message and press OK.");
    } else if(strncmp(app->prompt, "/contact ", 9) == 0) {
        const char* contact = app->prompt + 9;
        if(strchr(contact, '|')) {
            append_line(app, CONTACTS_PATH, "CONTACT", contact);
            snprintf(app->response, sizeof(app->response), "Contact saved:\n%s", contact);
        } else snprintf(app->response, sizeof(app->response), "Use /contact Name|Number");
        show_reply(app, app->response);
    } else if(strcmp(app->prompt, "/contacts") == 0) {
        read_contacts(app);
        show_reply(app, app->response);
    } else if(strcmp(app->prompt, "/help") == 0) {
        show_reply(app, "IsaiahAI 6.0\n\nAI Chat: uses the ESP32 UART bridge.\nConnect iPhone: pair as a Bluetooth keyboard.\nType & Send: types into the currently focused iPhone text field and presses Return.\n/contact Name|Number saves a contact.\n/contacts lists contacts.\n\nThe Flipper cannot directly read iMessages or select recipients.");
    } else if(app->uart) {
        if(!ask_bridge(app, app->prompt) && !app->response[0])
            snprintf(app->response, sizeof(app->response), "Bridge returned an invalid response.");
        append_line(app, HISTORY_PATH, "CHAT", app->prompt);
        show_reply(app, app->response);
    } else {
        show_reply(app, "GPIO UART unavailable. Close other serial apps.");
    }
}

static void submenu_callback(void* context, uint32_t index) {
    IsaiahApp* app = context;
    if(index == 0) {
        app->input_mode = ModeAiChat;
        app->editing_input = true;
        app->prompt[0] = '\0';
        text_input_set_header_text(app->input, "IsaiahAI 6.0 - AI Chat");
        view_dispatcher_switch_to_view(app->dispatcher, VIEW_INPUT);
    } else if(index == 1) {
        if(start_bluetooth_keyboard(app)) {
            show_reply(app, "Bluetooth keyboard is advertising.\n\nOn your iPhone, open Settings > Bluetooth and select the IsaiahAI/Flipper keyboard. Complete pairing if asked. Return here after it connects.");
        } else {
            show_reply(app, "Could not start Bluetooth HID. Close other Bluetooth HID apps and restart IsaiahAI.");
        }
    } else if(index == 2) {
        if(!app->ble_profile || !app->ble_connected) {
            show_reply(app, "Not connected to iPhone yet.\n\nChoose Connect iPhone first, then pair in iOS Settings > Bluetooth.");
        } else {
            app->input_mode = ModeBluetoothMessage;
            app->editing_input = true;
            app->prompt[0] = '\0';
            text_input_set_header_text(app->input, "Type iPhone message");
            view_dispatcher_switch_to_view(app->dispatcher, VIEW_INPUT);
        }
    } else if(index == 3) {
        show_reply(app, app->ble_connected ? "Bluetooth status: Connected" : "Bluetooth status: Not connected");
    }
}

static bool navigation_callback(void* context) {
    IsaiahApp* app = context;
    if(app->showing_reply) {
        app->showing_reply = false;
        view_dispatcher_switch_to_view(app->dispatcher, VIEW_MENU);
        return true;
    }
    if(app->editing_input) {
        app->editing_input = false;
        view_dispatcher_switch_to_view(app->dispatcher, VIEW_MENU);
        return true;
    }
    return false;
}

int32_t isaiahai6_app(void* p) {
    UNUSED(p);
    IsaiahApp* app = malloc(sizeof(IsaiahApp));
    if(!app) return -1;
    memset(app, 0, sizeof(IsaiahApp));

    app->storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(app->storage, APP_FOLDER);
    app->bt = furi_record_open(RECORD_BT);

    app->uart = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(app->uart) {
        furi_hal_serial_init(app->uart, 115200);
        furi_hal_serial_enable_direction(app->uart, FuriHalSerialDirectionTx);
        furi_hal_serial_enable_direction(app->uart, FuriHalSerialDirectionRx);
        furi_hal_serial_async_rx_start(app->uart, serial_rx_callback, app, false);
    }

    app->gui = furi_record_open(RECORD_GUI);
    app->dispatcher = view_dispatcher_alloc();
    view_dispatcher_enable_queue(app->dispatcher);
    view_dispatcher_set_navigation_event_callback(app->dispatcher, navigation_callback);
    view_dispatcher_attach_to_gui(app->dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->menu = submenu_alloc();
    submenu_set_header(app->menu, "IsaiahAI 6.0");
    submenu_add_item(app->menu, "AI Chat (ESP32)", 0, submenu_callback, app);
    submenu_add_item(app->menu, "Connect iPhone", 1, submenu_callback, app);
    submenu_add_item(app->menu, "Type & Send Message", 2, submenu_callback, app);
    submenu_add_item(app->menu, "Bluetooth Status", 3, submenu_callback, app);
    view_dispatcher_add_view(app->dispatcher, VIEW_MENU, submenu_get_view(app->menu));

    app->input = text_input_alloc();
    text_input_set_header_text(app->input, "IsaiahAI 6.0");
    text_input_set_result_callback(app->input, input_result_callback, app, app->prompt, sizeof(app->prompt), true);
    view_dispatcher_add_view(app->dispatcher, VIEW_INPUT, text_input_get_view(app->input));

    app->text_box = text_box_alloc();
    view_dispatcher_add_view(app->dispatcher, VIEW_REPLY, text_box_get_view(app->text_box));

    view_dispatcher_switch_to_view(app->dispatcher, VIEW_MENU);
    view_dispatcher_run(app->dispatcher);

    if(app->ble_profile) {
        bt_set_status_changed_callback(app->bt, NULL, NULL);
        bt_disconnect(app->bt);
        furi_delay_ms(200);
        bt_keys_storage_set_default_path(app->bt);
        furi_check(bt_profile_restore_default(app->bt));
    }
    furi_record_close(RECORD_BT);

    if(app->uart) {
        furi_hal_serial_async_rx_stop(app->uart);
        furi_hal_serial_disable_direction(app->uart, FuriHalSerialDirectionTx);
        furi_hal_serial_disable_direction(app->uart, FuriHalSerialDirectionRx);
        furi_hal_serial_deinit(app->uart);
        furi_hal_serial_control_release(app->uart);
    }
    view_dispatcher_remove_view(app->dispatcher, VIEW_MENU);
    view_dispatcher_remove_view(app->dispatcher, VIEW_INPUT);
    view_dispatcher_remove_view(app->dispatcher, VIEW_REPLY);
    submenu_free(app->menu);
    text_input_free(app->input);
    text_box_free(app->text_box);
    view_dispatcher_free(app->dispatcher);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_STORAGE);
    free(app);
    return 0;
}
