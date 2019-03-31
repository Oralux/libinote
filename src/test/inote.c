#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "inote.h"


#define BUFFER_SIZE (4*1024)
#define MAX_LANG 2

enum {
  NONE = '0',
  ALL = '1',
  SOME = '2',  
} punctuation_mode_t; 

enum {
  UNDEFINED_LANGUAGE,
  ENGLISH,
  FRENCH,  
} language_t; 

int main(int argc, char **argv)
{
  struct input_t input;
  struct output_t output;
  struct state_t state;  
  
  if (argc<2)
    return 1;

  memset(&input, 0, sizeof(input));
  memset(&output, 0, sizeof(output));
  memset(&state, 0, sizeof(state));
  
  input.text = calloc(1, BUFFER_SIZE);
  strncpy(input.text, argv[1], BUFFER_SIZE-1);
  input.text[BUFFER_SIZE-1] = 0;
  input.max_size = BUFFER_SIZE;

  state.punctuation_mode = ALL;
  state.spelling_enabled = 0;
  state.expected_lang = calloc(MAX_LANG, sizeof(*state.expected_lang));
  state.expected_lang[0] = ENGLISH;
  state.expected_lang[1] = FRENCH;
  state.max_expected_lang = MAX_LANG;
  state.ssml_enabled = 1;
    
  output.buffer = calloc(1, BUFFER_SIZE);
  output.max_size = BUFFER_SIZE;

  
  inote_get_annotated_text(&input, &state, &output);  
  printf("output: %d %d\n", output.buffer[0], output.buffer[1]);

}
