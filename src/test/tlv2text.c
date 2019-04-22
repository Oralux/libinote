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
Usage: tlv2text -i inputfile -o outputfile\n\
Convert a type-length-value formatted file to text\n\
  -i inputfile    read tlv from file\n\
  -o outputfile   write text to this file\n\
\n\
EXAMPLE:\n\
tlv2text -i file.tlv -o file.tlv\n\
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
  
  while ((opt = getopt(argc, argv, "i:o:")) != -1) {
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
  cb.user_data = (void *)fdo;  

  inote_convert_tlv_to_text(&tlv_message, &cb);

  fclose(fdi);
  fclose(fdo);
  free(tlv_message.buffer);
  
  if (ret) {
	printf("%s: error = %d\n", __func__, ret);
  }

  return ret;  
}
