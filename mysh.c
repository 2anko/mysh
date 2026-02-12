#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "builtins.h"
#include "io_helpers.h"
#include "commands.h"
#include "variables.h"



// You can remove __attribute__((unused)) once argc and argv are used.
int main(__attribute__((unused)) int argc, 
         __attribute__((unused)) char* argv[]) {
    char *prompt = "mysh$ "; // TODO Step 1, Uncomment this.

    char input_buf[MAX_STR_LEN + 1];
    input_buf[MAX_STR_LEN] = '\0';
    
    char expanded_buf[MAX_STR_LEN + 1]; // Buffer for expanded string
    expanded_buf[MAX_STR_LEN] = '\0';

    char *token_arr[MAX_STR_LEN] = {NULL};

    while (1) {
        // Prompt and input tokenization

        display_message(prompt);

        int ret = get_input(input_buf);

        // Clean exit
        if (ret == 0) break;
        if (ret == -1) continue;

        char temp_buf[MAX_STR_LEN + 1];
        strncpy(temp_buf, input_buf, MAX_STR_LEN + 1);
        char *temp_tokens[MAX_STR_LEN] = {NULL};
        size_t temp_count = tokenize_input(temp_buf, temp_tokens);

        if (temp_count > 0 && strchr(temp_tokens[0], '=') != NULL) {
            // It is an assignment. 
            // Split at the first '='
            char *key = strtok(temp_tokens[0], "=");
            char *val = strtok(NULL, "=");
            
            if (key != NULL) {
                // If val is NULL, it means "var=", effectively empty string
                if (val == NULL) val = "";
                add_variable(key, val);
            }
            continue; // Skip execution for assignment lines
        }

        expand_input(expanded_buf, input_buf, MAX_STR_LEN);

        size_t token_count = tokenize_input(expanded_buf, token_arr);

        if (token_count == 0) continue;
        
        if (strcmp(token_arr[0], "exit") == 0) break;

        // Command execution
        cmd_status_t status = dispatch_command(token_arr);
        if (status == CMD_STATUS_EXIT) {
            break;
        }
    }

    return 0;
}
