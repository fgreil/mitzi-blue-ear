// BLE Passive Monitor - Flipper Zero app for firmware 1.4.1
// Monitors BLE connections passively using available APIs
// Since active scanning is not supported in OFW 1.4.1, this shows connection events

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <input/input.h>
#include <furi_hal_bt.h>
#include <notification/notification_messages.h>

#define MAX_LOG_ENTRIES 50

typedef struct {
    char message[64];
    uint32_t timestamp;
} LogEntry;

typedef struct {
    View* view;
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
} BleMonitor;

// Add log entry
static void add_log(BleMonitor* monitor, const char* message) {
    furi_mutex_acquire(monitor->mutex, FuriWaitForever);
    
    if(monitor->log_count < MAX_LOG_ENTRIES) {
        snprintf(
            monitor->logs[monitor->log_count].message,
            sizeof(monitor->logs[monitor->log_count].message),
            "%s",
            message);
        monitor->logs[monitor->log_count].timestamp = 
            furi_get_tick() - monitor->start_time;
        monitor->log_count++;
    } else {
        // Shift logs up
        for(uint8_t i = 0; i < MAX_LOG_ENTRIES - 1; i++) {
            monitor->logs[i] = monitor->logs[i + 1];
        }
        snprintf(
            monitor->logs[MAX_LOG_ENTRIES - 1].message,
            sizeof(monitor->logs[MAX_LOG_ENTRIES - 1].message),
            "%s",
            message);
        monitor->logs[MAX_LOG_ENTRIES - 1].timestamp = 
            furi_get_tick() - monitor->start_time;
    }
    
    furi_mutex_release(monitor->mutex);
}

// Timer callback to check BLE status
static void update_timer_callback(void* context) {
    BleMonitor* monitor = context;
    
    // Check if BLE is active (connected or advertising)
    bool is_active = furi_hal_bt_is_active();
    
    static bool was_active = false;
    
    if(is_active && !was_active) {
        monitor->connection_count++;
        add_log(monitor, "BLE Activity Detected");
        notification_message(monitor->notifications, &sequence_blink_cyan_10);
    } else if(!is_active && was_active) {
        add_log(monitor, "BLE Activity Ended");
    }
    
    was_active = is_active;
    
    // Trigger view update
    view_commit_model(monitor->view, true);
}

// Draw callback
static void draw_callback(Canvas* canvas, void* context) {
    BleMonitor* monitor = context;
    
    furi_mutex_acquire(monitor->mutex, FuriWaitForever);
    
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "BLE Passive Monitor");
    
    // Draw status
    canvas_set_font(canvas, FontSecondary);
    char status_str[32];
    uint32_t uptime = (furi_get_tick() - monitor->start_time) / 1000;
    snprintf(status_str, sizeof(status_str), "Uptime: %lus | Events: %d", 
             uptime, monitor->connection_count);
    canvas_draw_str(canvas, 2, 20, status_str);
    
    // Draw monitoring status
    canvas_draw_str(canvas, 2, 30, monitor->monitoring ? "Status: Monitoring" : "Status: Paused");
    
    // Draw separator
    canvas_draw_line(canvas, 0, 32, 128, 32);
    
    // Draw log entries
    canvas_set_font(canvas, FontSecondary);
    uint8_t y = 42;
    uint8_t visible_lines = 3;
    
    if(monitor->log_count > 0) {
        uint8_t start_idx = monitor->log_count > visible_lines ? 
                           monitor->log_count - visible_lines - monitor->scroll_offset : 0;
        uint8_t end_idx = monitor->log_count;
        
        if(start_idx + visible_lines < end_idx) {
            end_idx = start_idx + visible_lines;
        }
        
        for(uint8_t i = start_idx; i < end_idx; i++) {
            char line[48];
            uint32_t sec = monitor->logs[i].timestamp / 1000;
            snprintf(line, sizeof(line), "[%02lu:%02lu] %s", 
                     sec / 60, sec % 60, monitor->logs[i].message);
            canvas_draw_str(canvas, 2, y, line);
            y += 10;
        }
    } else {
        canvas_draw_str(canvas, 2, y, "No events logged yet...");
    }
    
    // Draw controls
    elements_button_center(canvas, monitor->monitoring ? "Pause" : "Start");
    if(monitor->log_count > visible_lines) {
        elements_button_left(canvas, "Up");
        elements_button_right(canvas, "Down");
    }
    
    furi_mutex_release(monitor->mutex);
}

// Input callback
static bool input_callback(InputEvent* event, void* context) {
    BleMonitor* monitor = context;
    bool consumed = false;
    
    if(event->type == InputTypeShort) {
        consumed = true;
        
        switch(event->key) {
        case InputKeyOk:
            // Toggle monitoring
            furi_mutex_acquire(monitor->mutex, FuriWaitForever);
            monitor->monitoring = !monitor->monitoring;
            
            if(monitor->monitoring) {
                monitor->start_time = furi_get_tick();
                monitor->log_count = 0;
                monitor->scroll_offset = 0;
                monitor->connection_count = 0;
                add_log(monitor, "Monitoring started");
                furi_timer_start(monitor->update_timer, 500);
            } else {
                furi_timer_stop(monitor->update_timer);
                add_log(monitor, "Monitoring paused");
            }
            furi_mutex_release(monitor->mutex);
            break;
            
        case InputKeyUp:
            // Scroll up in logs
            furi_mutex_acquire(monitor->mutex, FuriWaitForever);
            if(monitor->scroll_offset < monitor->log_count - 3) {
                monitor->scroll_offset++;
            }
            furi_mutex_release(monitor->mutex);
            break;
            
        case InputKeyDown:
            // Scroll down in logs
            furi_mutex_acquire(monitor->mutex, FuriWaitForever);
            if(monitor->scroll_offset > 0) {
                monitor->scroll_offset--;
            }
            furi_mutex_release(monitor->mutex);
            break;
            
        case InputKeyLeft:
            // Scroll up (same as up key)
            furi_mutex_acquire(monitor->mutex, FuriWaitForever);
            if(monitor->scroll_offset < monitor->log_count - 3) {
                monitor->scroll_offset++;
            }
            furi_mutex_release(monitor->mutex);
            break;
            
        case InputKeyRight:
            // Scroll down (same as down key)
            furi_mutex_acquire(monitor->mutex, FuriWaitForever);
            if(monitor->scroll_offset > 0) {
                monitor->scroll_offset--;
            }
            furi_mutex_release(monitor->mutex);
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

// Allocate monitor
static BleMonitor* ble_monitor_alloc() {
    BleMonitor* monitor = malloc(sizeof(BleMonitor));
    
    monitor->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    monitor->log_count = 0;
    monitor->scroll_offset = 0;
    monitor->monitoring = false;
    monitor->start_time = furi_get_tick();
    monitor->connection_count = 0;
    
    monitor->notifications = furi_record_open(RECORD_NOTIFICATION);
    
    // Create update timer
    monitor->update_timer = furi_timer_alloc(update_timer_callback, FuriTimerTypePeriodic, monitor);
    
    // Create view
    monitor->view = view_alloc();
    view_set_context(monitor->view, monitor);
    view_set_draw_callback(monitor->view, draw_callback);
    view_set_input_callback(monitor->view, input_callback);
    
    // Create view dispatcher
    monitor->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_add_view(monitor->view_dispatcher, 0, monitor->view);
    view_dispatcher_switch_to_view(monitor->view_dispatcher, 0);
    
    return monitor;
}

// Free monitor
static void ble_monitor_free(BleMonitor* monitor) {
    // Stop monitoring if active
    if(monitor->monitoring) {
        furi_timer_stop(monitor->update_timer);
    }
    
    furi_timer_free(monitor->update_timer);
    view_dispatcher_remove_view(monitor->view_dispatcher, 0);
    view_dispatcher_free(monitor->view_dispatcher);
    view_free(monitor->view);
    furi_mutex_free(monitor->mutex);
    furi_record_close(RECORD_NOTIFICATION);
    free(monitor);
}

// Main entry point
int32_t ble_passive_monitor_app(void* p) {
    UNUSED(p);
    
    BleMonitor* monitor = ble_monitor_alloc();
    
    Gui* gui = furi_record_open(RECORD_GUI);
    view_dispatcher_attach_to_gui(
        monitor->view_dispatcher, gui, ViewDispatcherTypeFullscreen);
    
    // Add initial log entry
    add_log(monitor, "App initialized");
    add_log(monitor, "Press OK to start");
    
    view_dispatcher_run(monitor->view_dispatcher);
    
    furi_record_close(RECORD_GUI);
    ble_monitor_free(monitor);
    
    return 0;
}
