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

#define TLV_IS_TEXT(header) (header && (header->type1 == INOTE_TYPE_TEXT))
#define TLV_SET_TEXT(header, charset) if (header) { header->type1 = INOTE_TYPE_TEXT; header->type2 = charset;}

static void TLV_ADD_LENGTH(inote_tlv_t *header, uint16_t length) {
  if (header) {
	uint16_t len = length + header->length1 + ((header->length2)<<8);
	header->length1 = (len & 0xff);
	header->length2 = (len >> 8);
  }
}

static uint32_t get_charset(iconv_t *cd, const char *tocode, const char *fromcode) {
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

static uint32_t check_slice(const inote_slice_t *s) {
  return (s && s->buffer
		  && s->max_size && (s->length <= s->max_size)
		  && (s->charset > INOTE_CHARSET_UNDEFINED) && (s->charset < MAX_CHARSET));
}

static size_t min_size(size_t a, size_t b) {
  return (a<b) ? a : b;
}

static uint32_t tlv_new(inote_slice_t *tlv_message, inote_type_t type1, uint8_t type2, inote_tlv_t **header) {
  uint32_t status = EINVAL;

  if (tlv_message
	  && (tlv_message->length + sizeof(*header) <= tlv_message->max_size)
	  && header) {
	*header = (inote_tlv_t *)(tlv_message->buffer + tlv_message->length);
	(*header)->type1 = type1;
	(*header)->type2 = type2;
	(*header)->length1 = (*header)->length2 = 0;
	tlv_message->length += sizeof(inote_tlv_t);
	status = 0;
  }
  
  return status;
}

static uint32_t push_text(handle_t *h, inote_slice_t *segment, inote_state_t *state, inote_slice_t *tlv_message, inote_tlv_t **tlv_last) {
  char *outbuf;
  size_t outbytesleft, max_outbytesleft = 0;
  int ret = 0;
  int err = 0;
  inote_tlv_t *header;
  wchar_t *t;
  wchar_t *tmax;
  
  if (!h || !check_slice(segment) || !check_slice(tlv_message) || !tlv_last ) {
	dbg1("EINVAL");
	return EINVAL;
  }

  header = *tlv_last;
  if (!header || (header->type1 != INOTE_TYPE_TEXT)) {
	tlv_new(tlv_message, INOTE_TYPE_TEXT, tlv_message->charset, &header);
	*tlv_last = header;
  }
  if (!header) {
	dbg1("out of tlv");
	return ENOMEM;
  }  

  /* collect the full text segment   */
  t = (wchar_t *)(segment->buffer);
  tmax = (wchar_t *)(segment->buffer + segment->max_size);

  while ((t < tmax) && !iswpunct(*t)) {
	t++;
  }

  segment->length = (uint8_t*)t - segment->buffer;
  outbuf = (char *)header + sizeof(*header);
  max_outbytesleft = outbytesleft = min_size(tlv_message->max_size - tlv_message->length, MAX_TLV_LENGTH);
  
  ret = iconv(h->cd_from_wchar[tlv_message->charset],
			  (char**)&segment->buffer, &segment->length,
			  &outbuf, &outbytesleft);
  err = errno;
  if (!ret || (err == E2BIG)) {
	tlv_message->length += max_outbytesleft - outbytesleft;
	TLV_ADD_LENGTH(header, max_outbytesleft - outbytesleft);
	if (err) { /* not sufficient room at output */
	  dbg1("notice: not sufficient room at output");
	}
  } else {
	/* 
	   EINVAL: 
	   incomplete multibyte sequence in the input:
	   unexpected error, complete sequences are expected
	   EILSEQ:
	   invalid multibyte sequence in the input:
	   unexpected error thanks to //TRANSLIT
	*/
	ret = errno;
	dbg("unexpected error: %s", strerror(ret));
  }
  iconv(h->cd_from_wchar[tlv_message->charset], NULL, NULL, NULL, NULL);
  segment->max_size = (uint8_t*)tmax - segment->buffer;
  state->type = INOTE_TYPE_UNDEFINED;
  return ret;
}

/* TODO: ssml parser */
/* Currently any tag is simply filtered. */
static uint32_t push_tag(handle_t *h, inote_slice_t *segment, inote_state_t *state) {
  wchar_t *t;
  wchar_t *tmax;

  if (!state->ssml)
  	return 1;

  if (!h || !check_slice(segment)) {
  	dbg1("EINVAL");
  	return EINVAL;
  }

  /* collect the full tag segment   */
  t = (wchar_t *)(segment->buffer);
  tmax = (wchar_t *)(segment->buffer + segment->max_size);

  while ((t < tmax) && (*t != L'>')) {
	t++;
  }

  segment->buffer = (t < tmax) ? (uint8_t*)(t + 1) : (uint8_t*)tmax;
  segment->length = 0;
  segment->max_size = (uint8_t*)tmax - segment->buffer;
  state->type = INOTE_TYPE_UNDEFINED;

  return 0;
}

static uint32_t push_punct(handle_t *h, inote_slice_t *segment, inote_state_t *state, inote_slice_t *tlv_message, inote_tlv_t **tlv_last) {
  int ret = 0;
  wchar_t *t;
  wchar_t *tmax;
  inote_tlv_t *header;

  if (!h || !check_slice(segment) || !check_slice(tlv_message) || !tlv_last ) {
	dbg1("EINVAL");
	return EINVAL;
  }

  t = (wchar_t *)(segment->buffer);
  tmax = (wchar_t *)(segment->buffer + segment->max_size);
  
  if (state->punct_mode == INOTE_PUNCT_MODE_NONE) {
	if (t < tmax) {	  
	  header = *tlv_last;
	  if (!header || (header->type1 != INOTE_TYPE_TEXT)) {
		tlv_new(tlv_message, INOTE_TYPE_TEXT, tlv_message->charset, &header);
		*tlv_last = header;
	  }
	  
	  if (!header) {
		dbg1("out of tlv");
		return ENOMEM;
	  }  

	  if (tlv_message->length >= tlv_message->max_size) {
		dbg1("notice: not sufficient room at output");
	  } else {
		tlv_message->buffer[tlv_message->length] = *(uint8_t*)t; // punctuation is expected to be an ascii char 
		tlv_message->length += 1;
		TLV_ADD_LENGTH(header, 1);
		segment->buffer = (uint8_t*)(t + 1);
		segment->length = 1;
		segment->max_size -= sizeof(*t);
		ret = push_text(h, segment, state, tlv_message, tlv_last);
	  }
	} else {
	  segment->buffer = (uint8_t*)tmax;
	  segment->length = 0;	  
	}
  }

  return ret;
}

static uint32_t push_annotation(handle_t *h, inote_slice_t *segment, inote_state_t *state, inote_slice_t *tlv_message, inote_tlv_t **tlv_last) {
  if (!state->annotation)
	return 1;

  return 0;
}

static uint32_t push_entity(handle_t *h, inote_slice_t *segment, inote_state_t *state, inote_slice_t *tlv_message, inote_tlv_t **tlv_last) {
  if (!state->entity)
	return 1;
  return 0;
}

/* convert a wchar text to type_length_value format */
static uint32_t get_type_length_value(handle_t *h, const inote_slice_t *text, inote_state_t *state, inote_slice_t *tlv_message) {
  inote_tlv_t *tlv_last = NULL; /* current tlv */
  wchar_t *tmax;
  inote_slice_t segment;
  int ret = 1;
  
  if (!h || !check_slice(text) || !state || !check_slice(tlv_message))
	return 1;

  memcpy(&segment, text, sizeof(*text));
  state->type = INOTE_TYPE_UNDEFINED;
  segment.max_size = text->length;
  segment.length = 0;

  tmax = (wchar_t *)(segment.buffer + segment.max_size);  
  while ((wchar_t *)segment.buffer < tmax) {
	ret = 1;
	if (iswpunct(*(wchar_t *)segment.buffer)) {
	  switch(*(wchar_t *)segment.buffer) {
	  case L'<':
		ret = push_tag(h, &segment, state);
		break;
	  case L'\'':
		ret = push_annotation(h, &segment, state, tlv_message, &tlv_last);
		break;
	  case L'&':
		ret = push_entity(h, &segment, state, tlv_message, &tlv_last);
		break;
	  default:
		break;
	  }
	  if (ret) {
		ret = push_punct(h, &segment, state, tlv_message, &tlv_last);
	  }
	}
	if (ret) {
	  ret = push_text(h, &segment, state, tlv_message, &tlv_last);
	  if (ret) {
		dbg1("TODO");
	  }
	}
  };
  
  return ret;
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
	memset(h, 0, sizeof(*h));
	free(h);
  }
}

uint32_t inote_get_annotated_text(void *handle, const inote_slice_t *text, inote_state_t *state, inote_slice_t *tlv_message, size_t *input_offset) {
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
  if (!check_slice(text) || !check_slice(tlv_message)) {
	dbg("Args error (%p, %d)", (void*)text, __LINE__);
	return 1;
  }  
  if (!state)
	return 1;
  
  if (!text->length) {
	dbg("LEAVE (%d)", __LINE__);
	tlv_message->length = 0;
	return 0;
  }

  output.buffer = (uint8_t*)h->wchar_buf;
  output.length = 0;
  output.charset = INOTE_CHARSET_WCHAR_T;
  output.max_size = sizeof(h->wchar_buf);
  
  if (get_charset(&h->cd_to_wchar[text->charset], "WCHAR_T", charset_name[text->charset])
	  || get_charset(&h->cd_from_wchar[tlv_message->charset], charset_name[tlv_message->charset], "WCHAR_T"))  {
	a_status = 1;
	goto end0;
  }
  
  inbuf = (char *)(text->buffer);
  inbytesleft = text->length;
  outbuf = (char *)(output.buffer);
  outbytesleft = output.max_size;

  ret = -1;
  while (ret) {
	ret = iconv(h->cd_to_wchar[text->charset],
				&inbuf, &inbytesleft,
				&outbuf, &outbytesleft);
	if (!ret) {
	  output.length = output.max_size - outbytesleft;
	  get_type_length_value(h, &output, state, tlv_message);
	} else if (errno == E2BIG) { /* not sufficient room at output */
	  output.length = output.max_size - outbytesleft;
	  get_type_length_value(h, &output, state, tlv_message);
	  output.length = 0;
	  outbuf = (char *)(output.buffer);
	  outbytesleft = output.max_size;
	} else {
	  /* 
		 EINVAL: 
		 incomplete multibyte sequence in the input:
		 unexpected error, complete sequences are expected
		 EILSEQ:
		 invalid multibyte sequence in the input:
		 unexpected error thanks to //TRANSLIT
	  */
		ret = 0;
	}
  }
  /* initial state */
  ret = iconv(h->cd_to_wchar[text->charset], NULL, NULL, NULL, NULL);
    
  if (inbytesleft) {
	dbg("Failed to convert inbytesleft=%ld bytes",  (long int)inbytesleft);
	a_status = 1;
  }

  DebugDump("tlv: ", tlv_message->buffer, min_size(tlv_message->length, 256));
  
 end0:
  return a_status;
}

