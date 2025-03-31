#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

// Struct to store API response
struct Response {
    char *data;
    size_t size;
};

// Callback function for writing API response into memory
size_t write_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t real_size = size * nmemb;
    struct Response *res = (struct Response *)userdata;

    // Allocate memory dynamically
    char *ptr_new = realloc(res->data, res->size + real_size + 1);
    if (!ptr_new) {
        fprintf(stderr, "Memory allocation failed\n");
        return 0;
    }

    res->data = ptr_new;
    memcpy(&(res->data[res->size]), ptr, real_size);
    res->size += real_size;
    res->data[res->size] = '\0';  // Null-terminate

    return real_size;
}

void parse_and_print_output(const char *json_str) {
    cJSON *json = cJSON_Parse(json_str);
    if (!json) {
        fprintf(stderr, "Error parsing JSON: %s\n", cJSON_GetErrorPtr());
        printf("Raw response: %s\n", json_str);
        return;
    }

    // Check for error in the response
    cJSON *error = cJSON_GetObjectItemCaseSensitive(json, "error");
    if (error) {
        cJSON *message = cJSON_GetObjectItemCaseSensitive(error, "message");
        if (cJSON_IsString(message) && message->valuestring) {
            fprintf(stderr, "API Error: %s\n", message->valuestring);
        } else {
            fprintf(stderr, "Unknown API Error\n");
        }
        cJSON_Delete(json);
        return;
    }

    // Extract "candidates" array
    cJSON *candidates = cJSON_GetObjectItemCaseSensitive(json, "candidates");
    if (!cJSON_IsArray(candidates)) {
        fprintf(stderr, "No valid candidates array found\n");
        printf("Raw response: %s\n", json_str);
        cJSON_Delete(json);
        return;
    }

    // Get the first candidate
    cJSON *first_candidate = cJSON_GetArrayItem(candidates, 0);
    if (!cJSON_IsObject(first_candidate)) {
        fprintf(stderr, "No valid candidate object found\n");
        cJSON_Delete(json);
        return;
    }

    // Extract "content" object
    cJSON *content = cJSON_GetObjectItemCaseSensitive(first_candidate, "content");
    if (!cJSON_IsObject(content)) {
        fprintf(stderr, "No content object found\n");
        cJSON_Delete(json);
        return;
    }

    // Extract "parts" array
    cJSON *parts = cJSON_GetObjectItemCaseSensitive(content, "parts");
    if (!cJSON_IsArray(parts)) {
        fprintf(stderr, "No parts array found\n");
        cJSON_Delete(json);
        return;
    }

    // Get the first part
    cJSON *first_part = cJSON_GetArrayItem(parts, 0);
    if (!cJSON_IsObject(first_part)) {
        fprintf(stderr, "No valid part object found\n");
        cJSON_Delete(json);
        return;
    }

    // Extract "text" field
    cJSON *text = cJSON_GetObjectItemCaseSensitive(first_part, "text");
    if (cJSON_IsString(text) && text->valuestring) {
        printf("%s\n", text->valuestring);
    } else {
        fprintf(stderr, "Text not found in response\n");
    }

    cJSON_Delete(json);  // Free memory
}

// Function to check if a path is absolute
int is_absolute_path(const char *path) {
    return path[0] == '/';
}

// Function to read file contents
char* read_file_contents(const char *filepath) {
    FILE *file = fopen(filepath, "r");
    if (!file) {
        fprintf(stderr, "Error opening file '%s': %s\n", filepath, strerror(errno));
        return NULL;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Allocate memory for file contents
    char *contents = malloc(size + 1);
    if (!contents) {
        fprintf(stderr, "Memory allocation failed\n");
        fclose(file);
        return NULL;
    }

    // Read file contents
    size_t read = fread(contents, 1, size, file);
    contents[read] = '\0';
    fclose(file);

    return contents;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <prompt text or file path>\n", argv[0]);
        return 1;
    }

    // Get API key from environment variable
    char *api_key = getenv("GEMINI_API_KEY");
    if (!api_key) {
        fprintf(stderr, "Error: GEMINI_API_KEY environment variable not set\n");
        fprintf(stderr, "Please set it with: export GEMINI_API_KEY=your_api_key\n");
        return 1;
    }
    
    CURL *curl;
    CURLcode res;
    struct Response response = {NULL, 0};  // Initialize response struct

    // Check if the first argument is a file path
    char *input;
    if (argc == 2) {
        // Get current working directory
        char cwd[4096];  // Using a reasonable fixed size
        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            fprintf(stderr, "Error getting current directory: %s\n", strerror(errno));
            return 1;
        }

        // Construct full path if it's a relative path
        char full_path[4096];  // Using a reasonable fixed size
        if (is_absolute_path(argv[1])) {
            strncpy(full_path, argv[1], sizeof(full_path) - 1);
        } else {
            snprintf(full_path, sizeof(full_path), "%s/%s", cwd, argv[1]);
        }

        // Check if file exists
        struct stat st;
        if (stat(full_path, &st) == 0 && S_ISREG(st.st_mode)) {
            // Read file contents
            char *file_contents = read_file_contents(full_path);
            if (!file_contents) {
                return 1;
            }

            // Escape JSON special characters
            CURL *escape_curl = curl_easy_init();
            char *escaped_content = curl_easy_escape(escape_curl, file_contents, 0);
            if (!escaped_content) {
                fprintf(stderr, "Failed to escape file contents\n");
                free(file_contents);
                curl_easy_cleanup(escape_curl);
                return 1;
            }

            input = escaped_content;
            free(file_contents);
            curl_easy_cleanup(escape_curl);
        } else {
            // Not a file, treat as regular prompt
            input = strdup(argv[1]);
        }
    } else {
        // Join all arguments into a single prompt
        char temp_input[2000] = "";
        for (int i = 1; i < argc; i++) {
            if (i > 1) strcat(temp_input, " ");
            strcat(temp_input, argv[i]);
        }
        input = strdup(temp_input);
    }

    // JSON payload
    char post_data[2500];
    snprintf(post_data, sizeof(post_data),
             "{ \"contents\": [{ \"parts\": [{ \"text\": \"%s\" }] }] }",
             input);
    
    free(input);  // Free the input string

    // Create API URL with key
    char api_url[256];
    snprintf(api_url, sizeof(api_url),
             "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent?key=%s",
             api_key);

    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Failed to initialize CURL\n");
        return 1;
    }

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, api_url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        parse_and_print_output(response.data);
    } else {
        fprintf(stderr, "CURL request failed: %s\n", curl_easy_strerror(res));
    }

    // Cleanup
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    free(response.data);  // Free response memory

    return 0;
}