// --> For getopt
#define _XOPEN_SOURCE 1
// <--
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "inote.h"

#define MAX_LANG 2

enum {
  UNDEFINED_LANGUAGE,
  ENGLISH,
  FRENCH,  
} language_t; 

void usage() {
  printf("\
Usage: text2tlv [-p <punct_mode>] [-i inputfile | -t <text>] [-o outputfile]\n\
Convert a text to a type-length-value byte buffer\n\
  -i inputfile    read text from file\n\
  -o outputfile   write tlv to this file\n\
  -p punct_mode   optional punctuation mode; value from 0 to 2 (see inote_punct_mode_t in inote.h)\n\
  -t text         utf-8 text\n\
\n\
EXAMPLE:\n\
text2tlv -p 0 -t \"Hello, world\" > tlv\n\
text2tlv -i file.txt -o file.tlv\n\
\n\
");
}

int main(int argc, char **argv)
{
  inote_slice_t text;
  inote_slice_t tlv_message;
  inote_state_t state;  
  size_t text_left = 0;
  int punct_mode = 0;
  int opt; 
  FILE *fdi = NULL;
  int output = STDOUT_FILENO;
  int ret = 0;
  uint8_t text_buffer[TEXT_LENGTH_MAX+1];
  uint32_t state_expected_lang[MAX_LANG];
  uint8_t tlv_message_buffer[TLV_MESSAGE_LENGTH_MAX];
  
  memset(&text, 0, sizeof(text));
  memset(&tlv_message, 0, sizeof(tlv_message));
  memset(&state, 0, sizeof(state));

  text.buffer = text_buffer;
  *text.buffer = 0;
  
  while ((opt = getopt(argc, argv, "i:o:p:t:")) != -1) {
	switch (opt) {
	case 'i':
	  if (fdi)
		fclose(fdi);
	  fdi = fopen(optarg, "r");
	  if (!fdi) {
		perror(NULL);
		exit(1);
	  }
	  break;
	case 'o':
	  output = creat(optarg, S_IRWXU);
	  if (output==-1) {
		perror(NULL);
		exit(1);
	  }
	  break;
	case 'p':
	  punct_mode = atoi(optarg);
	  break;
	case 't':
	  strncpy(text.buffer, optarg, TEXT_LENGTH_MAX);
	  text.buffer[TEXT_LENGTH_MAX] = 0;
	  text.length = strlen(text.buffer);
	  text.charset = INOTE_CHARSET_UTF_8;
	  text.end_of_buffer = text.buffer + TEXT_LENGTH_MAX;	  
	  break;
	default:
	  usage();
	  exit(1);
	  break;
	}
  }
  
  if (!*text.buffer && !fdi) {
	  usage();
	  exit(1);	
  }
  
  //  state.punct_mode = INOTE_PUNCT_MODE_NONE;
  state.punct_mode = (inote_punct_mode_t)punct_mode;
  state.expected_lang = state_expected_lang;
  state.expected_lang[0] = ENGLISH;
  state.expected_lang[1] = FRENCH;
  state.max_expected_lang = MAX_LANG;
  //  state.ssml = 0;
  state.annotation = 1;
    
  tlv_message.buffer = tlv_message_buffer;
  tlv_message.end_of_buffer = tlv_message.buffer + TLV_MESSAGE_LENGTH_MAX;
  tlv_message.charset = INOTE_CHARSET_UTF_8;

  void *handle = inote_create();
  if (!fdi) {
	ret = inote_convert_text_to_tlv(handle, &text, &state, &tlv_message, &text_left);
	switch (ret) {
	case INOTE_INCOMPLETE_MULTIBYTE:
	case INOTE_INVALID_MULTIBYTE:
	  text.length -= text_left;
	  ret = inote_convert_text_to_tlv(handle, &text, &state, &tlv_message, &text_left);
	  break;
	default:
	  break;
	}
  } else {
	bool loop = true;
	while(loop) {
	  size_t len = fread(text.buffer, 1, TEXT_LENGTH_MAX, fdi);
	  if (!len)
		break;
	  text.buffer[len] = 0;
	  text.length = len;
	  text.charset = INOTE_CHARSET_UTF_8;
	  text.end_of_buffer = text.buffer + len;	  
	  ret = inote_convert_text_to_tlv(handle, &text, &state, &tlv_message, &text_left);
	  switch (ret) {
	  case INOTE_INVALID_MULTIBYTE: {
		int ret2;
		text.length -= text_left;
		text.buffer[text.length] = '?';
		text.length++;
		fseek(fdi, -text_left+1, SEEK_CUR);
		ret2 = inote_convert_text_to_tlv(handle, &text, &state, &tlv_message, &text_left);
		loop = (!ret2);
	  }
		break;
	  case INOTE_INCOMPLETE_MULTIBYTE: {
		int ret2;
		text.length -= text_left;
		fseek(fdi, -text_left, SEEK_CUR);
		ret2 = inote_convert_text_to_tlv(handle, &text, &state, &tlv_message, &text_left);
		loop = (!ret2);
	  }
		break;
	  default:
		loop = false;
		break;
	  }
	  write(output, tlv_message.buffer, tlv_message.length);
	  tlv_message.length = 0;
	}
	fclose(fdi);
  }
  if (ret) {
	printf("%s: error = %d\n", __func__, ret);
  }
  inote_delete(handle);
  write(output, tlv_message.buffer, tlv_message.length);
  tlv_message.length = 0;

  return ret;
}
