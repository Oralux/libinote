// --> For strdup, getopt
#define _POSIX_C_SOURCE 200809L
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

static char *prefix_capital;
static char *prefix_capitals;

void usage() {
  printf("\
Usage: tlv2text -i inputfile -o outputfile\n\
Convert a type-length-value formatted file to text\n\
  -i inputfile    read tlv from file\n\
  -o outputfile   write text to this file\n\
  -c capital      optional word to insert when a capital is detected.\n\
                  spaces will be added around his word.\n\
                  #n will be appended in case of several capitals.\n\
\n\
EXAMPLE:\n\
tlv2text -i file.tlv -o file.tlv -c beep\n\
\n\
");
}

inote_error add_text(inote_tlv_t *tlv, void *user_data) {
  FILE *fdo = (FILE*)user_data;
  uint8_t *t = inote_tlv_get_value(tlv);
  inote_error ret = INOTE_OK;

  if (t && fdo) {
    if (1 != fwrite(t, tlv->length, 1, fdo)) {
      printf("%s: write error\n", __func__);
      ret = INOTE_IO_ERROR;
    }
  }
  
  return ret;
}

inote_error add_capital(inote_tlv_t *tlv, bool capitals, void *user_data) {
  FILE *fdo = (FILE*)user_data;
  inote_error ret = INOTE_OK;
  const char *prefix = capitals ? prefix_capitals : prefix_capital; 

  if (!fdo || !prefix) {
    printf("%s: args error\n", __func__);
    return INOTE_IO_ERROR;
  }
  
  fwrite(prefix, strlen(prefix), 1, fdo);
  
  return add_text(tlv, user_data);
}

int main(int argc, char **argv)
{
  int opt; 
  FILE *fdi = NULL;
  FILE *fdo = NULL;
  int ret = 0;
  inote_slice_t tlv_message;
  struct stat statbuf;
  inote_cb_t cb;
  void *data = NULL;

  prefix_capital = strdup("");
  prefix_capitals = strdup("");
  
  while ((opt = getopt(argc, argv, "i:o:c:")) != -1) {
    switch (opt) {
    case 'i':
      if (fdi)
	fclose(fdi);
      fdi = fopen(optarg, "r");
      if (!fdi) {
	perror(NULL);
	exit(1);
      }
      if (stat(optarg, &statbuf)) {
	perror(NULL);
	exit(1);
      }
      break;
    case 'o':
      if (fdo)
	fclose(fdo);
      fdo = fopen(optarg, "w");
      if (!fdo) {
	perror(NULL);
	exit(1);
      }
      break;
    case 'c':
      {
	size_t len = strlen(optarg);
	free(prefix_capital);
	free(prefix_capitals);
	prefix_capital = calloc(1, len+10);
	prefix_capitals = calloc(1, len+10);
	if (len) {
	  snprintf(prefix_capital, len+10, " %s ", optarg);
	  snprintf(prefix_capitals, len+10, " %s#n ", optarg);
	}
      }
      break;
    default:
      usage();
      exit(1);
      break;
    }
  }
  
  if (!fdi || !fdo) {
    usage();
    exit(1);	
  }
  
  tlv_message.buffer = calloc(1, statbuf.st_size);
  tlv_message.length = statbuf.st_size;
  tlv_message.charset = INOTE_CHARSET_UNDEFINED;
  tlv_message.end_of_buffer = tlv_message.buffer + tlv_message.length;

  if (1 != fread(tlv_message.buffer, tlv_message.length, 1, fdi)) {
    printf("%s: read error\n", __func__);
    exit(1);
  }
  
  cb.add_annotation = add_text;
  cb.add_charset = add_text;
  cb.add_punctuation = add_text;
  cb.add_text = add_text;  
  cb.add_capital = add_capital;  
  cb.user_data = (void *)fdo;  

  inote_convert_tlv_to_text(&tlv_message, &cb);

  fclose(fdi);
  fclose(fdo);
  free(tlv_message.buffer);
  free(prefix_capital);
  free(prefix_capitals);
  
  if (ret) {
    printf("%s: error = %d\n", __func__, ret);
  }

  return ret;  
}
/* local variables: */
/* c-basic-offset: 2 */
/* end: */
