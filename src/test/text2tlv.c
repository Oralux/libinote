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
  -i inputfile          read text from file\n\
  -o outputfile         write tlv to this file\n\
  -p punct_mode         optional punctuation mode; value from 0 to 2 (see inote_punct_mode_t in inote.h)\n\
  -t text               text to convert to tlv\n\
  -c charset0:charset1  optional charsets: set0 = text charset, set1 = tlv charset. By Default: UTF-8.\n\
                        possible choices: ISO-8859-1, GBK, UCS-2, SJIS or UTF-8.\n\
\n\
EXAMPLE:\n\
text2tlv -p 0 -t \"Hello, world\" > tlv\n\
text2tlv -i file.txt -o file.tlv\n\
text2tlv -c ISO-8859-1:UTF-8 -i file.txt -o file.tlv\n\
\n\
");
}

static inote_charset_t getCharset(const char* s) {
  inote_charset_t ret = INOTE_CHARSET_UNDEFINED;

  if (!strcmp(s, "ISO-8859-1")) {
	ret = INOTE_CHARSET_ISO_8859_1;
  } else if (!strcmp(s, "GBK")) {
	ret = INOTE_CHARSET_GBK;
  } else if (!strcmp(s, "UCS-2")) {
	ret = INOTE_CHARSET_UCS_2;
  } else if (!strcmp(s, "SJIS")) {
	ret = INOTE_CHARSET_SJIS;
  } else {
	ret = INOTE_CHARSET_UTF_8;
  }
  
  return ret;
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
  inote_charset_t charset0 = INOTE_CHARSET_UTF_8;
  inote_charset_t charset1 = INOTE_CHARSET_UTF_8;
  
  memset(&text, 0, sizeof(text));
  memset(&tlv_message, 0, sizeof(tlv_message));
  memset(&state, 0, sizeof(state));

  text.buffer = text_buffer;
  *text.buffer = 0;
  
  while ((opt = getopt(argc, argv, "c:i:o:p:t:")) != -1) {
	switch (opt) {
	case 'c': {
	  char *x = strchr(optarg, ':');
	  if (!x) {
		usage();
		exit(1);
	  }
	  *x = 0;
	  charset0 = getCharset(optarg);
	  charset1 = getCharset(x+1);
	}
	  break;
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
	  text.length = strlen(text.buffer);
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


  text.charset = charset0;
  
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
  tlv_message.charset = charset1;

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
	  text.length = len;
	  text.charset = charset0;
	  text.end_of_buffer = text.buffer + len;	  
	  ret = inote_convert_text_to_tlv(handle, &text, &state, &tlv_message, &text_left);
	  switch (ret) {
	  case INOTE_INVALID_MULTIBYTE: {
		int ret2;
		text.length -= text_left;
		text.buffer[text.length] = ' '; // ignore this byte
		text.length++;
		fseek(fdi, -text_left+1, SEEK_CUR); // +1 for the space character
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
	  case INOTE_OK:
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
