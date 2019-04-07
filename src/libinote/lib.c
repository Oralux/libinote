#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <iconv.h>
#include <errno.h>
#include <wctype.h>
#include "inote.h"
#include "conf.h"
#include "debug.h"

#define ICONV_ERROR ((iconv_t)-1)
#define MAX_INPUT_BYTES 1024
#define MAX_WCHAR 1024
#define MAX_OUTPUT_BYTES (MAX_WCHAR*sizeof(wchar_t))
#define MAX_PUNCT 50
#define MAX_TOK 100
#define MAGIC 0x7E40B171

static const char* charset_name[] = {
	NULL,
	"ISO-8859-1//TRANSLIT",
	"GBK//TRANSLIT",
	"UCS2//TRANSLIT",
	"BIG5//TRANSLIT",
	"SJIS//TRANSLIT",
	"UTF8//TRANSLIT",
	"UTF16//TRANSLIT",
	"WCHAR_T//TRANSLIT",
};
#define MAX_CHARSET (sizeof(charset_name)/sizeof(*charset_name))

typedef struct {
  uint32_t magic;
  wchar_t wchar_buf[MAX_WCHAR];
  iconv_t cd_to_wchar[MAX_CHARSET];
  iconv_t cd_from_wchar[MAX_CHARSET];
  wchar_t punctuation_list[MAX_PUNCT];
  wchar_t token[MAX_TOK];
} handle_t;

static inline uint32_t get_charset(iconv_t *cd, const char *tocode, const char *fromcode) {
  uint32_t ret = 0;
  if (!cd)
	return EINVAL;
  
  if (*cd != ICONV_ERROR)
	return 0;

  *cd = iconv_open(tocode, fromcode);
  if (*cd == ICONV_ERROR) {
	ret = errno;
	dbg("Error iconv_open: from %s to %s (%s)", fromcode, tocode, strerror(ret));
  }
  return ret;
}

static inline uint32_t check_slice(const inote_slice_t *s) {
  return (s && s->buffer
		  && s->max_size && (s->length < s->max_size)
		  && (s->charset > INOTE_CHARSET_UNDEFINED) && (s->charset < MAX_CHARSET));
}

static uint32_t push_text(handle_t *h, const wchar_t *start, const wchar_t *end, inote_slice_t *tlv, const wchar_t **real_end) {
  size_t len;
  char *inbuf;
  size_t inbytesleft = 0;
  char *outbuf;
  size_t outbytesleft = 0;
  int ret;
  inote_tlv_t *header;

  if (!h || !start || !end || !check_slice(tlv) || !real_end || end < start) {
	dbg("EINVAL");
	return 1;
  }
  
  inbuf = (char *)start;
  inbytesleft = end - start;
  header = (inote_tlv_t *)(tlv->buffer + tlv->length);
  outbuf = (char *)header + sizeof(*header);
  outbytesleft = tlv->max_size - tlv->length;
  
  ret = iconv(h->cd_from_wchar[tlv->charset],
		&inbuf, &inbytesleft,
		&outbuf, &outbytesleft);
  if (!ret) {
	tlv->length = tlv->max_size - outbytesleft;
	*real_end = (wchar_t *)inbuf;
  } else if (errno == E2BIG) { // not sufficient room at output
	dbg("notice: not sufficient room at output");
	tlv->length = tlv->max_size - outbytesleft;
	*real_end = (wchar_t *)inbuf;
  } else {
	// EINVAL:
	// incomplete multibyte sequence in the input:
	// unexpected error, complete sequences are expected
	// EILSEQ:
	// invalid multibyte sequence in the input:
	// unexpected error thanks to //TRANSLIT
	ret = errno;
	dbg("unexpected error: %s", strerror(ret));
  }
  iconv(h->cd_from_wchar[tlv->charset], NULL, NULL, NULL, NULL);  
  return ret;
}

static inline uint32_t push_ssml(handle_t *h, wchar_t *start, wchar_t *end, inote_slice_t *tlv, const wchar_t **real_end) {
  return 0;
}

static inline uint32_t push_punct(handle_t *h, wchar_t *start, wchar_t *end, inote_slice_t *tlv, const wchar_t **real_end) {
  return 0;
}

static inline uint32_t push_annotation(handle_t *h, wchar_t *start, wchar_t *end, inote_slice_t *tlv, const wchar_t **real_end) {
  return 0;
}

// convert a wchar text to type_length_value format
static uint32_t get_type_length_value(handle_t *h, const inote_slice_t *text, inote_state_t *state, inote_slice_t *tlv) {
  wchar_t *start;
  size_t len;
  wchar_t *end;
  const wchar_t *real_end = NULL;
  wchar_t *t;
  int ret = 0;

  if (!h || !check_slice(text) || !state || !check_slice(tlv))
	return 1;

  start = (wchar_t *)text->buffer;
  len = text->length/sizeof(wchar_t);
  end = start + len;
  
  for (t=start; t<end; t++) {
	if (iswpunct(*t)) {
	  if (start != t) {
		push_text(h, start, t, tlv, &real_end);
	  }
	  switch(*t) {
	  case L'<':
		push_ssml(h, t, end, tlv, &real_end) && push_punct(h, t, end, tlv, &real_end) && push_text(h, t, end, tlv, &real_end);
		break;
	  case L'\'':
		push_annotation(h, t, end, tlv, &real_end) && push_punct(h, t, end, tlv, &real_end) && push_text(h, t, end, tlv, &real_end);
		break;
	  case L'&':
		push_text(h, t, end, tlv, &real_end);
		break;
	  default:
		push_punct(h, t, end, tlv, &real_end);
		break;
	  }
	}
  }
  
  return 0;
}

void *inote_create() {
  handle_t *h = (handle_t*)calloc(1, sizeof(handle_t));
  if (h) {
	int i;
	h->magic = MAGIC;
	for (i=0; i<MAX_CHARSET; i++) {
	  h->cd_to_wchar[i] = ICONV_ERROR;
	  h->cd_from_wchar[i] = ICONV_ERROR;
	}	
  }
  return h;
}

void inote_delete(void *handle) {
  handle_t *h;
  if (!handle)
	return;

  h = (handle_t*)handle;
  if (h->magic == MAGIC) {
	int i;
	for (i=0; i<MAX_CHARSET; i++) {
	  if (h->cd_to_wchar[i] != ICONV_ERROR) {
		iconv_close(h->cd_to_wchar[i]);
	  }
	  if (h->cd_from_wchar[i] != ICONV_ERROR) {
		iconv_close(h->cd_from_wchar[i]);
	  }
	}	
	memset(h, 0, sizeof(h));
	free(h);
  }
}

uint32_t inote_get_annotated_text(void *handle, const inote_slice_t *text, inote_state_t *state, inote_slice_t *tlv, size_t *input_offset) {
  uint32_t a_status = 0;
  inote_slice_t output;
  char *inbuf;
  size_t inbytesleft = 0;
  char *outbuf;
  size_t outbytesleft = 0;

  handle_t *h;
  int ret;
  
  if (!handle || ( (h=(handle_t*)handle)->magic != MAGIC)) {
	dbg("Args error (%p, %d)", handle, __LINE__);
	return 1;
  }
  if (!check_slice(text) || !check_slice(tlv)) {
	dbg("Args error (%p, %d)", text, __LINE__);
	return 1;
  }
  if (!state)
	return 1;
  
  if (!text->length) {
	dbg("LEAVE (%d)", __LINE__);
	tlv->length = 0;
	return 0;
  }

  output.buffer = (char *)h->wchar_buf;
  output.length = 0;
  output.charset = INOTE_CHARSET_WCHAR_T;
  output.max_size = sizeof(h->wchar_buf);
  
  if (get_charset(&h->cd_to_wchar[text->charset], "WCHAR_T", charset_name[text->charset])
	  || get_charset(&h->cd_from_wchar[tlv->charset], charset_name[tlv->charset], "WCHAR_T"))  {
	a_status = 1;
	goto end0;
  }
  
  inbuf = text->buffer;
  inbytesleft = text->length;
  outbuf = output.buffer;
  outbytesleft = output.max_size;

  ret = -1;
  while (ret) {
	ret = iconv(h->cd_to_wchar[text->charset],
				&inbuf, &inbytesleft,
				&outbuf, &outbytesleft);
	if (!ret) {
	  output.length = output.max_size - outbytesleft;
	  get_type_length_value(h, &output, state, tlv);
	} else if (errno == E2BIG) { // not sufficient room at output
	  output.length = output.max_size - outbytesleft;
	  get_type_length_value(h, &output, state, tlv);
	  output.length = 0;
	  outbuf = output.buffer;
	  outbytesleft = output.max_size;
	} else {
	  // EINVAL:
	  // incomplete multibyte sequence in the input:
	  // unexpected error, complete sequences are expected
	  // EILSEQ:
	  // invalid multibyte sequence in the input:
	  // unexpected error thanks to //TRANSLIT
		ret = 0;
	}
  }
  // initial state
  ret = iconv(h->cd_to_wchar[text->charset], NULL, NULL, NULL, NULL);
  
  
  if (inbytesleft) {
	dbg("Failed to convert inbytesleft=%ld bytes",  (long int)inbytesleft);
	a_status = 1;
  }

 end0:
  return a_status;
}

