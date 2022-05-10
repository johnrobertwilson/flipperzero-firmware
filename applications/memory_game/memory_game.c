#include <furi.h>
#include <furi_hal.h>

#include <gui/gui.h>
#include <input/input.h>

#include <time.h>

#include <notification/notification_messages.h>

const NotificationSequence sequence_set_yellow_255 = {
    &message_red_255,
    &message_green_255,
    &message_do_not_reset,
    NULL,
};

const NotificationSequence sequence_set_cyan_255 = {
    &message_blue_255,
    &message_green_255,
    &message_do_not_reset,
    NULL,
};

const NotificationSequence sequence_set_magenta_255 = {
    &message_blue_255,
    &message_red_255,
    &message_do_not_reset,
    NULL,
};

const NotificationSequence sequence_set_white_255 = {
    &message_red_255,
    &message_green_255,
    &message_blue_255,
    &message_do_not_reset,
    NULL,
};

typedef enum {
    GameStateStartScreen,
    GameStatePlaying,
    GameStateComputerPlaying,
    GameStateHumanPlaying,
    GameStateGameOver,
} GameState;

static const char *GameStateDebugString[] = {
    "GameStateStartScreen",
    "GameStatePlaying",
    "GameStateComputerPlaying",
    "GameStateHumanPlaying",
    "GameStateGameOver",
};

typedef enum {
    Up,
    Right,
    Down,
    Left,
    Center,
    None,
} MemoryGameInput;

typedef enum {
    EventTypeTick,
    EventTypeKey,
} EventType;

typedef struct {
    EventType type;
    InputEvent input;
} MemoryGameEvent;

#define MAX_MEMORY_PATTERN_LEN 200

typedef struct {
    uint16_t score;
    uint16_t round;
    uint16_t pattern[MAX_MEMORY_PATTERN_LEN];
    MemoryGameInput current_input;
    GameState state;
    bool press;
    FuriThread* computer_player_thread;
    NotificationApp* notification;
    ViewPort* view_port;
} MemoryGameState;

void effects_on( NotificationApp* const notifications, char color[12], int speaker_note) {
    FURI_LOG_I("MemoryGame", "EFFECTS ON");
    // Switch can't use char
    if ( strcmp(color,"blue") == 0 ) {
        notification_message(notifications, &sequence_set_blue_255);
    }
    if ( strcmp(color, "cyan" ) == 0 ) {
        notification_message(notifications, &sequence_set_cyan_255);
    }
    if ( strcmp(color, "yellow" ) == 0 ) {
        notification_message(notifications, &sequence_set_yellow_255);
    }
    if ( strcmp(color, "magenta" ) == 0 ) {
        notification_message(notifications, &sequence_set_magenta_255);
    }
    if ( strcmp(color, "white" ) == 0 ) {
        notification_message(notifications, &sequence_set_white_255);
    }
    furi_hal_speaker_start(speaker_note, 1.0);
}

void effects_off(NotificationApp* const notifications) {
    FURI_LOG_I("MemoryGame", "EFFECTS OFF");
    notification_message(notifications, &sequence_reset_rgb);
    furi_hal_speaker_stop();
    FURI_LOG_I("MemoryGame", "EFFECTS OFF FINISHED");
}

void press_down(MemoryGameState* const memory_game_state, char notification_color[], int speaker_note) {
    FURI_LOG_I("MemoryGame", "PRESS DOWN");
    effects_on(memory_game_state->notification, notification_color, speaker_note);
    memory_game_state->press = true;
}

void press_up(MemoryGameState* const memory_game_state) {
    FURI_LOG_I("MemoryGame", "PRESS UP");
    effects_off(memory_game_state->notification);
    memory_game_state->press = false;
}

static void memory_game_render_callback(Canvas* const canvas, void* ctx) {
    const MemoryGameState* memory_game_state = acquire_mutex((ValueMutex*)ctx, 25);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);

    canvas_draw_str(canvas, 0, 10, "Memory Game");
    canvas_set_font(canvas, FontKeyboard);
    canvas_draw_str(canvas, 0, 30, GameStateDebugString[memory_game_state->state]);
    canvas_draw_str(canvas, 0, 40, "Round");
    char round_string[6];
    sprintf(round_string, "%d", memory_game_state->round);
    canvas_draw_str(canvas, 40, 40, round_string);
    
    if ( memory_game_state->state == GameStateStartScreen ) {
        canvas_draw_str(canvas, 0, 60, "Press (o) to start");
    }
    canvas_draw_circle(canvas, 100, 26, 25); //Large Main Circle
    canvas_draw_circle(canvas, 118, 26, 5); // Right
    canvas_draw_circle(canvas, 82, 26, 5);  // Left
    canvas_draw_circle(canvas, 100, 8, 5);  // Up
    canvas_draw_circle(canvas, 100, 44, 5); // Down
    canvas_draw_circle(canvas, 100, 26, 5); // Center

    switch(memory_game_state->current_input) {
        case Right:
            if ( ! memory_game_state->press ) {
                canvas_draw_circle(canvas, 118, 26, 5);
            }
            else {
                canvas_draw_disc(canvas, 118, 26, 5);
            }
            break;
        case Left:
            if ( ! memory_game_state->press ) {
                canvas_draw_circle(canvas, 82, 26, 5);
            }
            else {
                canvas_draw_disc(canvas, 82, 26, 5);
            }
            break;
        case Up:
            if ( ! memory_game_state->press ) {
                canvas_draw_circle(canvas, 100, 8, 5);
            }
            else {
                canvas_draw_disc(canvas, 100, 8, 5);
            }
            break;
        case Down:
            if ( ! memory_game_state->press ) {
                canvas_draw_circle(canvas, 100, 44, 5);
            }
            else {
                canvas_draw_disc(canvas, 100, 44, 5);
            }
            break;
        case Center:
            if ( ! memory_game_state->press ) {
                canvas_draw_circle(canvas, 100, 26, 5);
            }
            else {
                canvas_draw_disc(canvas, 100, 26, 5);
            }
            break;
        case None:
            FURI_LOG_I("MemoryGame", "RENDER NONE");
            break;
    }

    release_mutex((ValueMutex*)ctx, memory_game_state);
}

static void memory_game_input_callback(InputEvent* input_event, osMessageQueueId_t event_queue) {
    furi_assert(event_queue);
    MemoryGameEvent event = {.type = EventTypeKey, .input = *input_event};
    osMessageQueuePut(event_queue, &event, 0, osWaitForever);
}

static int32_t memory_game_computer_player_thread(void* p) {
    FURI_LOG_I("MemoryGame", "STARTING MEMORY GAME COMPUTER PLAYER THREAD");
    MemoryGameState* memory_game_state = (MemoryGameState*)p;
    while(1) {
        // FURI_LOG_I("MemoryGame", "THREAD FUNCTION: RUNNING...");
        if ( memory_game_state->state == GameStateComputerPlaying ) {
            FURI_LOG_I("MemoryGame", "THREAD FUNCTION: Computer Playing");
            char notification_color[12] = "";
            int speaker_note = 0;
            //Process the pattern and press buttons
            for ( int i=0; i < memory_game_state->round; i++ ) {
                FURI_LOG_I("MemoryGame", "Computer Plays: Iterating through pattern at index %d", i);
                FURI_LOG_I("MemoryGame", "Pattern Value: %d", memory_game_state->pattern[i]);
                MemoryGameInput memory_game_input = None;
                switch ( memory_game_state->pattern[i] ) {
                    case 1:
                        memory_game_input = Up;
                        strcat(notification_color, "blue");
                        speaker_note = 659;
                    break;

                    case 2:
                        memory_game_input = Right;
                        strcat(notification_color, "cyan");
                        speaker_note = 1046;
                    break;

                    case 3:
                        memory_game_input = Down;
                        strcat(notification_color, "yellow");
                        speaker_note = 880;
                    break;

                    case 4:
                        memory_game_input = Left;
                        strcat(notification_color, "magenta");
                        speaker_note = 1175;
                    break;

                    case 5:
                        memory_game_input = Center;
                        strcat(notification_color, "white");
                        speaker_note = 1319;
                    break;
                }
                press_down(memory_game_state, notification_color, speaker_note);
                memory_game_state->current_input = memory_game_input;
                view_port_update(memory_game_state->view_port);
                furi_hal_delay_ms(500);
                press_up(memory_game_state);
                memory_game_state->current_input = None;
                view_port_update(memory_game_state->view_port);
                FURI_LOG_I("MemoryGame", "PRESS UP FINISHED");
            }
            FURI_LOG_I("MemoryGame", "THREAD FUNCTION: Handing focus to Human");
            memory_game_state->state = GameStateHumanPlaying;
        }
    }

    return 0;
}

static void memory_game_init_game(MemoryGameState* const memory_game_state) {
    memory_game_state->score = 0;
    memory_game_state->round = 0;
    memory_game_state->current_input = None;
    memory_game_state->press = false;
    memory_game_state->state = GameStateStartScreen;
    memory_game_state->notification = furi_record_open("notification");
    memory_game_state->view_port = view_port_alloc();
    memory_game_state->computer_player_thread = furi_thread_alloc();
    furi_thread_set_name(memory_game_state->computer_player_thread, "MemoryGameCompPlayerThread");
    furi_thread_set_stack_size(memory_game_state->computer_player_thread, 2048);
    furi_thread_set_context(memory_game_state->computer_player_thread, memory_game_state);
    furi_thread_set_callback(memory_game_state->computer_player_thread, memory_game_computer_player_thread);
    furi_thread_start(memory_game_state->computer_player_thread);

    // Build the patter for the entire game, each value between 1 and 5
    srand(time(NULL));
    int random_key;

    for ( int i = 0; i < MAX_MEMORY_PATTERN_LEN; i++ ) {
        random_key = (rand() % 5) + 1;
        memory_game_state->pattern[i] = random_key;
    }
}

int32_t memory_game_app(void* p) {
    FURI_LOG_I("MemoryGame", "Memory Game Start!");
    osMessageQueueId_t event_queue = osMessageQueueNew(8, sizeof(MemoryGameEvent), NULL);

    MemoryGameState* memory_game_state = malloc(sizeof(MemoryGameState));
    memory_game_init_game(memory_game_state);

    ValueMutex state_mutex;
    if(!init_mutex(&state_mutex, memory_game_state, sizeof(MemoryGameState))) {
        FURI_LOG_E("MemoryGame", "cannot create mutex\r\n");
        free(memory_game_state);
        return 255;
    }

    view_port_draw_callback_set(memory_game_state->view_port, memory_game_render_callback, &state_mutex);
    view_port_input_callback_set(memory_game_state->view_port, memory_game_input_callback, event_queue);

    Gui* gui = furi_record_open("gui");
    gui_add_view_port(gui, memory_game_state->view_port, GuiLayerFullscreen);

    MemoryGameEvent event;
    FURI_LOG_I("MemoryGame", "Beginning Processing....");
    for(bool processing = true; processing;) {
        osStatus_t event_status = osMessageQueueGet(event_queue, &event, NULL, osWaitForever);
        MemoryGameState* memory_game_state = (MemoryGameState*)acquire_mutex_block(&state_mutex);
        if(event_status == osOK) {
            FURI_LOG_I("MemoryGame", "event_status is osOK");
            // GAME STATE: COMPUTER PLAYING
            if ( memory_game_state->state == GameStateComputerPlaying ) {
                // Play as the computer
                FURI_LOG_I("MemoryGame", "APP FUNCTION: Computer is Playing");
                if ( event.input.key == InputKeyBack ) {
                    processing = false;
                }
            }
            // GAME STATE: START SCREEN
            if ( memory_game_state->state == GameStateStartScreen ) {
                FURI_LOG_I("MemoryGame", "APP FUNCTION: Start Screen");
                if ( event.input.key == InputKeyOk ) {
                    memory_game_state->state = GameStateComputerPlaying;
                    // memory_game_state->state = GameStateHumanPlaying;
                    memory_game_state->round = 10;
                }

                if ( event.input.key == InputKeyBack ) {
                    FURI_LOG_I("MemoryGame", "Attempting to end app..");
                    processing = false;
                }
            }
            // GAME STATE: HUMAN PLAYING
            if ( memory_game_state->state == GameStateHumanPlaying) {
                FURI_LOG_I("MemoryGame", "APP FUNCTION: Human is playing");
                if (event.type == EventTypeKey) {
                    
                    memory_game_state->press = ( event.input.type == InputTypePress || event.input.type == InputTypeRepeat || event.input.type == InputTypeShort || event.input.type == InputTypeLong) ? true : false;
                    if(event.input.type == InputTypePress || event.input.type == InputTypeRelease) {
                        switch(event.input.key) {
                        case InputKeyUp:
                            memory_game_state->current_input = Up;
                            if ( event.input.type == InputTypePress ) {
                                press_down(memory_game_state, "blue", 659);
                            }
                            if ( event.input.type == InputTypeRelease ) {
                                press_up(memory_game_state);
                            }
                            break;
                        case InputKeyDown:
                            memory_game_state->current_input = Down;
                            if ( event.input.type == InputTypePress ) {
                                press_down(memory_game_state, "yellow", 880);
                            }
                            if ( event.input.type == InputTypeRelease ) {
                                press_up(memory_game_state);
                            }
                            break;
                        case InputKeyRight:
                            memory_game_state->current_input = Right;
                            if ( event.input.type == InputTypePress ) {
                                press_down(memory_game_state, "cyan", 1046);
                            }
                            if ( event.input.type == InputTypeRelease ) {
                                press_up(memory_game_state);
                            }
                            break;
                        case InputKeyLeft:
                            memory_game_state->current_input = Left;
                            if ( event.input.type == InputTypePress ) {
                                press_down(memory_game_state, "magenta", 1175);
                            }
                            if ( event.input.type == InputTypeRelease ) {
                                press_up(memory_game_state);
                            }
                            break;
                        case InputKeyOk:
                            memory_game_state->current_input = Center;
                            if ( event.input.type == InputTypePress ) {
                                press_down(memory_game_state, "white", 1319);
                            }
                            if ( event.input.type == InputTypeRelease ) {
                                press_up(memory_game_state);
                            }
                            break;
                        case InputKeyBack:
                            processing = false;
                            break;
                        }
                    }
                }
            }
        }
        else {
            FURI_LOG_I("MemoryGame", "Event status is NOT osOK");
            processing = false;
        }
        
        if ( processing ) {
            view_port_update(memory_game_state->view_port);
            release_mutex(&state_mutex, memory_game_state);
        }
    }

    FURI_LOG_I("MemoryGame", "ENDING MEMORY GAME APP");
    FURI_LOG_I("MemoryGame", "closing computer player thread");
    // osThreadTerminate(memory_game_state->computer_player_thread);
    // furi_thread_join(memory_game_state->computer_player_thread);
    furi_thread_free(memory_game_state->computer_player_thread);
    FURI_LOG_I("MemoryGame", "stopping speaker");
    furi_hal_speaker_stop();
    FURI_LOG_I("MemoryGame", "Disabling ViewPort");
    view_port_enabled_set(memory_game_state->view_port, false);
    FURI_LOG_I("MemoryGame", "Remove viewport from GUI");
    gui_remove_view_port(gui, memory_game_state->view_port);
    FURI_LOG_I("MemoryGame", "Closing GUI and Notification");
    furi_record_close("gui");
    furi_record_close("notifications");
    FURI_LOG_I("MemoryGame", "Freeing view port");
    view_port_free(memory_game_state->view_port);
    osMessageQueueDelete(event_queue);
    delete_mutex(&state_mutex);
    free(memory_game_state);

    FURI_LOG_I("MemoryGame", "Finished releasing resources");

    return 0;
}