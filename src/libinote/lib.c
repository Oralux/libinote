#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <iconv.h>
#include <errno.h>
#include "inote.h"
#include "conf.h"
#include "debug.h"

static iconv_t my_conv_to_wchar;
static iconv_t my_conv_to_utf8;


uint32_t inote_get_annotated_text(struct input_t *input, struct state_t *state, struct output_t *output) {
  size_t bytes_in = 0;
  size_t bytes_out = 0;
  uint32_t a_status = 0;
  char *inbuf = NULL;
  char *outbuf = NULL;
  size_t inbytesleft = 0;
  size_t outbytesleft = 0;
  int ret;

  if (!input || !input->text || !input->max_size || (input->index >= input->max_size)
	  || !state
	  || !output || !output->buffer || !output->max_size)
	return 1;
  
  input->text[input->max_size - 1] = 0;
  bytes_in = strlen(input->text);
  if (!bytes_in) {
	dbg("LEAVE (%d)", __LINE__);
	output->length = 0;
	return 0;
  }
  
  my_conv_to_wchar = iconv_open("WCHAR_T", "UTF-8");
  if (my_conv_to_wchar == (iconv_t)-1) {
	ret = errno;
	dbg("Error iconv_open: from %s to %s (%s)", "WCHAR_T", "UTF-8", strerror(ret));
	a_status = 1;
	goto end0;
  }

  my_conv_to_utf8 = iconv_open("UTF-8", "WCHAR_T"); 
  if (my_conv_to_utf8 == (iconv_t)-1) {
	ret = errno;
	dbg("Error iconv_open: from %s to %s (%s)", "UTF-8", "WCHAR_T", strerror(ret));
	a_status = 1;
	goto end0;
  }
  
  inbuf = input->text;
  inbytesleft = bytes_in;
  outbuf = output->buffer;
  bytes_out = output->max_size;
  outbytesleft = output->max_size;

  ret = iconv(my_conv_to_wchar,
			  &inbuf, &inbytesleft,
			  &outbuf, &outbytesleft);
  if (ret == -1) {
	ret = errno;
  }
  
  if (inbytesleft || ret) {
	dbg("Failed to convert utf-8 to wchar_t, inbytesleft=%ld (%s)",  (long int)inbytesleft, ret ? strerror(ret) : "");
	a_status = 1;
	goto end0;
  }

  bytes_out -= outbytesleft;

  dbg("msg=%s, bytes_in=%ld, bytes_out=%ld", input->text, (long int)bytes_in, (long int)bytes_out);
	
  //  w_total_src = bytes_out/sizeof(wchar_t);


 end0:
  return a_status;
}
