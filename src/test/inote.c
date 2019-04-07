#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "inote.h"


#define BUFFER_SIZE (4*1024)
#define MAX_LANG 2

enum {
  UNDEFINED_LANGUAGE,
  ENGLISH,
  FRENCH,  
} language_t; 

int main(int argc, char **argv)
{
  inote_slice_t text;
  inote_slice_t type_length_value;
  inote_state_t state;  
  size_t text_offset = 0;
  
  if (argc<2)
    return 1;

  memset(&text, 0, sizeof(text));
  memset(&type_length_value, 0, sizeof(type_length_value));
  memset(&state, 0, sizeof(state));
  
  text.buffer = calloc(1, BUFFER_SIZE);
  strncpy(text.buffer, argv[1], BUFFER_SIZE-1);
  text.buffer[BUFFER_SIZE-1] = 0;
  text.length = strlen(text.buffer);
  text.charset = INOTE_CHARSET_UTF_8;
  text.max_size = BUFFER_SIZE;

  state.punctuation = INOTE_PUNCT_MODE_ALL;
  state.spelling = 0;
  state.expected_lang = calloc(MAX_LANG, sizeof(*state.expected_lang));
  state.expected_lang[0] = ENGLISH;
  state.expected_lang[1] = FRENCH;
  state.max_expected_lang = MAX_LANG;
  state.ssml = 1;
    
  type_length_value.buffer = calloc(1, BUFFER_SIZE);
  type_length_value.max_size = BUFFER_SIZE;
  type_length_value.charset = INOTE_CHARSET_UTF_8;

  void *handle = inote_create();
  inote_get_annotated_text(handle, &text, &state, &type_length_value, &text_offset);  
  printf("type_length_value: %d %d\n", type_length_value.buffer[0], type_length_value.buffer[1]);
  inote_delete(handle);
}
