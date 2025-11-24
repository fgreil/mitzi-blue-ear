// BluEar - Flipper Zero app for firmware 1.4.1
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
    
    if(bluear->show_splash) { // Splash screen     
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
    } else { // Listen screen
        canvas_set_font(canvas, FontPrimary);
		canvas_draw_icon(canvas, 1, 1, &I_icon_10x10);
        canvas_draw_str_aligned(canvas, 13, 2, AlignLeft, AlignTop, "BluEar Listener");
        
        // Draw status
        canvas_set_font(canvas, FontSecondary);
        char status_str[32];
        uint32_t uptime = (furi_get_tick() - bluear->start_time) / 1000;
        snprintf(status_str, sizeof(status_str), "Up: %lus | Evts: %d", 
                 uptime, bluear->event_count);
        canvas_draw_str(canvas, 2, 20, status_str);
        
        bool bt_active = furi_hal_bt_is_active(); // BT status
        canvas_draw_str(canvas, 80, 20, bt_active ? "BT: Active" : "BT: Idle");
        canvas_draw_line(canvas, 0, 22, 128, 22);
        // Draw log entries
        uint8_t y = 33; // y_offset
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
