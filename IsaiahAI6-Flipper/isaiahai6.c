#include <furi.h>
#include <furi_hal_serial.h>
#include <furi_hal_serial_control.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/text_input.h>
#include <gui/modules/text_box.h>
#include <storage/storage.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIEW_INPUT 0
#define VIEW_REPLY 1
#define PROMPT_SIZE 320
#define RESPONSE_SIZE 1800
#define APP_FOLDER "/ext/apps_data/isaiahai6"
#define HISTORY_PATH APP_DATA_PATH("history.txt")
#define CONTACTS_PATH APP_DATA_PATH("contacts.txt")

typedef struct {
    Gui* gui;
    ViewDispatcher* dispatcher;
    TextInput* input;
    TextBox* text_box;
    Storage* storage;
    FuriHalSerialHandle* uart;
    char prompt[PROMPT_SIZE];
    char response[RESPONSE_SIZE];
    volatile size_t rx_length;
    volatile bool rx_done;
    bool showing_reply;
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

static uint32_t navigation_callback(void* context) {
    IsaiahApp* app = context;
    if(app->showing_reply) {
        app->showing_reply = false;
        return VIEW_INPUT;
    }
    return VIEW_NONE;
}

static void input_result_callback(void* context) {
    IsaiahApp* app = context;
    app->response[0] = '\0';
    if(!app->prompt[0]) {
        snprintf(app->response, sizeof(app->response), "Type a message and press OK.");
    } else if(strncmp(app->prompt, "/contact ", 9) == 0) {
        const char* contact = app->prompt + 9;
        if(strchr(contact, '|')) {
            append_line(app, CONTACTS_PATH, "CONTACT", contact);
            snprintf(app->response, sizeof(app->response), "Contact saved:\n%s", contact);
        } else snprintf(app->response, sizeof(app->response), "Use /contact Name|Number");
    } else if(strcmp(app->prompt, "/contacts") == 0) {
        read_contacts(app);
    } else if(strcmp(app->prompt, "/help") == 0) {
        snprintf(app->response, sizeof(app->response), "IsaiahAI 6.0\nNormal text: ask ESP32 bridge.\n/contact Name|Number saves a contact.\n/contacts lists saved contacts.");
    } else if(app->uart) {
        if(!ask_bridge(app, app->prompt) && !app->response[0])
            snprintf(app->response, sizeof(app->response), "Bridge returned an invalid response.");
    } else snprintf(app->response, sizeof(app->response), "GPIO UART unavailable. Close other serial apps.");
    if(app->prompt[0]) append_line(app, HISTORY_PATH, "CHAT", app->prompt);
    text_box_set_text(app->text_box, app->response);
    text_box_set_font(app->text_box, TextBoxFontText);
    text_box_set_focus(app->text_box, TextBoxFocusStart);
    app->showing_reply = true;
    view_dispatcher_switch_to_view(app->dispatcher, VIEW_REPLY);
}

int32_t isaiahai6_app(void* p) {
    UNUSED(p);
    IsaiahApp* app = malloc(sizeof(IsaiahApp));
    if(!app) return -1;
    memset(app, 0, sizeof(IsaiahApp));
    app->storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(app->storage, APP_FOLDER);
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
    app->input = text_input_alloc();
    text_input_set_header_text(app->input, "IsaiahAI 6.0");
    text_input_set_result_callback(app->input, input_result_callback, app, app->prompt, sizeof(app->prompt), true);
    view_dispatcher_add_view(app->dispatcher, VIEW_INPUT, text_input_get_view(app->input));
    app->text_box = text_box_alloc();
    view_dispatcher_add_view(app->dispatcher, VIEW_REPLY, text_box_get_view(app->text_box));
    if(!app->uart) {
        snprintf(app->response, sizeof(app->response), "GPIO UART unavailable.\nConnect ESP32 TX to Flipper RX (pin 14), ESP32 RX to Flipper TX (pin 13), and GND to GND.");
        text_box_set_text(app->text_box, app->response);
        app->showing_reply = true;
        view_dispatcher_switch_to_view(app->dispatcher, VIEW_REPLY);
    } else view_dispatcher_switch_to_view(app->dispatcher, VIEW_INPUT);
    view_dispatcher_run(app->dispatcher);
    if(app->uart) {
        furi_hal_serial_async_rx_stop(app->uart);
        furi_hal_serial_disable_direction(app->uart, FuriHalSerialDirectionTx);
        furi_hal_serial_disable_direction(app->uart, FuriHalSerialDirectionRx);
        furi_hal_serial_deinit(app->uart);
        furi_hal_serial_control_release(app->uart);
    }
    view_dispatcher_remove_view(app->dispatcher, VIEW_INPUT);
    view_dispatcher_remove_view(app->dispatcher, VIEW_REPLY);
    text_input_free(app->input);
    text_box_free(app->text_box);
    view_dispatcher_free(app->dispatcher);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_STORAGE);
    free(app);
    return 0;
}
