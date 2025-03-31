#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>

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

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <prompt text>\n", argv[0]);
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

    // Join all arguments into a single prompt
    char input[2000] = "";
    for (int i = 1; i < argc; i++) {
        if (i > 1) strncat(input, " ", sizeof(input) - strlen(input) - 1);
        strncat(input, argv[i], sizeof(input) - strlen(input) - 1);
    }

    // Escape JSON special characters in the input
    CURL *escape_curl = curl_easy_init();
    char *escaped_input = curl_easy_escape(escape_curl, input, 0);
    if (!escaped_input) {
        fprintf(stderr, "Failed to escape input\n");
        curl_easy_cleanup(escape_curl);
        return 1;
    }

    // JSON payload
    char post_data[2500];
    snprintf(post_data, sizeof(post_data),
             "{ \"contents\": [{ \"parts\": [{ \"text\": \"%s\" }] }] }",
             escaped_input);
    
    curl_free(escaped_input);
    curl_easy_cleanup(escape_curl);

    // Create API URL with key
    char api_url[256];
    snprintf(api_url, sizeof(api_url),
             "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-pro-exp-03-25:generateContent?key=%s",
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