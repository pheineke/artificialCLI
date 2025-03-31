#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <sys/ioctl.h> // For terminal size
#include <wchar.h>     // For wide character support if needed (box chars)
#include <locale.h>    // For wide character support

// ANSI color codes
#define ANSI_RESET   "\x1b[0m"
#define ANSI_BOLD    "\x1b[1m"
#define ANSI_DIM     "\x1b[2m" // Often used for italics in terminals
#define ANSI_CYAN    "\x1b[36m"
#define ANSI_GREEN   "\x1b[32m"
#define ANSI_YELLOW  "\x1b[33m"
#define ANSI_RED     "\x1b[31m"
#define ANSI_BLUE    "\x1b[34m"
#define ANSI_MAGENTA "\x1b[35m"

// Box drawing characters (using UTF-8 directly)
#define BOX_TOP_LEFT     "┌"
#define BOX_TOP_RIGHT    "┐"
#define BOX_BOTTOM_LEFT  "└"
#define BOX_BOTTOM_RIGHT "┘"
#define BOX_HORIZONTAL   "─"
#define BOX_VERTICAL     "│"

// --- Function Prototypes ---
int is_note_block(const char *line);
int is_warning_block(const char *line);
int is_code_block_marker(const char *line); // Also declare this one for print_formatted_text


// --- Helper Functions ---

// Get terminal width
int get_terminal_width() {
    struct winsize w;
    int default_width = 80;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1 || w.ws_col == 0) {
        char *cols = getenv("COLUMNS");
        if (cols) {
            int width = atoi(cols);
            if (width > 0) return width;
        }
        return default_width;
    }
    // Reduce width slightly to prevent wrapping issues at edge cases
    return (w.ws_col > 2) ? w.ws_col -1 : w.ws_col;
}

// Calculate visible length of a string (stripping ANSI codes)
size_t visible_strlen(const char *str) {
    size_t len = 0;
    int in_escape = 0;
    for (size_t i = 0; str[i] != '\0'; ++i) {
        if (str[i] == '\x1b') {
            in_escape = 1;
        } else if (in_escape && str[i] == 'm') {
            in_escape = 0;
        } else if (!in_escape) {
            // Basic handling for multi-byte UTF-8 chars (crude approximation)
            if ((unsigned char)str[i] >= 0x80) {
                 if (i == 0 || (unsigned char)str[i-1] < 0x80 || ((unsigned char)str[i-1] >= 0xc0)) {
                      len++;
                 }
            } else {
                len++;
            }
        }
    }
    return len;
}


// Print a string N times
void print_n_chars(const char *s, int n) {
    for (int i = 0; i < n; ++i) {
        printf("%s", s);
    }
}

// Print top border
void print_box_top(int width, const char *color) {
    printf("%s", color);
    printf(BOX_TOP_LEFT);
    print_n_chars(BOX_HORIZONTAL, width - 2);
    printf(BOX_TOP_RIGHT);
    printf(ANSI_RESET "\n");
}

// Print bottom border
void print_box_bottom(int width, const char *color) {
    printf("%s", color);
    printf(BOX_BOTTOM_LEFT);
    print_n_chars(BOX_HORIZONTAL, width - 2);
    printf(BOX_BOTTOM_RIGHT);
    printf(ANSI_RESET "\n");
}

// Print a line of text wrapped within box borders (Used ONLY for code blocks now)
void print_bordered_line(const char *text, int width, const char *line_color, const char *border_color, const char *prefix) {
    int available_width = width - 4; // Width inside the borders (space | text | space)
    int prefix_len_visible = 0;
    if (prefix) {
        prefix_len_visible = visible_strlen(prefix);
        available_width -= prefix_len_visible;
    }
    if (available_width < 1) available_width = 1; // Ensure minimum space

    char *text_to_print = strdup(text);
    if (!text_to_print) return;

    char *current_pos = text_to_print;
    int first_line = 1;

    while (*current_pos != '\0') {
        size_t line_len_visible = 0;
        size_t line_len_actual = 0;
        size_t last_space_actual = 0;
        size_t last_space_visible = 0;
        int in_escape = 0;
        int can_wrap_here = 0;

        for (size_t i = 0; ; ++i) {
             can_wrap_here = 0;
             if (current_pos[i] == '\0' || current_pos[i] == '\n') {
                line_len_actual = i;
                can_wrap_here = 0;
                break;
            }

            if (current_pos[i] == '\x1b') {
                in_escape = 1;
            } else if (in_escape && current_pos[i] == 'm') {
                in_escape = 0;
            } else if (!in_escape) {
                line_len_visible++;
                if (current_pos[i] == ' ') {
                    last_space_actual = i;
                    last_space_visible = line_len_visible;
                    can_wrap_here = 1;
                } else {
                     can_wrap_here = 1;
                }
            }
            if (line_len_visible > available_width) {
                if (last_space_visible > 0) {
                    line_len_actual = last_space_actual;
                } else {
                    size_t rewind_actual = 0;
                    size_t rewind_visible = 0;
                    int esc = 0;
                    for(size_t j = 0; current_pos[j] != '\0' && rewind_visible < available_width ; ++j) {
                         if (current_pos[j] == '\x1b') esc = 1;
                         else if (esc && current_pos[j] == 'm') esc = 0;
                         else if (!esc) rewind_visible++;
                         rewind_actual++;
                    }
                    line_len_actual = rewind_actual;
                }
                can_wrap_here = 0;
                break;
            }
             line_len_actual = i + 1;
        }

        printf("%s%s %s", border_color, BOX_VERTICAL, ANSI_RESET);
        if (line_color) printf("%s", line_color);
        if (prefix && first_line) printf("%s", prefix);
        else if (prefix && !first_line) {
             print_n_chars(" ", prefix_len_visible);
        }

        printf("%.*s", (int)line_len_actual, current_pos);

        char temp_segment[line_len_actual + 1];
        strncpy(temp_segment, current_pos, line_len_actual);
        temp_segment[line_len_actual] = '\0';
        size_t printed_visible_len = visible_strlen(temp_segment);

        int padding = available_width - printed_visible_len;
        if (padding < 0) padding = 0;

        print_n_chars(" ", padding);

        if (line_color) printf(ANSI_RESET);
        printf(" %s%s%s\n", border_color, BOX_VERTICAL, ANSI_RESET);

        current_pos += line_len_actual;
        if (*current_pos == ' ' || *current_pos == '\n') {
            // Avoid double skip if we broke exactly at \n
            if (!(*current_pos == '\n' && line_len_actual > 0 && *(current_pos - 1) != '\n')) {
                 current_pos++;
            }
        }
        first_line = 0;
    }
    free(text_to_print);
}

// Simple print function with basic markdown/prefix handling (No BORDERS)
void print_simple_line(const char *line) {
    if (strncmp(line, "**", 2) == 0 && strlen(line) > 3 && line[strlen(line)-1] == '*' && line[strlen(line)-2] == '*') {
        // Bold: **text**
        char* temp_bold = strdup(line + 2);
        if (temp_bold) {
            temp_bold[strlen(temp_bold) - 2] = '\0'; // Remove trailing **
            printf(ANSI_BOLD "%s" ANSI_RESET "\n", temp_bold);
            free(temp_bold);
        } else {
             printf("%s\n", line); // Fallback
        }
    } else if (strncmp(line, "*", 1) == 0 && strlen(line) > 2 && line[strlen(line)-1] == '*') {
        // Italic: *text*
        char* temp_italic = strdup(line + 1);
        if (temp_italic) {
            temp_italic[strlen(temp_italic) - 1] = '\0'; // Remove trailing *
            printf(ANSI_DIM "%s" ANSI_RESET "\n", temp_italic); // Using DIM for italic
            free(temp_italic);
        } else {
             printf("%s\n", line); // Fallback
        }
    } else if (is_note_block(line)) { // <--- Call is now valid
        // Note: > text
        printf(ANSI_BLUE "> %s" ANSI_RESET "\n", line + 2);
    } else if (is_warning_block(line)) { // <--- Call is now valid
        // Warning: ! text
        printf(ANSI_YELLOW "! %s" ANSI_RESET "\n", line + 2);
    }
     else {
        // Regular text
        printf("%s\n", line);
    }
}

// Struct to store API response
struct Response {
    char *data;
    size_t size;
};

// Callback function for writing API response into memory (Keep as before)
size_t write_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t real_size = size * nmemb;
    struct Response *res = (struct Response *)userdata;

    char *ptr_new = realloc(res->data, res->size + real_size + 1);
    if (!ptr_new) {
        fprintf(stderr, "Memory allocation failed\n");
        return 0; // Returning 0 signals error to curl
    }

    res->data = ptr_new;
    memcpy(&(res->data[res->size]), ptr, real_size);
    res->size += real_size;
    res->data[res->size] = '\0';  // Null-terminate

    return real_size;
}


// Function to detect if a line is a code block marker (Definition)
int is_code_block_marker(const char *line) {
    if (strncmp(line, "```", 3) == 0) {
        return 1;
    }
    return 0;
}

// Function to detect if a line is a note block (Definition)
int is_note_block(const char *line) {
    return strncmp(line, "> ", 2) == 0;
}

// Function to detect if a line is a warning block (Definition)
int is_warning_block(const char *line) {
    return strncmp(line, "! ", 2) == 0;
}

// Enum for block state
typedef enum {
    BLOCK_NONE,
    BLOCK_CODE,
} BlockType;

// Function to print headers
void print_header(const char *line, int level, int width) {
    // Remove leading '#'s and space
    const char *header_text = line;
    while (*header_text == '#') header_text++;
    if (*header_text == ' ') header_text++;
    
    // Assign color based on header level
    const char *color;
    switch(level) {
        case 1: color = ANSI_MAGENTA; break;
        case 2: color = ANSI_BLUE; break;
        case 3: color = ANSI_GREEN; break;
        default: color = ANSI_RESET; break;
    }

    // Print header inside a box
    print_box_top(width, color);
    printf("%s%s %s", color, BOX_VERTICAL, ANSI_RESET);
    printf("%s%s%s", ANSI_BOLD, header_text, ANSI_RESET);
    size_t visible_len = visible_strlen(header_text);
    int padding = width - 4 - visible_len;
    if (padding < 0) padding = 0;
    print_n_chars(" ", padding);
    printf("%s%s%s\n", color, BOX_VERTICAL, ANSI_RESET);
    print_box_bottom(width, color);
}

// Function to detect headers
int is_header(const char *line, int *header_level) {
    int level = 0;
    while (*line == '#') {
        level++;
        line++;
    }
    if (level > 0 && *line == ' ') {
        *header_level = level;
        return 1;
    }
    return 0;
}

// Function to detect unordered list items
int is_unordered_list(const char *line, const char **marker) {
    if (line[0] == '*' && line[1] == ' ') {
        *marker = "*";
        return 1;
    }
    if (line[0] == '-' && line[1] == ' ') {
        *marker = "-";
        return 1;
    }
    if (line[0] == '+' && line[1] == ' ') {
        *marker = "+";
        return 1;
    }
    return 0;
}

// Function to detect ordered list items
int is_ordered_list(const char *line, int *order_number) {
    int num = 0;
    const char *p = line;
    while (*p >= '0' && *p <= '9') {
        num = num * 10 + (*p - '0');
        p++;
    }
    if (num > 0 && *p == '.' && p[1] == ' ') {
        *order_number = num;
        return 1;
    }
    return 0;
}

// Function to detect horizontal rules
int is_horizontal_rule(const char *line) {
    int len = strlen(line);
    if (len < 3)
        return 0;
    for(int i=0; i<len; i++) {
        if(line[i] != '-' && line[i] != '*' && line[i] != '_') {
            return 0;
        }
    }
    return 1;
}

// Function to print unordered list items
void print_unordered_list_item(const char *marker, const char *text, int width) {
    char display_text[width - 6];
    snprintf(display_text, sizeof(display_text), "%s %s", marker, text);
    printf("%s%s %s", ANSI_DIM, BOX_VERTICAL, ANSI_RESET);
    printf("%s%s", ANSI_MAGENTA, display_text); // Using magenta for markers
    size_t visible_len = visible_strlen(display_text);
    int padding = width - 4 - visible_len;
    if (padding < 0) padding = 0;
    print_n_chars(" ", padding);
    printf("%s%s%s\n", ANSI_MAGENTA, BOX_VERTICAL, ANSI_RESET);
}

// Function to print ordered list items
void print_ordered_list_item(int number, const char *text, int width) {
    char display_text[width - 6];
    snprintf(display_text, sizeof(display_text), "%d. %s", number, text);
    printf("%s%s %s", ANSI_DIM, BOX_VERTICAL, ANSI_RESET);
    printf("%s%s", ANSI_GREEN, display_text); // Using green for numbers
    size_t visible_len = visible_strlen(display_text);
    int padding = width - 4 - visible_len;
    if (padding < 0) padding = 0;
    print_n_chars(" ", padding);
    printf("%s%s%s\n", ANSI_GREEN, BOX_VERTICAL, ANSI_RESET);
}

// Function to print blockquotes
void print_blockquote(const char *text, int width) {
    char display_text[width - 6];
    snprintf(display_text, sizeof(display_text), "> %s", text);
    printf("%s%s %s", ANSI_DIM, BOX_VERTICAL, ANSI_RESET);
    printf("%s", display_text); // Using dim for blockquotes
    size_t visible_len = visible_strlen(display_text);
    int padding = width - 4 - visible_len;
    if (padding < 0) padding = 0;
    print_n_chars(" ", padding);
    printf("%s%s%s\n", ANSI_DIM, BOX_VERTICAL, ANSI_RESET);
}

// Function to print horizontal rule
void print_horizontal_rule(int width) {
    // Print a dim horizontal line inside borders
    printf("%s", ANSI_DIM);
    printf("%s", BOX_VERTICAL);
    print_n_chars(BOX_HORIZONTAL, width - 4);
    printf("%s", BOX_VERTICAL);
    printf("%s\n", ANSI_RESET);
}

// Function to print formatted text (REVISED FOR NEW STYLE)
void print_formatted_text(const char *text) {
    char *text_copy = strdup(text);
    if (!text_copy) return;

    int term_width = get_terminal_width();
    if (term_width < 20) term_width = 20; // Minimum sensible width

    BlockType current_block = BLOCK_NONE;
    char *line = strtok(text_copy, "\n");
    int first_line_after_block = 1; // Flag to add spacing
    int buffer_len = 0;
    char buffer[10000] = {0}; // Buffer to collect code between markers

    while (line != NULL) {
        if (is_code_block_marker(line)) { // <--- Call is now valid
            if (current_block == BLOCK_CODE) {
                // --- End of Code Block ---
                if (buffer_len > 0) {
                    // Print the buffered code inside a box without title
                    print_box_top(term_width, ANSI_CYAN);
                    
                    // Print each line of the buffered code
                    char *code_line = strtok(buffer, "\n");
                    while (code_line != NULL) {
                        print_bordered_line(code_line, term_width, ANSI_CYAN, ANSI_CYAN, "  ");
                        code_line = strtok(NULL, "\n");
                    }
                    
                    print_box_bottom(term_width, ANSI_CYAN);
                    
                    // Reset buffer
                    buffer[0] = '\0';
                    buffer_len = 0;
                }
                current_block = BLOCK_NONE;
                first_line_after_block = 1;
            } else {
                // --- Start of Code Block ---
                if (!first_line_after_block) {
                    printf("\n");
                }
                
                // Just mark the beginning of a code block, don't print anything yet
                current_block = BLOCK_CODE;
            }
        } else if (current_block == BLOCK_CODE) {
            // --- Inside Code Block: Buffer the lines ---
            if (buffer_len + strlen(line) + 2 < sizeof(buffer)) {
                // Add line to buffer
                strcat(buffer, line);
                strcat(buffer, "\n");
                buffer_len += strlen(line) + 1;
            }
            first_line_after_block = 0;
        } else {
            // --- Regular Text, Header, List, Blockquote, Horizontal Rule ---
            int header_level;
            const char *unordered_marker;
            int ordered_number;
            if (is_header(line, &header_level)) {
                // Handle header
                print_header(line, header_level, term_width);
            } else if (is_unordered_list(line, &unordered_marker)) {
                // Handle unordered list item
                // Get list item text
                const char *item_text = line;
                while (*item_text != ' ' && *item_text != '\0') item_text++;
                if (*item_text == ' ') item_text++;
                print_unordered_list_item(unordered_marker, item_text, term_width);
            } else if (is_ordered_list(line, &ordered_number)) {
                // Handle ordered list item
                // Get list item text
                const char *item_text = line;
                while (*item_text != '.' && *item_text != '\0') item_text++;
                if (*item_text == '.') item_text++;
                if (*item_text == ' ') item_text++;
                print_ordered_list_item(ordered_number, item_text, term_width);
            } else if (is_horizontal_rule(line)) {
                // Handle horizontal rule
                print_horizontal_rule(term_width);
            } else if (is_note_block(line)) {
                // Handle note
                print_blockquote(line + 2, term_width);
            } else if (is_warning_block(line)) {
                // Handle warning
                print_blockquote(line + 2, term_width); // You may want different styling
            } else {
                // Regular text
                if (strlen(line) > 0) {
                    int is_note_or_warning = is_note_block(line) || is_warning_block(line); // Check type
                    if (first_line_after_block && is_note_or_warning) {
                        printf("\n");
                    }
                    print_simple_line(line);
                    // Treat consecutive notes/warnings like block separators for spacing
                    first_line_after_block = is_note_or_warning;
                } else {
                    if (!first_line_after_block) {
                        printf("\n");
                        first_line_after_block = 1;
                    }
                }
            }
        }

        line = strtok(NULL, "\n");
    }

    // Close the code block if the text ended while inside one
    if (current_block == BLOCK_CODE && buffer_len > 0) {
        // Print any remaining buffered code
        print_box_top(term_width, ANSI_CYAN);
        // No title
        char *code_line = strtok(buffer, "\n");
        while (code_line != NULL) {
            print_bordered_line(code_line, term_width, ANSI_CYAN, ANSI_CYAN, "  ");
            code_line = strtok(NULL, "\n");
        }
        print_box_bottom(term_width, ANSI_CYAN);
    }

    free(text_copy);
}


// Parse JSON and print (Keep Error Boxing, Use New Formatter for Success)
void parse_and_print_output(const char *json_str) {
    cJSON *json = cJSON_Parse(json_str);
    int term_width = get_terminal_width();

    // --- JSON Parse Error Handling (Keep Boxed) ---
    if (!json) {
        fprintf(stderr, "Error parsing JSON: %s\n", cJSON_GetErrorPtr());
        print_box_top(term_width, ANSI_RED);
        print_bordered_line(ANSI_BOLD "JSON Parse Error" ANSI_RESET, term_width, NULL, ANSI_RED, NULL);
        const char *err_ptr_msg = cJSON_GetErrorPtr() ? cJSON_GetErrorPtr() : "(Unknown parse error)";
        char *err_ptr_copy = strdup(err_ptr_msg); // Use intermediate variable
        if (err_ptr_copy) {
            print_bordered_line(err_ptr_copy, term_width, ANSI_RED, ANSI_RED, "  ");
            free(err_ptr_copy);
        }
        print_bordered_line("--- Raw Response Snippet ---", term_width, ANSI_DIM, ANSI_RED, NULL);
        char *raw_copy = strndup(json_str, 200);
        if(raw_copy) {
            char *line = strtok(raw_copy, "\n");
            int lines_shown = 0;
            while(line && lines_shown < 5) {
                 print_bordered_line(line, term_width, ANSI_DIM, ANSI_RED, "  ");
                 line = strtok(NULL, "\n");
                 lines_shown++;
            }
            if(line) print_bordered_line("...", term_width, ANSI_DIM, ANSI_RED, "  ");
            free(raw_copy);
        } else {
             print_bordered_line("(Could not copy raw response)", term_width, ANSI_DIM, ANSI_RED, "  ");
        }
        print_box_bottom(term_width, ANSI_RED);
        return;
    }

    // --- API Error Handling (Keep Boxed) ---
    cJSON *error = cJSON_GetObjectItemCaseSensitive(json, "error");
    if (error) {
        cJSON *message = cJSON_GetObjectItemCaseSensitive(error, "message");
        print_box_top(term_width, ANSI_RED);
        print_bordered_line(ANSI_BOLD "API Error" ANSI_RESET, term_width, NULL, ANSI_RED, NULL);
        if (cJSON_IsString(message) && message->valuestring) {
            char* msg_copy = strdup(message->valuestring);
            if (msg_copy) {
                print_bordered_line(msg_copy, term_width, ANSI_RED, ANSI_RED, "  ");
                free(msg_copy);
            } else { 
                print_bordered_line("(Failed to copy error message)", term_width, ANSI_RED, ANSI_RED, "  "); 
            }
        } else {
            print_bordered_line("Unknown API Error structure.", term_width, ANSI_RED, ANSI_RED, "  ");
            char* formatted_error = cJSON_Print(error);
            if (formatted_error) {
                char* err_copy = strdup(formatted_error);
                if(err_copy){
                    print_bordered_line(err_copy, term_width, ANSI_DIM, ANSI_RED, "  ");
                    free(err_copy);
                } else { 
                    print_bordered_line("(Failed to copy error details)", term_width, ANSI_DIM, ANSI_RED, "  ");
                }
                free(formatted_error);
            }
        }
        print_box_bottom(term_width, ANSI_RED);
        cJSON_Delete(json);
        return;
    }

    // --- Success Path ---
    cJSON *candidates = cJSON_GetObjectItemCaseSensitive(json, "candidates");
    if (!cJSON_IsArray(candidates) || cJSON_GetArraySize(candidates) == 0) {
         print_box_top(term_width, ANSI_YELLOW);
         print_bordered_line(ANSI_BOLD "API Warning" ANSI_RESET, term_width, NULL, ANSI_YELLOW, NULL);
         print_bordered_line("No 'candidates' array found or it's empty.", term_width, ANSI_YELLOW, ANSI_YELLOW, "  ");
         print_box_bottom(term_width, ANSI_YELLOW);
         cJSON_Delete(json);
         return;
    }

    cJSON *first_candidate = cJSON_GetArrayItem(candidates, 0);
    if (!cJSON_IsObject(first_candidate)) { /* Handle error */
        print_box_top(term_width, ANSI_YELLOW);
        print_bordered_line(ANSI_BOLD "API Warning" ANSI_RESET, term_width, NULL, ANSI_YELLOW, NULL);
        print_bordered_line("First candidate is not a valid object.", term_width, ANSI_YELLOW, ANSI_YELLOW, "  ");
        print_box_bottom(term_width, ANSI_YELLOW);
        cJSON_Delete(json); return;
    }
    cJSON *content = cJSON_GetObjectItemCaseSensitive(first_candidate, "content");
    if (!cJSON_IsObject(content)) { /* Handle error */
        print_box_top(term_width, ANSI_YELLOW);
        print_bordered_line(ANSI_BOLD "API Warning" ANSI_RESET, term_width, NULL, ANSI_YELLOW, NULL);
        print_bordered_line("No 'content' object found in candidate.", term_width, ANSI_YELLOW, ANSI_YELLOW, "  ");
        print_box_bottom(term_width, ANSI_YELLOW);
        cJSON_Delete(json); return;
     }
    cJSON *parts = cJSON_GetObjectItemCaseSensitive(content, "parts");
    if (!cJSON_IsArray(parts) || cJSON_GetArraySize(parts) == 0) { /* Handle error */
        print_box_top(term_width, ANSI_YELLOW);
        print_bordered_line(ANSI_BOLD "API Warning" ANSI_RESET, term_width, NULL, ANSI_YELLOW, NULL);
        print_bordered_line("No 'parts' array found or it's empty in content.", term_width, ANSI_YELLOW, ANSI_YELLOW, "  ");
        print_box_bottom(term_width, ANSI_YELLOW);
        cJSON_Delete(json); return;
    }
    cJSON *first_part = cJSON_GetArrayItem(parts, 0);
    if (!cJSON_IsObject(first_part)) { /* Handle error */
        print_box_top(term_width, ANSI_YELLOW);
        print_bordered_line(ANSI_BOLD "API Warning" ANSI_RESET, term_width, NULL, ANSI_YELLOW, NULL);
        print_bordered_line("First part is not a valid object.", term_width, ANSI_YELLOW, ANSI_YELLOW, "  ");
        print_box_bottom(term_width, ANSI_YELLOW);
        cJSON_Delete(json); return;
     }
    cJSON *text = cJSON_GetObjectItemCaseSensitive(first_part, "text");

    if (cJSON_IsString(text) && text->valuestring) {
        print_formatted_text(text->valuestring);
    } else {
         print_box_top(term_width, ANSI_YELLOW);
         print_bordered_line(ANSI_BOLD "API Warning" ANSI_RESET, term_width, NULL, ANSI_YELLOW, NULL);
         print_bordered_line("Response structure OK, but 'text' field missing or not a string.", term_width, ANSI_YELLOW, ANSI_YELLOW, "  ");
         print_box_bottom(term_width, ANSI_YELLOW);
    }

    cJSON_Delete(json);
}


// Function to check if a path is absolute (Keep as before)
int is_absolute_path(const char *path) {
    return path != NULL && path[0] == '/';
}

// Function to read file contents (Keep as before)
char* read_file_contents(const char *filepath) {
    FILE *file = fopen(filepath, "rb");
    if (!file) {
        fprintf(stderr, "Error opening file '%s': %s\n", filepath, strerror(errno));
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
     if (size == -1) {
        fprintf(stderr, "Error getting size of file '%s': %s\n", filepath, strerror(errno));
        fclose(file);
        return NULL;
    }
    if (size == 0) {
        fclose(file);
        char *empty_content = malloc(1);
        if(empty_content) empty_content[0] = '\0';
        else fprintf(stderr, "Malloc failed for empty file content\n");
        return empty_content;
    }
    rewind(file);
    char *contents = malloc(size + 1);
    if (!contents) {
        fprintf(stderr, "Memory allocation failed for file contents (%ld bytes)\n", size + 1);
        fclose(file);
        return NULL;
    }
    size_t read_size = fread(contents, 1, size, file);
    if (read_size != (size_t)size) {
        if (feof(file)) { fprintf(stderr, "Error reading file '%s': Unexpected end of file\n", filepath); }
        else if(ferror(file)) { fprintf(stderr, "Error reading file '%s': %s\n", filepath, strerror(errno)); }
        else { fprintf(stderr, "Error reading file '%s': Read size mismatch (%zu != %ld)\n", filepath, read_size, size); }
        fclose(file);
        free(contents);
        return NULL;
    }
    contents[size] = '\0';
    fclose(file);
    return contents;
}

// Escape JSON string literal content (Keep as before)
char *escape_json_string(const char *input) {
    if (!input) return NULL;
    size_t len = strlen(input);
    size_t buffer_size = len * 2 + 3; // Initial estimate
    char *escaped = malloc(buffer_size);
    if (!escaped) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        // Ensure enough space for worst-case expansion (\uXXXX is 6 chars, plus potentially \\)
        if (j >= buffer_size - 7) {
            buffer_size = buffer_size * 2 + 10; // Increase buffer size
            char *new_escaped = realloc(escaped, buffer_size);
            if (!new_escaped) { free(escaped); return NULL; }
            escaped = new_escaped;
        }
        switch (input[i]) {
            case '"':  escaped[j++] = '\\'; escaped[j++] = '"'; break;
            case '\\': escaped[j++] = '\\'; escaped[j++] = '\\'; break;
            case '\n': escaped[j++] = '\\'; escaped[j++] = 'n'; break;
            case '\r': escaped[j++] = '\\'; escaped[j++] = 'r'; break;
            case '\t': escaped[j++] = '\\'; escaped[j++] = 't'; break;
            case '\b': escaped[j++] = '\\'; escaped[j++] = 'b'; break;
            case '\f': escaped[j++] = '\\'; escaped[j++] = 'f'; break;
            default:
                // Escape control characters (U+0000 to U+001F)
                if ((unsigned char)input[i] < 0x20) {
                   // Use snprintf for safety, ensure null termination is handled by caller or later loop stage
                   int written = snprintf(&escaped[j], 7, "\\u%04x", (unsigned char)input[i]);
                   if (written == 6) { // Check if snprintf wrote the expected number of chars
                       j += 6;
                   } else {
                       // Handle error: snprintf failed or wrote unexpected number of bytes
                       fprintf(stderr, "Warning: snprintf failed during JSON escaping.\n");
                       // Option: Skip this character or handle error differently
                   }
                } else {
                    escaped[j++] = input[i]; // Keep other printable chars as is
                }
                break;
        }
    }
    escaped[j] = '\0'; // Null-terminate the final string
    return escaped;
}


// Main function (Keep mostly as before)
int main(int argc, char *argv[]) {
    setlocale(LC_ALL, "");

    if (argc < 2) {
        fprintf(stderr, ANSI_YELLOW "Usage:" ANSI_RESET " %s <\"prompt text\"> | <path/to/prompt.txt>\n", argv[0]);
        return 1;
    }

    char *api_key = getenv("GEMINI_API_KEY");
    if (!api_key) {
        int term_width = get_terminal_width();
        print_box_top(term_width, ANSI_RED);
        print_bordered_line(ANSI_BOLD "Configuration Error" ANSI_RESET, term_width, NULL, ANSI_RED, NULL);
        print_bordered_line("GEMINI_API_KEY environment variable not set.", term_width, ANSI_RED, ANSI_RED, "->");
        print_bordered_line("Set it using: export GEMINI_API_KEY='your_api_key'", term_width, ANSI_DIM, ANSI_RED, "  ");
        print_box_bottom(term_width, ANSI_RED);
        return 1;
    }

    CURL *curl;
    CURLcode res;
    struct Response response = {NULL, 0};
    char *input_raw = NULL;
    int is_file = 0;

    // --- Determine Input: File or Text ---
    if (argc == 2) {
        struct stat st;
        if (stat(argv[1], &st) == 0 && S_ISREG(st.st_mode)) {
            printf(ANSI_DIM "[Reading input from file: %s]\n" ANSI_RESET, argv[1]);
            input_raw = read_file_contents(argv[1]);
            is_file = 1;
            if (!input_raw) { return 1; }
        }
    }
    if (!is_file) {
        size_t total_len = 0;
        for (int i = 1; i < argc; i++) { total_len += strlen(argv[i]) + 1; }
        input_raw = malloc(total_len);
        if (!input_raw) { fprintf(stderr, "Failed to allocate memory for prompt.\n"); return 1; }
        input_raw[0] = '\0';
        for (int i = 1; i < argc; i++) {
            strcat(input_raw, argv[i]);
            if (i < argc - 1) { strcat(input_raw, " "); }
        }
         printf(ANSI_DIM "[Using prompt text]\n" ANSI_RESET);
    }

    // --- Escape Input for JSON ---
    char *input_escaped = escape_json_string(input_raw);
    free(input_raw);
    if (!input_escaped) { fprintf(stderr, "Failed to escape input string for JSON.\n"); return 1; }

    // --- Construct JSON Payload ---
    // Estimate size: base JSON structure + escaped input length + config + buffer
    size_t json_base_len = strlen("{ \"contents\": [{ \"parts\": [{ \"text\": \"\" }] }], \"generationConfig\": { \"temperature\": 0.7 } }");
    size_t post_data_size = json_base_len + strlen(input_escaped) + 50; // Add buffer
    char *post_data = malloc(post_data_size);
    if (!post_data) { fprintf(stderr, "Failed to allocate memory for POST data.\n"); free(input_escaped); return 1; }
    snprintf(post_data, post_data_size, "{ \"contents\": [{ \"parts\": [{ \"text\": \"%s\" }] }], \"generationConfig\": { \"temperature\": 0.7 } }", input_escaped);
    free(input_escaped);

    // --- Prepare and Perform CURL Request ---
    char api_url[512];
    snprintf(api_url, sizeof(api_url), "https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash-latest:generateContent?key=%s", api_key);

    curl = curl_easy_init();
    if (!curl) { fprintf(stderr, "Failed to initialize CURL\n"); free(post_data); return 1; }

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    response.data = malloc(1); // Start with minimal allocation
    if (!response.data) { fprintf(stderr, "Initial malloc failed for response data\n"); free(post_data); curl_easy_cleanup(curl); curl_slist_free_all(headers); return 1; }
    response.data[0] = '\0';
    response.size = 0;

    curl_easy_setopt(curl, CURLOPT_URL, api_url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 90L);
    // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

    printf(ANSI_DIM "Sending request to Gemini...\n" ANSI_RESET);
    res = curl_easy_perform(curl);
    printf("\n");

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    // --- Handle Response ---
    if (res != CURLE_OK) { // Curl Error (Network, DNS, etc.)
        int term_width = get_terminal_width();
        print_box_top(term_width, ANSI_RED);
        print_bordered_line(ANSI_BOLD "CURL Error" ANSI_RESET, term_width, NULL, ANSI_RED, NULL);
        char error_buf[CURL_ERROR_SIZE * 2]; // Larger buffer just in case
        snprintf(error_buf, sizeof(error_buf), "Code: %d (%s)", res, curl_easy_strerror(res));
        print_bordered_line(error_buf, term_width, ANSI_RED, ANSI_RED, "->");
        if (http_code != 0 && http_code != 200) { // Show HTTP code if relevant and not OK
            snprintf(error_buf, sizeof(error_buf), "HTTP Status: %ld", http_code);
            print_bordered_line(error_buf, term_width, ANSI_RED, ANSI_RED, "->");
        }
        if (response.size > 0) {
            print_bordered_line("--- Response Body Snippet ---", term_width, ANSI_DIM, ANSI_RED, NULL);
             char *resp_copy = strndup(response.data, 200);
             if (resp_copy) {
                 char* line = strtok(resp_copy, "\n");
                 int lines_shown = 0;
                 while(line && lines_shown < 5) {
                     print_bordered_line(line, term_width, ANSI_DIM, ANSI_RED, "  ");
                     line = strtok(NULL, "\n");
                     lines_shown++;
                 }
                 if(line) print_bordered_line("...", term_width, ANSI_DIM, ANSI_RED, "  ");
                 free(resp_copy);
             } else { print_bordered_line("(Failed to copy response snippet)", term_width, ANSI_DIM, ANSI_RED, "  "); }
        }
        print_box_bottom(term_width, ANSI_RED);
    } else { // Curl request succeeded, check HTTP status and body
        if (response.data && response.size > 0) {
            // We have a response body, parse it (handles JSON errors / API errors inside)
            parse_and_print_output(response.data);
        } else if (http_code >= 200 && http_code < 300) {
             // Successful HTTP status but empty body
             int term_width = get_terminal_width();
             print_box_top(term_width, ANSI_YELLOW);
             print_bordered_line(ANSI_BOLD "API Warning" ANSI_RESET, term_width, NULL, ANSI_YELLOW, NULL);
             print_bordered_line("Request successful (HTTP OK), but no response body received.", term_width, ANSI_YELLOW, ANSI_YELLOW, "->");
             print_box_bottom(term_width, ANSI_YELLOW);
        } else {
             // Non-successful HTTP status and no response body
             int term_width = get_terminal_width();
             print_box_top(term_width, ANSI_RED);
             print_bordered_line(ANSI_BOLD "HTTP Error" ANSI_RESET, term_width, NULL, ANSI_RED, NULL);
             char error_buf[100];
             snprintf(error_buf, sizeof(error_buf), "Received HTTP Status: %ld (No response body)", http_code);
             print_bordered_line(error_buf, term_width, ANSI_RED, ANSI_RED, "->");
             print_box_bottom(term_width, ANSI_RED);
        }
    }

    // Cleanup
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    free(post_data);
    free(response.data);

    // Return non-zero if curl failed OR http code indicates failure
    return (res == CURLE_OK && http_code >= 200 && http_code < 300) ? 0 : 1;
}