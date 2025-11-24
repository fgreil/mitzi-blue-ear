// BluEar - Flipper Zero app for firmware 1.4.1
// Monitors BLE connections passively using available APIs

#include <furi.h> // Flipper Universal Registry Implementation = Core OS functionality
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/icon.h>
#include <gui/elements.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <input/input.h>
#include <furi_hal_bt.h>
#include <notification/notification_messages.h>

#define MAX_LOG_ENTRIES 50

extern const Icon I_splash, I_icon_10x10;

// View IDs
typedef enum {
    ScreenSplash,
    ScreenListen
} ViewId;

typedef struct {
    char message[64];
    uint32_t timestamp;
} LogEntry;

// Main application structure
typedef struct {
    View* view_splash;
    View* view_listen;
    ViewDispatcher* view_dispatcher;
    NotificationApp* notifications;
    FuriTimer* update_timer;
    FuriMutex* mutex;
    
    LogEntry logs[MAX_LOG_ENTRIES];
    uint8_t log_count;
    uint8_t scroll_offset;
    
    bool monitoring;
    uint32_t start_time;
    uint8_t connection_count;
} BluEar;

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================
static void log_entry(BluEar* bluear, const char* message) { // Add log entry
    furi_mutex_acquire(bluear->mutex, FuriWaitForever);
    
    if(bluear->log_count < MAX_LOG_ENTRIES) {
        snprintf(
            bluear->logs[bluear->log_count].message,
            sizeof(bluear->logs[bluear->log_count].message),
            "%s",
            message);
        bluear->logs[bluear->log_count].timestamp = 
            furi_get_tick() - bluear->start_time;
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
        bluear->logs[MAX_LOG_ENTRIES - 1].timestamp = 
            furi_get_tick() - bluear->start_time;
    }
    
    furi_mutex_release(bluear->mutex);
}

static void timer_tick(void* context) { // Timer callback to check BLE status
    BluEar* bluear = context;
    
    // Check if BLE is active (connected or advertising)
    bool is_active = furi_hal_bt_is_active();
    
    static bool was_active = false;
    
    if(is_active && !was_active) {
        bluear->connection_count++;
        log_entry(bluear, "BLE Activity Detected");
        notification_message(bluear->notifications, &sequence_blink_cyan_10);
    } else if(!is_active && was_active) {
        log_entry(bluear, "BLE Activity Ended");
    }
    
    was_active = is_active;
    
    // Trigger view update
    view_commit_model(bluear->view_listen, true);
}

// =============================================================================
// View: Splash Screen
// =============================================================================
static void render_screen_splash(Canvas* canvas, void* context) {
    UNUSED(context);
    
    canvas_clear(canvas);
	canvas_draw_icon(canvas, 48, 0, &I_splash);
    canvas_set_font(canvas, FontPrimary);
    
    // Center the title
    canvas_draw_str_aligned(canvas, 64, 15, AlignCenter, AlignCenter, "BluEar");
    canvas_draw_str_aligned(canvas, 64, 27, AlignCenter, AlignCenter, "BLE Monitor");
    
    // Draw icon or decoration (simple BLE symbol representation)
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 42, AlignCenter, AlignCenter, "v1.0");
    
    // Draw instruction
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 55, AlignCenter, AlignCenter, "Passive BLE Activity Logger");
    
    // Draw button hint at bottom
    elements_button_center(canvas, "Start");
}

// Splash screen input callback
static bool handle_input_screen_splash(InputEvent* event, void* context) {
    BluEar* bluear = context;
    bool consumed = false;
    
    if(event->type == InputTypeShort) {
        if(event->key == InputKeyOk) {
            // Switch to listen view
            view_dispatcher_switch_to_view(bluear->view_dispatcher, ScreenListen);
            consumed = true;
        } else if(event->key == InputKeyBack) {
            consumed = false; // Let system handle back to exit
        }
    }
    
    return consumed;
}

// =============================================================================
// View: Listen Screen
// =============================================================================
static void render_screen_listen(Canvas* canvas, void* context) {
    BluEar* bluear = context;
    
    furi_mutex_acquire(bluear->mutex, FuriWaitForever);
    
    canvas_clear(canvas);
	canvas_draw_icon(canvas, 1, 1, &I_icon_10x10);
    
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
    canvas_draw_str(canvas, 2, 30, bluear->monitoring ? "Status: Monitoring" : "Status: Paused");
    
    // Draw separator
    canvas_draw_line(canvas, 0, 32, 128, 32);
    
    // Draw log entries
    canvas_set_font(canvas, FontSecondary);
    uint8_t y = 42;
    uint8_t visible_lines = 3;
    
    if(bluear->log_count > 0) {
        uint8_t start_idx = bluear->log_count > visible_lines ? 
                           bluear->log_count - visible_lines - bluear->scroll_offset : 0;
        uint8_t end_idx = bluear->log_count;
        
        if(start_idx + visible_lines < end_idx) {
            end_idx = start_idx + visible_lines;
        }
        
        for(uint8_t i = start_idx; i < end_idx; i++) {
            char line[48];
            uint32_t sec = bluear->logs[i].timestamp / 1000;
            snprintf(line, sizeof(line), "[%02lu:%02lu] %s", 
                     sec / 60, sec % 60, bluear->logs[i].message);
            canvas_draw_str(canvas, 2, y, line);
            y += 10;
        }
    } else {
        canvas_draw_str(canvas, 2, y, "No events logged yet...");
    }
    
    // Draw controls
    elements_button_center(canvas, bluear->monitoring ? "Pause" : "Start");
    if(bluear->log_count > visible_lines) {
        elements_button_left(canvas, "Up");
        elements_button_right(canvas, "Down");
    }
    
    furi_mutex_release(bluear->mutex);
}

// Listen screen input callback
static bool handle_input_screen_listen(InputEvent* event, void* context) {
    BluEar* bluear = context;
    bool consumed = false;
    
    if(event->type == InputTypeShort) {
        consumed = true;
        
        switch(event->key) {
        case InputKeyOk:
            // Toggle monitoring
            furi_mutex_acquire(bluear->mutex, FuriWaitForever);
            bluear->monitoring = !bluear->monitoring;
            
            if(bluear->monitoring) {
                bluear->start_time = furi_get_tick();
                bluear->log_count = 0;
                bluear->scroll_offset = 0;
                bluear->connection_count = 0;
                log_entry(bluear, "Monitoring started");
                furi_timer_start(bluear->update_timer, 500);
            } else {
                furi_timer_stop(bluear->update_timer);
                log_entry(bluear, "Monitoring paused");
            }
            furi_mutex_release(bluear->mutex);
            break;
            
        case InputKeyUp:
            // Scroll up in logs
            furi_mutex_acquire(bluear->mutex, FuriWaitForever);
            if(bluear->scroll_offset < bluear->log_count - 3) {
                bluear->scroll_offset++;
            }
            furi_mutex_release(bluear->mutex);
            break;
            
        case InputKeyDown:
            // Scroll down in logs
            furi_mutex_acquire(bluear->mutex, FuriWaitForever);
            if(bluear->scroll_offset > 0) {
                bluear->scroll_offset--;
            }
            furi_mutex_release(bluear->mutex);
            break;
            
        case InputKeyLeft:
            // Scroll up (same as up key)
            furi_mutex_acquire(bluear->mutex, FuriWaitForever);
            if(bluear->scroll_offset < bluear->log_count - 3) {
                bluear->scroll_offset++;
            }
            furi_mutex_release(bluear->mutex);
            break;
            
        case InputKeyRight:
            // Scroll down (same as down key)
            furi_mutex_acquire(bluear->mutex, FuriWaitForever);
            if(bluear->scroll_offset > 0) {
                bluear->scroll_offset--;
            }
            furi_mutex_release(bluear->mutex);
            break;
            
        case InputKeyBack:
            consumed = false; // Let system handle back
            break;
            
        default:
            break;
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
    bluear->scroll_offset = 0;
    bluear->monitoring = false;
    bluear->start_time = furi_get_tick();
    bluear->connection_count = 0;
    
    bluear->notifications = furi_record_open(RECORD_NOTIFICATION);
    
    // Create update timer
    bluear->update_timer = furi_timer_alloc(timer_tick, FuriTimerTypePeriodic, bluear);
    
    // Create splash view
    bluear->view_splash = view_alloc();
    view_set_context(bluear->view_splash, bluear);
    view_set_draw_callback(bluear->view_splash, render_screen_splash);
    view_set_input_callback(bluear->view_splash, handle_input_screen_splash);
    
    // Create listen view
    bluear->view_listen = view_alloc();
    view_set_context(bluear->view_listen, bluear);
    view_set_draw_callback(bluear->view_listen, render_screen_listen);
    view_set_input_callback(bluear->view_listen, handle_input_screen_listen);
    
    // Create view dispatcher
    bluear->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_add_view(bluear->view_dispatcher, ScreenSplash, bluear->view_splash);
    view_dispatcher_add_view(bluear->view_dispatcher, ScreenListen, bluear->view_listen);
    view_dispatcher_switch_to_view(bluear->view_dispatcher, ScreenSplash);
    
    return bluear;
}

static void bluear_destroy(BluEar* bluear) { // Free BluEar
    // Stop monitoring if active
    if(bluear->monitoring) {
        furi_timer_stop(bluear->update_timer);
    }
    
    furi_timer_free(bluear->update_timer);
    view_dispatcher_remove_view(bluear->view_dispatcher, ScreenSplash);
    view_dispatcher_remove_view(bluear->view_dispatcher, ScreenListen);
    view_dispatcher_free(bluear->view_dispatcher);
    view_free(bluear->view_splash);
    view_free(bluear->view_listen);
    furi_mutex_free(bluear->mutex);
    furi_record_close(RECORD_NOTIFICATION);
    free(bluear);
}

// =============================================================================
// MAIN APPLICATION aka main entry point
// =============================================================================
int32_t bluear_main(void* p) {
    UNUSED(p);
    
    BluEar* bluear = bluear_create();
    
    Gui* gui = furi_record_open(RECORD_GUI);
    view_dispatcher_attach_to_gui(
        bluear->view_dispatcher, gui, ViewDispatcherTypeFullscreen);
    
    // Add initial log entries (for when user reaches listen screen)
    log_entry(bluear, "App initialized");
    log_entry(bluear, "Press OK to start");
    
    view_dispatcher_run(bluear->view_dispatcher);
    
    furi_record_close(RECORD_GUI);
    bluear_destroy(bluear);
    
    return 0;
}
