#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdlib.h>
#include <storage/storage.h>
#include <storage/storage_sd_api.h>
#include <gui/elements.h>

#include <math.h>

typedef enum {
    EventTypeTick,
    EventTypeKey,
} EventType;

typedef struct {
    EventType type;
    InputEvent input;
} PluginEvent;

typedef struct {
    int xArrayIndex;
    //saves the y position you were on when switching
    int selectionPositionYValue[2];
    //all values
    FuriString* filledTextSelection[2][2];
    float percentUsed;
} PluginState;

static void
    CUSTOM_drawTriangle(Canvas* const canvas, int length, int startX, int startY, char direction) {
    int xStartOffset = startX, yStartOffset;
    yStartOffset = startY;

    switch(direction) {
    case 'R':
        //arrow pointed right
        for(int sideYLength = length, xOffset = 0; sideYLength >= 0; sideYLength--, xOffset++)
            canvas_draw_line(
                canvas,
                xStartOffset + xOffset,
                yStartOffset + sideYLength,
                xStartOffset + xOffset,
                yStartOffset - sideYLength);
        break;

    case 'L':
        //arrow pointed left
        for(int sideYLength = length, xOffset = 0; sideYLength >= 0; sideYLength--, xOffset--)
            canvas_draw_line(
                canvas,
                xStartOffset + xOffset,
                yStartOffset + sideYLength,
                xStartOffset + xOffset,
                yStartOffset - sideYLength);
        break;

    case 'U':
        //arrow pointed up
        yStartOffset = yStartOffset + (length / 2);
        for(int sideXLength = length, yOffset = 0; sideXLength >= 0; sideXLength--, yOffset--)

            canvas_draw_line(
                canvas,
                xStartOffset - sideXLength,
                yStartOffset + yOffset,
                xStartOffset + sideXLength,
                yStartOffset + yOffset);
        break;

    case 'D':
        //arrow pointed down
        yStartOffset = yStartOffset + (length / 2);
        for(int sideXLength = length, yOffset = 0; sideXLength >= 0; sideXLength--, yOffset++)

            canvas_draw_line(
                canvas,
                xStartOffset - sideXLength,
                yStartOffset + yOffset,
                xStartOffset + sideXLength,
                yStartOffset + yOffset);
        break;

    default:
        //invalid direction
        return;
    }
}

static void render_callback(Canvas* const canvas, void* ctx) {
    const PluginState* plugin_state = ctx;

    // border around the edge of the screen
    canvas_draw_frame(canvas, 0, 0, 128, 64);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 14, AlignCenter, AlignBottom, "SD STORAGE");

    //draws the text centered on the screen
    canvas_set_font(canvas, FontSecondary);

    canvas_draw_str_aligned(
        canvas,
        (uint8_t)64,
        (uint8_t)32,
        AlignCenter,
        AlignBottom,
        furi_string_get_cstr(
            plugin_state->filledTextSelection
                [plugin_state->xArrayIndex]
                [plugin_state->selectionPositionYValue[plugin_state->xArrayIndex]]));

    elements_progress_bar(canvas, 24, 35, (uint8_t)80, ((float)plugin_state->percentUsed));

    CUSTOM_drawTriangle(canvas, 2, 116, 50, 'U');
    CUSTOM_drawTriangle(canvas, 2, 116, 56, 'D');
    CUSTOM_drawTriangle(canvas, 2, 112, 54, 'L');
    CUSTOM_drawTriangle(canvas, 2, 120, 54, 'R');
}

static void input_callback(InputEvent* input_event, FuriMessageQueue* event_queue) {
    furi_assert(event_queue);

    PluginEvent event = {.type = EventTypeKey, .input = *input_event};
    furi_message_queue_put(event_queue, &event, FuriWaitForever);
}

//initializes the index of the array value to 0
static void storage_state_init(PluginState* const plugin_state) {
    //initializes the array
    Storage* storage = furi_record_open(RECORD_STORAGE);
    uint64_t total_space;
    uint64_t free_space;
    FS_Error error =
        storage_common_fs_info(storage, STORAGE_EXT_PATH_PREFIX, &total_space, &free_space);

    if(error != FSE_OK) {
        return;
    } else {
        plugin_state->filledTextSelection[0][0] = furi_string_alloc();
        plugin_state->filledTextSelection[0][1] = furi_string_alloc();
        plugin_state->filledTextSelection[1][0] = furi_string_alloc();
        plugin_state->filledTextSelection[1][1] = furi_string_alloc();
        float percentFree;
        float percentUsedTemp;

        //creates FuriString with storage #'s

        furi_string_printf(
            plugin_state->filledTextSelection[0][0],
            "%luGB free / %luGB total",
            (uint32_t)(ceil(free_space / 1000000000) + 1),
            (uint32_t)(total_space / 1000000000));

        furi_string_printf(
            plugin_state->filledTextSelection[0][1],
            "%luGB used / %luGB total",
            (uint32_t)(ceil((total_space - free_space) / 1000000000)),
            (uint32_t)(total_space / 1000000000));

        //  % of storage free (saves the variable to be used for percentage bar)
        percentFree = (((float)free_space / total_space));
        furi_string_printf(
            plugin_state->filledTextSelection[1][0], "%i%% free", (int)floor(percentFree * 100));

        percentUsedTemp = (((float)(total_space - free_space) / total_space));
        furi_string_printf(
            plugin_state->filledTextSelection[1][1],
            "%i%% used",
            (int)ceil(percentUsedTemp * 100));

        plugin_state->xArrayIndex = 0;
        plugin_state->selectionPositionYValue[0] = 1;
        plugin_state->selectionPositionYValue[1] = 1;

        plugin_state->percentUsed = percentUsedTemp;
    }

    furi_record_close(RECORD_STORAGE);
}

//changes the index of the array value to go between index 0 and 1
static void increment_next_display_x(PluginState* const plugin_state) {
    plugin_state->xArrayIndex = (plugin_state->xArrayIndex + 1) % 2;
}

static void increment_next_display_y(PluginState* const plugin_state) {
    plugin_state->selectionPositionYValue[plugin_state->xArrayIndex] =
        (plugin_state->selectionPositionYValue[plugin_state->xArrayIndex] + 1) % 2;
}

//main app functions
int32_t clean_storage_viewer_app() {
    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(PluginEvent));
    PluginState plugin_state;

    //initializes the arrayIndex variable and fill array with text values
    storage_state_init(&plugin_state);

    // Set system callbacks
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, render_callback, &plugin_state);
    view_port_input_callback_set(view_port, input_callback, event_queue);

    // Open GUI and register view_port
    Gui* gui = furi_record_open("gui");
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    PluginEvent event;
    for(bool processing = true; processing;) {
        FuriStatus event_status = furi_message_queue_get(event_queue, &event, 100);

        if(event_status == FuriStatusOk) {
            // press events
            if(event.type == EventTypeKey) {
                if(event.input.type == InputTypePress) {
                    switch(event.input.key) {
                    case InputKeyRight:
                        increment_next_display_x(&plugin_state);
                        break;
                    case InputKeyLeft:
                        increment_next_display_x(&plugin_state);
                        break;
                    case InputKeyUp:
                        increment_next_display_y(&plugin_state);
                        break;
                    case InputKeyDown:
                        increment_next_display_y(&plugin_state);
                        break;
                    case InputKeyOk:
                        break;
                    case InputKeyBack:
                        processing = false;
                        break;
                    default:
                        break;
                    }
                }
            }
        } else {
            FURI_LOG_D("CLEAN_STORAGE_APP", "FuriMessageQueue: event timeout");
            // event timeout
        }

        view_port_update(view_port);
    }

    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    furi_record_close("gui");
    view_port_free(view_port);
    furi_message_queue_free(event_queue);
    furi_string_free(plugin_state.filledTextSelection[0][0]);
    furi_string_free(plugin_state.filledTextSelection[0][1]);
    furi_string_free(plugin_state.filledTextSelection[1][0]);
    furi_string_free(plugin_state.filledTextSelection[1][1]);

    return 0;
}