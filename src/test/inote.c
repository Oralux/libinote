// --> For getop
#define _XOPEN_SOURCE 1
// <--
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "inote.h"


#define BUFFER_SIZE (2*MAX_TLV_LENGTH)
#define MAX_LANG 2

enum {
  UNDEFINED_LANGUAGE,
  ENGLISH,
  FRENCH,  
} language_t; 

void usage() {
  printf("\
Usage: inote -p <punct_mode> -t <text>\n\
Convert a text to a type length value byte buffer\n\
  -p punct_mode       punctuation mode; value from 0 to 2 (see inote_punct_mode_t in inote.h)\n\
  -t text             utf-8 text\n\
\n\
EXAMPLE:\n\
inote -p 0 -t \"Hello, world\" > tlv\n\
\n\
");
}

int main(int argc, char **argv)
{
  inote_slice_t text;
  inote_slice_t tlv_message;
  inote_state_t state;  
  size_t text_offset = 0;
  int punct_mode = -1;
  int opt;
  
  memset(&text, 0, sizeof(text));
  memset(&tlv_message, 0, sizeof(tlv_message));
  memset(&state, 0, sizeof(state));
  text.buffer = calloc(1, BUFFER_SIZE);
  *text.buffer = 0;
  
  while ((opt = getopt(argc, argv, "p:t:")) != -1) {
	switch (opt) {
	case 'p':
	  punct_mode = atoi(optarg);
	  break;
	case 't':
	  strncpy(text.buffer, optarg, BUFFER_SIZE-1);
	  text.buffer[BUFFER_SIZE-1] = 0;
	  break;
	default:
	  usage();
	  exit(1);
	  break;
	}
  }
  
  if ((punct_mode == -1) || (!*text.buffer)) {
	  usage();
	  exit(1);	
  }
  
  text.length = strlen(text.buffer);
  text.charset = INOTE_CHARSET_UTF_8;
  text.end_of_buffer = text.buffer + BUFFER_SIZE;

  //  state.punct_mode = INOTE_PUNCT_MODE_NONE;
  state.punct_mode = (inote_punct_mode_t)punct_mode;
  state.expected_lang = calloc(MAX_LANG, sizeof(*state.expected_lang));
  state.expected_lang[0] = ENGLISH;
  state.expected_lang[1] = FRENCH;
  state.max_expected_lang = MAX_LANG;
  state.ssml = 1;
    
  tlv_message.buffer = calloc(1, BUFFER_SIZE);
  tlv_message.end_of_buffer = tlv_message.buffer + BUFFER_SIZE;
  tlv_message.charset = INOTE_CHARSET_UTF_8;

  void *handle = inote_create();
  inote_get_annotated_text(handle, &text, &state, &tlv_message, &text_offset);  
  inote_delete(handle);
  write(STDOUT_FILENO, tlv_message.buffer, tlv_message.length);
}
