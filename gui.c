#include <stdio.h>
#include <unistd.h> // for sleep
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h> // for free and calloc
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <termios.h>

/*
* This table holds binary to character mappings for display, it uses a unicode character set to represent pixels.
* It is encoded as such, the grid is 2x3 so it is 6 bits TL,TR ML,MR BL,BR in that order, so the following shape:
* 11
* 10
* 11
* which looks like a C is encoded as just the flattened version of that shape: 11 10 11 or 59 which the 59th element is: 𜺇
*/
const char* char_map[64] = {
    " ","𜹰","𜹠","𜺀", "𜹘","𜹸","𜹨","𜺈", "𜹔","𜹴","𜹤","𜺄", "𜹜","𜹼","𜹬","𜺌", "𜹒","𜹲","𜹢","𜺂", "𜹚","𜹺","𜹪","𜺊", "𜹖","𜹶","𜹦","𜺆", "𜹞","𜹾","𜹮","𜺎",
    "𜹑","𜹱","𜹡","𜺁", "𜹙","𜹹","𜹩","𜺉", "𜹕","𜹵","𜹥","𜺅", "𜹝","𜹽","𜹭","𜺍", "𜹓","𜹳","𜹣","𜺃", "𜹛","𜹻","𜹫","𜺋", "𜹗","𜹷","𜹧","𜺇", "𜹟","𜹿","𜹯","𜺏"
};

/**
* @brief Converts a 6 bool binary array into a uint8_t
* @param arr a 6 bool array used to represent a 2x3 binary shape
* @return The uint8_t representation of the bool input array
*/
uint8_t boolsToInt(bool arr[6]) {
    uint8_t result = 0;
    for (int i = 0; i < 6; i++) {
        result = (result << 1) | (uint8_t) arr[i];
    }
    return result;
}

/**
* @brief Converts a 6 boolean binary array into a string from a table
* @param arr a 6 integer array used to represent a 2x3 binary shape
* @return a string containing a utf8 character
*/
const char *boolsToString(bool arr[6]) {
    return char_map[boolsToInt(arr)];
}

/*
* Screen type for different display types
*/
typedef struct {
    uint8_t status;
    uint8_t width;
    uint8_t height;
    uint8_t flags;
    bool *data;
    uint8_t *render;
} Screen;

/**
* The status definitions below and their aligned BIT counterparts
*/
#define SCREEN_READY   0b00000001
#define SCREEN_SUCCESS 0b00000010
#define SCREEN_ERROR   0b00000100
#define SCREEN_READY_BIT   (SCREEN_READY << 8)
#define SCREEN_SUCCESS_BIT (SCREEN_SUCCESS << 8)
#define SCREEN_ERROR_BIT   (SCREEN_ERROR << 8)

/**
* @brief combines the status and data return into a single value
* @param status the current return code of the function
* @param data the byte being returned from the function
* @return The combined status and data
*/
uint16_t joinReturn(uint8_t status, uint8_t data) {
    return ((uint16_t)status << 8) | data;
}
/**
* @brief returns 0 if the status is successful and the error code if not
* @param value the combined status and data return value
* @return the error if it exists or 0 if not
*/
uint8_t returnError(uint16_t value) {
    return ((value & 0xFF00) == SCREEN_SUCCESS_BIT)?0:(value>>8);
}
/**
* @brief returns the data value from the combined uint16_t
* @param value the combined status and data return value
* @return the data half of the return value
*/
uint8_t returnData(uint16_t value) {
    return (uint8_t) (value & 0x00FF);
}

/**
* @brief initializes and allocates a Screen struct
* @param scr a pointer to the current screen
* @param flags the screen option flag bits in a uint8_t
* @param width the width of the screen
* @param height the height of the screen
* @return the allocation status
*/
uint16_t initScreen(Screen *scr, uint8_t flags, uint8_t width, uint8_t height) {
    if (!scr) {
        fprintf(stderr, "[E] Screen pointer invalid!\n");
        return 0; // input pointer is invalid
    }
    scr->status = SCREEN_READY;
    scr->flags = flags;
    scr->width = width;
    scr->height = height;
    scr->data = (bool*) calloc( (width * height), sizeof(bool));
    scr->render = (uint8_t*) calloc( ((width/2)+1) * ((height/3)+1), sizeof(uint8_t));

    uint8_t ret = SCREEN_SUCCESS;
    if (!scr->data || !scr->render) {
        ret = SCREEN_ERROR;
        fprintf(stderr, "Error allocating memory during initialization\n");
    }
    return joinReturn(ret, 0x00); // is screen data successfully allocated, no data
}
/**
* @brief resizes a screen struct
* @param scr a pointer to the current screen
* @param width the width of the screen
* @param height the height of the screen
* @return the resize allocation status
*/
uint16_t resizeScreen(Screen *scr, uint8_t width, uint8_t height) {
    if (!scr) {
        fprintf(stderr, "[E] Screen pointer invalid!\n");
        return joinReturn(SCREEN_ERROR, 0x00);
    }
    if (scr->data) {
        free(scr->data);
        free(scr->render);
        scr->data = NULL;
    }

    scr->width = width;
    scr->height = height;
    scr->data = (bool*) calloc( (width * height), sizeof(bool));
    scr->render = (uint8_t*) calloc( ((width/2)+1) * ((height/3)+1), sizeof(uint8_t));

    uint8_t ret = SCREEN_SUCCESS;
    if (!scr->data || !scr->render) {
        ret = SCREEN_ERROR;
        fprintf(stderr, "Error allocating memory during resize\n");
    }
    return joinReturn(ret, 0x00); // is screen data successfully allocated, no data
}

/**
* @brief gets the data of a pixel at the X and Y position
* @param scr a pointer to the current screen
* @param x the x position of the desired pixel
* @param y the y position of the desired pixel
* @return the pixel value
*/
bool getScreenPixel(Screen *scr, uint8_t x, uint8_t y) {
    if (!scr) {
        fprintf(stderr, "[E] Screen pointer invalid!\n");
        return 0;
    }
    if (scr->status != SCREEN_READY) {
        initScreen(scr, 0b00000000, 20,20);
        fprintf(stderr, "[E] Screen not initialized!\n");
        return 0;
    }
    if (x >= scr->width || y >= scr->height) {
        return 0;
    }
    return scr->data[(y*scr->width)+x];
}
/**
* @brief sets the data of a pixel at the X and Y position
* @param scr a pointer to the current screen
* @param x the x position of the desired pixel
* @param y the y position of the desired pixel
* @return the status
*/
uint16_t setScreenPixel(Screen *scr, uint8_t x, uint8_t y, bool value) {
    if (!scr) {
        fprintf(stderr, "[E] Screen pointer invalid!\n");
        return joinReturn(SCREEN_ERROR, 0x00);
    }
    if (scr->status != SCREEN_READY) {
        initScreen(scr, 0b00000000, 20,20);
        fprintf(stderr, "[E] Screen not initialized! setPixel\n");
        return joinReturn(SCREEN_ERROR, 0x00);
    }
    if (x >= scr->width || y >= scr->height) {
        return 0;
    }
    scr->data[(y*scr->width)+x] = value;
    return joinReturn(SCREEN_SUCCESS, 0x00);
}

/**
* @brief renders the pixels to a character grid 1/6 the size
* @param scr a pointer to the current screen
* @return a pointer to the array
*/
void renderScreen(Screen *scr) {
    uint8_t width = (scr->width/2)+1;
    uint8_t height = (scr->height/3)+1;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint16_t index = (y*width)+x;
            bool inp[6];
            inp[0] = getScreenPixel(scr,(x*2)+0,(y*3)+0);
            inp[1] = getScreenPixel(scr,(x*2)+1,(y*3)+0);
            inp[2] = getScreenPixel(scr,(x*2)+0,(y*3)+1);
            inp[3] = getScreenPixel(scr,(x*2)+1,(y*3)+1);
            inp[4] = getScreenPixel(scr,(x*2)+0,(y*3)+2);
            inp[5] = getScreenPixel(scr,(x*2)+1,(y*3)+2);
            scr->render[index] = boolsToInt(inp);
        }
    }
}

// TODO: document terminal IO functions

// terminal control functions
struct termios orig_termios;

// forward declarations
void set_nonblocking(int enabled);
void enable_raw_mode();
void disable_raw_mode();

void enter_term() {
    printf("\x1b[?1049h"); // Enable alternate screen buffer
    fflush(stdout);
}
void exit_term() {
    printf("\x1b[?1049l"); // Return to normal screen buffer
    fflush(stdout);
}

void init_term() {
    // load temporary stdout buffer
    enter_term();
    enable_raw_mode();
    printf("\033[?25l"); // hide cursor
}
void restore_term() {
    // restore to normal keyboard input
    disable_raw_mode();
    exit_term();
    printf("\033[?25h"); // show cursor
}

void handle_quit(int sig) {
    restore_term();
    printf("Exiting...\n", sig);
    exit(1);
}

// raw input
void enable_raw_mode() {
    struct termios new_termios;

    // handle exit for ctrl-c
    // Set up signal handlers
    signal(SIGINT, handle_quit);   // Ctrl+C
    signal(SIGTERM, handle_quit);  // Termination

    // Save current terminal settings
    tcgetattr(STDIN_FILENO, &orig_termios);

    // Copy to new settings
    new_termios = orig_termios;

    // Disable canonical mode and echo
    new_termios.c_lflag &= ~(ICANON | ECHO);

    // Set minimum characters to 0 (don't wait for characters)
    new_termios.c_cc[VMIN] = 0;
    new_termios.c_cc[VTIME] = 0;

    // Apply new settings
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

    // Make stdin non-blocking
    int flags = fcntl(STDIN_FILENO, F_GETFL);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
}
void disable_raw_mode() {
    // Restore original terminal settings
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);

    // Remove non-blocking flag
    int flags = fcntl(STDIN_FILENO, F_GETFL);
    fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);
}

void printXY(int x, int y, const char *str) {
   printf("\033[%d;%dH%s", x, y, str);
}

/**
* @brief renders and prints out the current screen
* @param scr a pointer to the current screen
*/
void printScreen(Screen *scr) {
    uint8_t width = (scr->width/2)+1;
    uint8_t height = (scr->height/3)+1;
    for (int y = 0; y < height; y++) {
        char buf[512+1] = "";
        for (int x = 0; x < width; x++) {
            uint16_t index = (y*width)+x;
            strcat(buf, char_map[scr->render[index]]);
        }
        printXY(y+2, 2, buf);
    }
    printf("\n");
    fflush(stdout); // push changes to terminal
}

/**
* @brief frees the memory of the current screen
* @param scr a pointer to the current screen
*/
void destroyScreen(Screen *scr) {
    if (scr->data) {
        free(scr->data);
        scr->data = NULL;
    }
    if (scr->render) {
        free(scr->render);
        scr->render = NULL;
    }
}

char getch() {
    char ch;
    if (read(STDIN_FILENO, &ch, 1) > 0) {
        return ch;
    }
    return 0;  // No character available
}


int main() {
    uint16_t ret;
    bool running = true;

    // load temporary stdout buffer
    init_term();

    // current screen instance
    Screen scr;

    if (returnError(initScreen(&scr, 0x0, 250, 100))) {
        exit(1);
    }

    printf("Resolution: %ix%i\n", scr.width/2,scr.height/3);

    for (int i = 0; i < 250; i++) {
        for (int j = 0; j < 100; j++) {
            setScreenPixel(&scr, i,j, (bool) (rand() % 2)-1);
        }
    }

    //while (running) {
    //    char key = getch();
    //    if (key != 0) {
    //        printf("key: %c\n", key);
    //    }
        // render
        renderScreen(&scr);
        printScreen(&scr);
    fflush(stdout);
        usleep(1000 * 1000); // Sleep 10ms
    //}

    // clean up
    destroyScreen(&scr);

    // return to original stdout
    restore_term();

    return 0;
}
