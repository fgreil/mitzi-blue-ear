// BluEar - Flipper Zero app for firmware 1.4.1
// Monitors BLE connections passively using available APIs

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/icon.h>
#include <gui/elements.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <input/input.h>// BluEar - Flipper Zero app for firmware 1.4.1
// Monitors BLE connections passively using available APIs

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/icon.h>
#include <gui/elements.h>
#include <input/input.h>
#include <furi_hal_bt.h>

#define MAX_LOG_ENTRIES 10

extern const Icon I_splash, I_icon_10x10;

typedef struct {
    char message[64];
} LogEntry;

// Main application structure
typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* event_queue;
    
    LogEntry logs[MAX_LOG_ENTRIES];
    uint8_t log_count;
    
    uint32_t start_time;
    uint8_t event_count;
    bool is_running;
    bool show_splash;
} BluEar;

typedef enum {
    EventTypeInput,
    EventTypeTick,
} EventType;

typedef struct {
    EventType type;
    InputEvent input;
} AppEvent;

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================
static void log_entry(BluEar* bluear, const char* message) {
    if(bluear->log_count < MAX_LOG_ENTRIES) {
        snprintf(
            bluear->logs[bluear->log_count].message,
            sizeof(bluear->logs[bluear->log_count].message),
            "%s",
            message);
        bluear->log_count++;
    } else {
        // Shift logs up
        for(uint8_t i = 0; i < MAX_LOG_ENTRIES - 1; i++) {
            bluear->logs[i] = bluear->logs[i + 1];
        }
        snprintf(
            bluear->logs[MAX_LOG_ENTRIES - 1].message,
            sizeof(bluear->logs[MAX_LOG_ENTRIES - 1].message),
            "%s",
            message);
    }
}

// =============================================================================
// RENDERING
// =============================================================================
static void render_callback(Canvas* canvas, void* ctx) {
    BluEar* bluear = ctx;
    
    canvas_clear(canvas);
    
    if(bluear->show_splash) {
        // Splash screen
        canvas_draw_icon(canvas, 48, 0, &I_splash);
        canvas_set_font(canvas, FontPrimary);   
        canvas_draw_str_aligned(canvas, 1, 1, AlignLeft, AlignTop, "BluEar");

        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 1, 10, AlignLeft, AlignTop, "A passive");
        canvas_draw_str_aligned(canvas, 1, 20, AlignLeft, AlignTop, "BLE listener");
        canvas_draw_str_aligned(canvas, 1, 49, AlignLeft, AlignTop, "Hold 'back'");
        canvas_draw_str_aligned(canvas, 1, 57, AlignLeft, AlignTop, "to exit.");
        canvas_draw_str_aligned(canvas, 110, 1, AlignLeft, AlignTop, "v0.1");
        
        elements_button_center(canvas, "Start");
    } else {
        // Listen screen
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 2, 10, "BluEar Listener");
        
        // Draw status
        canvas_set_font(canvas, FontSecondary);
        char status_str[32];
        uint32_t uptime = (furi_get_tick() - bluear->start_time) / 1000;
        snprintf(status_str, sizeof(status_str), "Up: %lus | Evts: %d", 
                 uptime, bluear->event_count);
        canvas_draw_str(canvas, 2, 20, status_str);
        
        // BT status
        bool bt_active = furi_hal_bt_is_active();
        canvas_draw_str(canvas, 2, 30, bt_active ? "BT: Active" : "BT: Idle");
        
        // Draw separator
        canvas_draw_line(canvas, 0, 32, 128, 32);
        
        // Draw log entries
        canvas_set_font(canvas, FontSecondary);
        uint8_t y = 42;
        
        if(bluear->log_count > 0) {
            uint8_t start_idx = bluear->log_count > 4 ? bluear->log_count - 4 : 0;
            
            for(uint8_t i = start_idx; i < bluear->log_count; i++) {
                canvas_draw_str(canvas, 2, y, bluear->logs[i].message);
                y += 10;
            }
        } else {
            canvas_draw_str(canvas, 2, y, "Waiting for BLE...");
        }
    }
}

// =============================================================================
// INPUT HANDLING
// =============================================================================
static void input_callback(InputEvent* input_event, void* ctx) {
    BluEar* bluear = ctx;
    AppEvent event = {.type = EventTypeInput, .input = *input_event};
    furi_message_queue_put(bluear->event_queue, &event, FuriWaitForever);
}

// =============================================================================
// APPLICATION LIFECYCLE
// =============================================================================
static BluEar* bluear_alloc() {
    BluEar* bluear = malloc(sizeof(BluEar));
    memset(bluear, 0, sizeof(BluEar));
    
    bluear->event_queue = furi_message_queue_alloc(8, sizeof(AppEvent));
    bluear->view_port = view_port_alloc();
    bluear->gui = furi_record_open(RECORD_GUI);
    
    bluear->start_time = furi_get_tick();
    bluear->is_running = true;
    bluear->show_splash = true;
    
    view_port_draw_callback_set(bluear->view_port, render_callback, bluear);
    view_port_input_callback_set(bluear->view_port, input_callback, bluear);
    
    gui_add_view_port(bluear->gui, bluear->view_port, GuiLayerFullscreen);
    
    return bluear;
}

static void bluear_free(BluEar* bluear) {
    gui_remove_view_port(bluear->gui, bluear->view_port);
    view_port_free(bluear->view_port);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(bluear->event_queue);
    free(bluear);
}

// =============================================================================
// MAIN APPLICATION
// =============================================================================
int32_t bluear_main(void* p) {
    UNUSED(p);
    
    BluEar* bluear = bluear_alloc();
    
    log_entry(bluear, "App started");
    
    AppEvent event;
    bool bt_was_active = false;
    
    while(bluear->is_running) {
        if(furi_message_queue_get(bluear->event_queue, &event, 100) == FuriStatusOk) {
            if(event.type == EventTypeInput) {
                if(event.input.type == InputTypeLong && event.input.key == InputKeyBack) {
                    bluear->is_running = false;
                } else if(event.input.type == InputTypeShort) {
                    if(bluear->show_splash && event.input.key == InputKeyOk) {
                        bluear->show_splash = false;
                        log_entry(bluear, "Listening started");
                        view_port_update(bluear->view_port);
                    } else if(!bluear->show_splash && event.input.key == InputKeyBack) {
                        bluear->show_splash = true;
                        view_port_update(bluear->view_port);
                    }
                }
            }
        }
        
        // Check BT state periodically (only when not on splash)
        if(!bluear->show_splash) {
            bool bt_active = furi_hal_bt_is_active();
            
            if(bt_active && !bt_was_active) {
                bluear->event_count++;
                log_entry(bluear, "BLE Activity Started");
                view_port_update(bluear->view_port);
            } else if(!bt_active && bt_was_active) {
                log_entry(bluear, "BLE Activity Stopped");
                view_port_update(bluear->view_port);
            }
            
            bt_was_active = bt_active;
        }
    }
    
    bluear_free(bluear);
    
    return 0;
}
#include <furi_hal_bt.h>

#define MAX_LOG_ENTRIES 50

extern const Icon I_splash, I_icon_10x10;

// View IDs
typedef enum {
    ScreenSplash,
    ScreenListen
} ViewId;

typedef struct {
    char message[64];
} LogEntry;

// Main application structure
typedef struct {
    View* view_splash;
    View* view_listen;
    ViewDispatcher* view_dispatcher;
    FuriTimer* update_timer;
    FuriMutex* mutex;
    
    LogEntry logs[MAX_LOG_ENTRIES];
    uint8_t log_count;
    
    uint32_t start_time;
    uint8_t connection_count;
    ViewId current_view;
} BluEar;

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================
static void log_entry(BluEar* bluear, const char* message) {
    furi_mutex_acquire(bluear->mutex, FuriWaitForever);
    
    if(bluear->log_count < MAX_LOG_ENTRIES) {
        snprintf(
            bluear->logs[bluear->log_count].message,
            sizeof(bluear->logs[bluear->log_count].message),
            "%s",
            message);
        bluear->log_count++;
    } else {
        // Shift logs up
        for(uint8_t i = 0; i < MAX_LOG_ENTRIES - 1; i++) {
            bluear->logs[i] = bluear->logs[i + 1];
        }
        snprintf(
            bluear->logs[MAX_LOG_ENTRIES - 1].message,
            sizeof(bluear->logs[MAX_LOG_ENTRIES - 1].message),
            "%s",
            message);
    }
    
    furi_mutex_release(bluear->mutex);
}

static void timer_tick(void* context) {
    BluEar* bluear = context;
    
    // Check if BLE is active (connected or advertising)
    bool is_active = furi_hal_bt_is_active();
    
    static bool was_active = false;
    
    if(is_active && !was_active) {
        bluear->connection_count++;
        log_entry(bluear, "BLE Activity Detected");
    } else if(!is_active && was_active) {
        log_entry(bluear, "BLE Activity Ended");
    }
    
    was_active = is_active;
}

// =============================================================================
// View: Splash Screen
// =============================================================================
static void render_screen_splash(Canvas* canvas, void* context) {
    UNUSED(context);
    
    canvas_clear(canvas);
    canvas_draw_icon(canvas, 48, 0, &I_splash);
    canvas_set_font(canvas, FontPrimary);   
    canvas_draw_str_aligned(canvas, 1, 1, AlignLeft, AlignTop, "BluEar");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 1, 10, AlignLeft, AlignTop, "A passive");
	canvas_draw_str_aligned(canvas, 1, 20, AlignLeft, AlignTop, "BLE listener");
    canvas_draw_str_aligned(canvas, 1, 49, AlignLeft, AlignTop, "Hold 'back'");
    canvas_draw_str_aligned(canvas, 1, 57, AlignLeft, AlignTop, "to exit.");
    canvas_draw_str_aligned(canvas, 110, 1, AlignLeft, AlignTop, "v0.1");
    
    
    elements_button_center(canvas, "Start");
}

// =============================================================================
// View: Listen Screen
// =============================================================================
static void render_screen_listen(Canvas* canvas, void* context) {
    BluEar* bluear = context;
    
    furi_mutex_acquire(bluear->mutex, FuriWaitForever);
    
    canvas_clear(canvas);    
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 12, 10, "BluEar");
    
    // Draw status
    canvas_set_font(canvas, FontSecondary);
    char status_str[32];
    uint32_t uptime = (furi_get_tick() - bluear->start_time) / 1000;
    snprintf(status_str, sizeof(status_str), "Uptime: %lus | Events: %d", 
             uptime, bluear->connection_count);
    canvas_draw_str(canvas, 2, 20, status_str);
    
    // Draw monitoring status
    canvas_draw_str(canvas, 2, 30, "Status: Monitoring");
    
    // Draw separator
    canvas_draw_line(canvas, 0, 32, 128, 32);
    
    // Draw last 3 log entries
    canvas_set_font(canvas, FontSecondary);
    uint8_t y = 42;
    uint8_t visible_lines = 3;
    
    if(bluear->log_count > 0) {
        uint8_t start_idx = bluear->log_count > visible_lines ? 
                           bluear->log_count - visible_lines : 0;
        
        for(uint8_t i = start_idx; i < bluear->log_count; i++) {
            canvas_draw_str(canvas, 2, y, bluear->logs[i].message);
            y += 10;
        }
    } else {
        canvas_draw_str(canvas, 2, y, "No events logged yet...");
    }
    
    furi_mutex_release(bluear->mutex);
}

// =============================================================================
// Unified Input Handler
// =============================================================================
static bool handle_input(InputEvent* event, void* context) {
    BluEar* bluear = context;
    bool consumed = false;
    
    if(event->type == InputTypeShort || event->type == InputTypeLong) {
        if(bluear->current_view == ScreenSplash) {
            // Splash screen input
            if(event->key == InputKeyOk && event->type == InputTypeShort) {
                bluear->current_view = ScreenListen;
                view_dispatcher_switch_to_view(bluear->view_dispatcher, ScreenListen);
                consumed = true;
            } else if(event->key == InputKeyBack && event->type == InputTypeLong) {
                view_dispatcher_stop(bluear->view_dispatcher);
                consumed = true;
            }
        } else if(bluear->current_view == ScreenListen) {
            // Listen screen input
            if(event->key == InputKeyBack) {
                if(event->type == InputTypeShort) {
                    bluear->current_view = ScreenSplash;
                    view_dispatcher_switch_to_view(bluear->view_dispatcher, ScreenSplash);
                    consumed = true;
                } else if(event->type == InputTypeLong) {
                    view_dispatcher_stop(bluear->view_dispatcher);
                    consumed = true;
                }
            }
        }
    }
    
    return consumed;
}

// =============================================================================
// Application Lifecycle
// =============================================================================
static BluEar* bluear_create() {
    BluEar* bluear = malloc(sizeof(BluEar));
    
    bluear->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    bluear->log_count = 0;
    bluear->start_time = furi_get_tick();
    bluear->connection_count = 0;
    bluear->current_view = ScreenSplash;
    
    // Create update timer - starts monitoring immediately
    bluear->update_timer = furi_timer_alloc(timer_tick, FuriTimerTypePeriodic, bluear);
    furi_timer_start(bluear->update_timer, 500);
    
    // Create splash view
    bluear->view_splash = view_alloc();
    view_set_context(bluear->view_splash, bluear);
    view_set_draw_callback(bluear->view_splash, render_screen_splash);
    view_set_input_callback(bluear->view_splash, handle_input);
    
    // Create listen view
    bluear->view_listen = view_alloc();
    view_set_context(bluear->view_listen, bluear);
    view_set_draw_callback(bluear->view_listen, render_screen_listen);
    view_set_input_callback(bluear->view_listen, handle_input);
    
    // Create view dispatcher
    bluear->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_add_view(bluear->view_dispatcher, ScreenSplash, bluear->view_splash);
    view_dispatcher_add_view(bluear->view_dispatcher, ScreenListen, bluear->view_listen);
    view_dispatcher_switch_to_view(bluear->view_dispatcher, ScreenSplash);
    
    return bluear;
}

static void bluear_destroy(BluEar* bluear) {
    furi_timer_stop(bluear->update_timer);
    furi_timer_free(bluear->update_timer);
    view_dispatcher_remove_view(bluear->view_dispatcher, ScreenSplash);
    view_dispatcher_remove_view(bluear->view_dispatcher, ScreenListen);
    view_dispatcher_free(bluear->view_dispatcher);
    view_free(bluear->view_splash);
    view_free(bluear->view_listen);
    furi_mutex_free(bluear->mutex);
    free(bluear);
}

// =============================================================================
// MAIN APPLICATION
// =============================================================================
int32_t bluear_main(void* p) {
    UNUSED(p);
    
    BluEar* bluear = bluear_create();
    
    Gui* gui = furi_record_open(RECORD_GUI);
    view_dispatcher_attach_to_gui(
        bluear->view_dispatcher, gui, ViewDispatcherTypeFullscreen);
    
    // Add initial log entry
    log_entry(bluear, "App initialized");
    
    view_dispatcher_run(bluear->view_dispatcher);
    
    furi_record_close(RECORD_GUI);
    bluear_destroy(bluear);
    
    return 0;
}
