#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <iconv.h>
#include <errno.h>
#include <wctype.h>
#include <wchar.h>
#include "inote.h"
#include "conf.h"
#include "debug.h"

#define ICONV_ERROR ((iconv_t)-1)
#define MAX_INPUT_BYTES 1024
#define MAX_WCHAR (TEXT_LENGTH_MAX*sizeof(wchar_t))
#define MAX_PUNCT 50
#define MAX_TOK 100
#define MAGIC 0x7E40B171
#define TLV_VALUE_LENGTH_THRESHOLD 16

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
	const wchar_t *str;
	wchar_t c;
	int l;
} predef_t;

static const predef_t xml_predefined_entity[] = {
	{L"&quot;", L'"',6},
	{L"&amp;", L'&',5},
	{L"&apos;", L'\'',6},
	{L"&lt;", L'<',4},
	{L"&gt;", L'>',4},
};
#define MAX_ENTITY_NB (sizeof(xml_predefined_entity)/sizeof(xml_predefined_entity[0]))

typedef struct {
  uint32_t magic;
  wchar_t wchar_buf[MAX_WCHAR];
  iconv_t cd_to_wchar[MAX_CHARSET];
  iconv_t cd_from_wchar[MAX_CHARSET];
  wchar_t punctuation_list[MAX_PUNCT];
  wchar_t token[MAX_TOK];
  uint32_t count;
} inote_t;

typedef struct {
  inote_type_t type;
  inote_slice_t s; // slice on a valid text buffer
} segment_t;

typedef struct {
  inote_slice_t *s;
  inote_tlv_t *header;
  inote_tlv_t *previous_header;
} tlv_t;

typedef enum {
  _INOTE_OK=INOTE_OK,
  _INOTE_ARGS_ERROR=INOTE_ARGS_ERROR,
  _INOTE_CHARSET_ERROR=INOTE_CHARSET_ERROR,
  _INOTE_INTERNAL_ERROR=INOTE_INTERNAL_ERROR,
  _INOTE_TLV_MESSAGE_FULL, // no other tlv can be added to tlv_message
  _INOTE_TLV_FULL, // the current tlv is full
  _INOTE_UNPROCESSED,
  _INOTE_UNEMPTIED_BUFFER, // the internal wchar_t buffer can't be fully processed
} _inote_error;

static size_t min_size(size_t a, size_t b) {
  return (a<b) ? a : b;
}

static bool slice_check(const inote_slice_t *self) {
  return (self && self->buffer
		  && (self->buffer + self->length <= self->end_of_buffer)
		  && (self->charset > INOTE_CHARSET_UNDEFINED) && (self->charset < MAX_CHARSET));
}

static size_t slice_get_free_size(const inote_slice_t *self) {
  size_t s = 0;
  if (self && self->buffer) {
	uint8_t *free_byte = self->buffer + self->length;
	if (free_byte < self->end_of_buffer) {
	  s = self->end_of_buffer - free_byte;
	}
  }
  return s;
}

static uint8_t *slice_get_free_byte(const inote_slice_t *self) {
  uint8_t *free_byte = NULL;
  if (self && self->buffer) {
	free_byte = self->buffer + self->length;
	if (free_byte >= self->end_of_buffer) {
	  free_byte = NULL;
	}
  }
  return free_byte;
}

static _inote_error segment_init(segment_t *self, const inote_slice_t *text) {
  _inote_error ret = _INOTE_ARGS_ERROR;  
  if (self && text) {
	inote_slice_t *s = &self->s;
	memset(self, 0, sizeof(*self));
	memcpy(s, text, sizeof(*text));
	self->type = INOTE_TYPE_UNDEFINED;
	s->end_of_buffer = text->buffer + text->length;
	s->length = 0;
	ret = _INOTE_OK;
  }
  return ret;
}

static wchar_t *segment_get_buffer(segment_t *self) {
  wchar_t* buffer = NULL;
  if (self && self->s.buffer) {
	buffer = (wchar_t*)(self->s.buffer);  
  }
  return buffer;
}

static wchar_t *segment_get_max(segment_t *self) {
  wchar_t* max = NULL;
  if (self && self->s.buffer) {
	max = (wchar_t*)(self->s.end_of_buffer);  
  }
  return max;
}

static void segment_erase(segment_t *self, uint8_t* buffer) {
  if (self && (self->s.buffer <= buffer) && (buffer <= self->s.end_of_buffer)) {
	if (self->s.buffer + self->s.length <= buffer) {
	  self->s.length = 0 ;
	} else {
	  self->s.length = buffer - self->s.buffer;
	}
	self->s.buffer = buffer;
  }
}

/* static segment_t *segment_next(segment_t *self, inote_type_t type) { */
/*   if (!self) { */
/* 	return NULL; */
/*   } */

/*   if ((type == self->type) && */
/* 	  ((self->type == INOTE_TYPE_TEXT) || (self->type == INOTE_TYPE_UNDEFINED))) { */
/* 	return self; // keep current segment */
/*   }   */

/*   self->type = type; */
/*   self->s.length = 0; */

/*   return self; */
/* } */

static _inote_error tlv_init(tlv_t *self, inote_slice_t *tlv_message) {
  int ret = _INOTE_ARGS_ERROR;  
  if (self && tlv_message) {
	inote_tlv_t *header = (inote_tlv_t *)(tlv_message->buffer + tlv_message->length);
	memset(header, 0, sizeof(*header));
	memset(self, 0, sizeof(*self));
	self->s = tlv_message;
	ret = _INOTE_OK;
  }
  return ret;
}

static tlv_t *tlv_next(tlv_t *self, inote_type_t type) {
  tlv_t *next = NULL;
  inote_tlv_t *header;
  uint8_t *free_byte = NULL;
  inote_slice_t *s = NULL;
  
  if (!self || !self->s || (type == INOTE_TYPE_UNDEFINED)) {
	return NULL;
  }
  
  header = self->header;
  if (header) {
	if (header->type == INOTE_TYPE_UNDEFINED) {
	  header->length = 0;
	  return self;
	}
	if ((type == header->type) && (header->type == INOTE_TYPE_TEXT)
		&& (header->length < TLV_VALUE_LENGTH_MAX-TLV_VALUE_LENGTH_THRESHOLD)) {
	  return self;
	}
  }  

  s = self->s;
  free_byte = s->buffer + s->length;
  if (free_byte + TLV_LENGTH_MAX <= s->end_of_buffer) {
	self->previous_header = header;
	header = (inote_tlv_t *)free_byte;
	header->type = type;
	header->length = 0;
	s->length += TLV_HEADER_LENGTH_MAX; // used bytes: only header at this stage
	self->header = header;
	next = self;
  } else {
	dbg1("out of tlv");	
  }
  
  return next;
}

static _inote_error tlv_add_length(tlv_t *self, uint16_t *length) {
  inote_slice_t *s;
  inote_tlv_t *header;
  uint8_t len;

  if (!self || !self->header || !self->s || !length) {
	dbg1("_INOTE_ERROR_ARGS");		 
	return _INOTE_ARGS_ERROR;
  }
	  
  s = self->s;
  if (slice_get_free_size(s) < *length) {
	dbg1("_INOTE_TLV_MESSAGE_FULL");		 	
	return _INOTE_TLV_MESSAGE_FULL;
  }
  
  header = self->header;
  len = min_size(*length, TLV_VALUE_LENGTH_MAX - header->length);
  if (!len) {
	dbg1("_INOTE_TLV_FULL");		 	
	return _INOTE_TLV_FULL;
  }
  *length -= len;
  header->length += len;
  self->s->length += len;
  return _INOTE_OK;
}

static uint8_t *tlv_get_free_byte(tlv_t *self) {
  return (self) ? slice_get_free_byte(self->s) : NULL;
}

// get free size in the current tlv
static size_t tlv_get_free_size(tlv_t *self) {
  return (self && self->header) ? TLV_VALUE_LENGTH_MAX - self->header->length : 0;
}

static _inote_error get_charset(iconv_t *cd, const char *tocode, const char *fromcode) {
  uint32_t ret = 0;
  if (!cd)
	return _INOTE_ARGS_ERROR;
  
  if (*cd != ICONV_ERROR)
	return _INOTE_OK;

  *cd = iconv_open(tocode, fromcode);
  if (*cd == ICONV_ERROR) {
	int status = errno;
	dbg("Error iconv_open: from %s to %s (%s)", fromcode, tocode, strerror(status));
	ret = _INOTE_CHARSET_ERROR;
  }
  return ret;
}

static _inote_error inote_push_text(inote_t *self, inote_type_t first, segment_t *segment, inote_state_t *state, tlv_t *tlv) {
  char *outbuf;
  size_t outbytesleft, max_outbytesleft = 0;
  _inote_error ret = _INOTE_OK;  
  int status;
  int err = 0;
  wchar_t *t, *t0, *tmax;
  
  if (!self || !segment || !tlv) {
	dbg1("_INOTE_ARGS_ERROR");
	return _INOTE_ARGS_ERROR;
  }

  tlv = tlv_next(tlv, first);
  if (!tlv) {
	return _INOTE_TLV_MESSAGE_FULL;
  }

  t = t0 = segment_get_buffer(segment);
  tmax = segment_get_max(segment);
  // the first char is considered as text
  t++;
  if (first == INOTE_TYPE_ANNOTATION) {
	while ((t < tmax) && (*t != L' ')) {
	  t++;
	}
	t++; // + white space
  } else {
	while ((t < tmax) && !iswpunct(*t)) {
	  t++;
	}
  }

  segment->s.length = (uint8_t*)t - (uint8_t*)t0;
  outbuf = (char*)tlv_get_free_byte(tlv);
  max_outbytesleft = outbytesleft = tlv_get_free_size(tlv);
  
  status = iconv(self->cd_from_wchar[tlv->s->charset],
				 (char**)&segment->s.buffer, &segment->s.length,
				 &outbuf, &outbytesleft);
  err = errno;
  if (!status || (err == E2BIG)) {
	uint16_t length = max_outbytesleft - outbytesleft;
	ret = tlv_add_length(tlv, &length);
  } else {
	/* 
	   EINVAL: 
	   incomplete multibyte sequence in the input:
	   unexpected error, complete sequences are expected
	   EILSEQ:
	   invalid multibyte sequence in the input:
	   unexpected error thanks to //TRANSLIT
	*/
	dbg("unexpected error: %s", strerror(err));
	ret = _INOTE_ARGS_ERROR;
  }
  iconv(self->cd_from_wchar[tlv->s->charset], NULL, NULL, NULL, NULL);
  return ret;
}

/* TODO: ssml parser */
/* Currently any tag is simply filtered. */
static _inote_error inote_push_tag(inote_t *self, segment_t *segment, inote_state_t *state, tlv_t *tlv) {
  wchar_t *t, *tmax;

  if (!state->ssml)
  	return _INOTE_UNPROCESSED;

  if (!self || !segment || !tlv) {
  	dbg1("_INOTE_ARGS_ERROR");
  	return _INOTE_ARGS_ERROR;
  }

  t = segment_get_buffer(segment);
  tmax = segment_get_max(segment);
  while ((t < tmax) && (*t != L'>')) {
	t++;
  }

  segment_erase(segment, (uint8_t*)(t+1));
  return _INOTE_OK;
}

static _inote_error inote_push_punct(inote_t *self, segment_t *segment, inote_state_t *state, tlv_t *tlv) {
  int ret = 0;
  wchar_t *t, *tmax;
  bool signal_punctuation = false;
  
  if (!self || !segment || !tlv) {
	dbg1("_INOTE_ARGS_ERROR");
	return _INOTE_ARGS_ERROR;
  }

  t = segment_get_buffer(segment);
  tmax = segment_get_max(segment);
  
  switch (state->punct_mode) {
  case INOTE_PUNCT_MODE_SOME: {
	int i;
	for (i=0; i<MAX_PUNCT && self->punctuation_list[i]; i++) {
	  if (self->punctuation_list[i] == *t) {
		signal_punctuation = true;
		break;
	  }
	}
  }	  
	break;
  case INOTE_PUNCT_MODE_ALL:
	signal_punctuation = true;
	break;
  default:
	break;
  }

  ret = _INOTE_UNPROCESSED;
  if (!signal_punctuation) {
	ret = inote_push_text(self, INOTE_TYPE_TEXT, segment, state, tlv);	  
  } else if (t < tmax) {
	ret = inote_push_text(self, INOTE_TYPE_PUNCTUATION, segment, state, tlv);	  
  }

  return ret;
}

static _inote_error inote_push_annotation(inote_t *self, segment_t *segment, inote_state_t *state, tlv_t *tlv) {
  wchar_t *t0, *t, *tmax;  
  size_t len;
  inote_type_t first = INOTE_TYPE_UNDEFINED;
  int ret = _INOTE_OK;
  
  if (!state->annotation)
	return _INOTE_UNPROCESSED;

  if (!self || !segment || !tlv) {
  	dbg1("_INOTE_ARGS_ERROR");
  	return _INOTE_ARGS_ERROR;
  }

  t0 = t = segment_get_buffer(segment);
  tmax = segment_get_max(segment);
  while ((t < tmax) && (*t != L' ')) {
	t++;
  }

  if (t >= tmax) {
	ret = _INOTE_UNPROCESSED;
	goto exit0;
  }	
  
  if (!wcsncmp(t0, L"`gfa1 ", 6)) {
	state->ssml = 1;
	goto exit0;
  }

  if (!wcsncmp(t0, L"`gfa2 ", 6)) {
	// obsolete punc filter
	goto exit0;
  }

  if (!wcsncmp(t0, L"`Pf", 3)) {
	switch(t0[3]) {
	case L'0':
	  state->punct_mode = INOTE_PUNCT_MODE_NONE;
	  break;
	case L'1':
	  state->punct_mode = INOTE_PUNCT_MODE_ALL;
	  break;
	case L'2':
	  state->punct_mode = INOTE_PUNCT_MODE_SOME;	  
	  len = min_size(t - (t0 + 4), MAX_PUNCT-1);
	  wcsncpy(self->punctuation_list, t0+4, len);
	  self->punctuation_list[len] = 0;
	  break;
	default:
	  first = INOTE_TYPE_TEXT; // unexpected value
	  ret = _INOTE_UNPROCESSED;
	  break;
	  }
	goto exit0;
  }
  
  first = INOTE_TYPE_ANNOTATION;
  ret = _INOTE_UNPROCESSED;

  exit0:
  if (ret == _INOTE_UNPROCESSED) {
	ret = inote_push_text(self, first, segment, state, tlv);	
  } else {
	segment_erase(segment, (uint8_t*)(t+1));
  }
  return ret;
}

static _inote_error inote_push_entity(inote_t *self, segment_t *segment, inote_state_t *state, tlv_t *tlv) {
  wchar_t *t, *tmax;  
  size_t len;  
  int i;
  _inote_error ret = _INOTE_UNPROCESSED;
  
  if (!state->ssml)
	return _INOTE_UNPROCESSED;

  if (!self || !segment || !tlv) {
	dbg1("_INOTE_ARGS_ERROR");
	return _INOTE_ARGS_ERROR;
  }

  t = segment_get_buffer(segment);
  tmax = segment_get_max(segment);
  
  len = tmax - t;
  for (i=0; i < MAX_ENTITY_NB; i++) {	
	if ((len >= xml_predefined_entity[i].l) && !wcsncmp(t, xml_predefined_entity[i].str, xml_predefined_entity[i].l)) {
	  break;
	}
  }
  if (i == MAX_ENTITY_NB) {
	return _INOTE_ARGS_ERROR;
  }

  t += xml_predefined_entity[i].l -1;
  *t = xml_predefined_entity[i].c;

  segment_erase(segment, (uint8_t*)t);
  if (iswpunct(*t)) {
	ret = inote_push_punct(self, segment, state, tlv);
  } else {
	ret = inote_push_text(self, INOTE_TYPE_TEXT, segment, state, tlv);
  }
  
  return ret;
}

static _inote_error inote_get_type_length_value(inote_t *self, const inote_slice_t *text, inote_state_t *state, inote_slice_t *tlv_message) {
  wchar_t *tmax;
  wchar_t *t;
  segment_t segment;
  tlv_t tlv;
  _inote_error ret = _INOTE_ARGS_ERROR;
  
  if (!self || !slice_check(text) || !state || !slice_check(tlv_message))
	return _INOTE_ARGS_ERROR;

  segment_init(&segment, text);
  tlv_init(&tlv, tlv_message);
  
  tmax = segment_get_max(&segment);  
  while (((t=segment_get_buffer(&segment)) < tmax) && t) {
	ret = _INOTE_UNPROCESSED;
	if (iswpunct(*t)) {
	  switch(*t) {
	  case L'<':
		ret = inote_push_tag(self, &segment, state, &tlv);
		break;
	  case L'`':
		ret = inote_push_annotation(self, &segment, state, &tlv);
		break;
	  case L'&':
		ret = inote_push_entity(self, &segment, state, &tlv);
		break;
	  default:
		break;
	  }
	  if (ret) {
		ret = inote_push_punct(self, &segment, state, &tlv);
	  }
	}
	if (ret) {
	  ret = inote_push_text(self, INOTE_TYPE_TEXT, &segment, state, &tlv);
	  if (ret) {
		break;
	  }
	}
  }
  
  if (!ret && (t=segment_get_buffer(&segment)) < tmax) {
	dbg1("Error: wchar_t text not fully processed!");
	ret = _INOTE_UNEMPTIED_BUFFER;
  }

  return ret;
}

void *inote_create() {
  inote_t *self = (inote_t*)calloc(1, sizeof(inote_t));
  if (self) {
	int i;
	self->magic = MAGIC;
	for (i=0; i<MAX_CHARSET; i++) {
	  self->cd_to_wchar[i] = ICONV_ERROR;
	  self->cd_from_wchar[i] = ICONV_ERROR;
	}	
  }
  return self;
}

void inote_delete(void *handle) {
  inote_t *self;
  if (!handle)
	return;

  self = (inote_t*)handle;
  if (self->magic == MAGIC) {
	int i;
	for (i=0; i<MAX_CHARSET; i++) {
	  if (self->cd_to_wchar[i] != ICONV_ERROR) {
		iconv_close(self->cd_to_wchar[i]);
	  }
	  if (self->cd_from_wchar[i] != ICONV_ERROR) {
		iconv_close(self->cd_from_wchar[i]);
	  }
	}	
	memset(self, 0, sizeof(*self));
	free(self);
  }
}

inote_error inote_convert_text_to_tlv(void *handle, const inote_slice_t *text, inote_state_t *state, inote_slice_t *tlv_message, size_t *text_left) {
  inote_error ret = INOTE_OK;
  inote_slice_t output;
  char *inbuf;
  size_t inbytesleft = 0;
  char *outbuf;
  size_t outbytesleft = 0;
  size_t outbytesleftmax = 0;
  inote_t *self;
  int iconv_status; // nb of non reversible conv char or -1
  
  if (!handle || ( (self=(inote_t*)handle)->magic != MAGIC)) {
	dbg("Args error (%p, %d)", handle, __LINE__);
	return INOTE_ARGS_ERROR;
  }
  if (!slice_check(text) || !slice_check(tlv_message)) {
	dbg("Args error (%p, %d)", (void*)text, __LINE__);
	return INOTE_ARGS_ERROR;
  }  

  if (slice_get_free_size(text) > TEXT_LENGTH_MAX) {
	dbg("Args error (%p, %d)", (void*)text, __LINE__);
	return INOTE_ARGS_ERROR;
  }  

  if (slice_get_free_size(tlv_message) > TLV_MESSAGE_LENGTH_MAX) {
	dbg("Args error (%p, %d)", (void*)text, __LINE__);
	return INOTE_ARGS_ERROR;
  }  

  if (!state || !text_left)
	return INOTE_ARGS_ERROR;
  
  if (!text->length) {
	dbg("LEAVE (%d)", __LINE__);
	tlv_message->length = 0;
	return INOTE_OK;
  }

  output.buffer = (uint8_t*)self->wchar_buf;
  output.length = 0;
  output.charset = INOTE_CHARSET_WCHAR_T;
  output.end_of_buffer = output.buffer + sizeof(self->wchar_buf);
  
  if (get_charset(&self->cd_to_wchar[text->charset], "WCHAR_T", charset_name[text->charset])
	  || get_charset(&self->cd_from_wchar[text->charset], charset_name[text->charset], "WCHAR_T"))  {
	ret = INOTE_CHARSET_ERROR;
	goto end0;
  }
  
  *text_left = 0;

  inbuf = (char *)(text->buffer);
  inbytesleft = text->length;
  outbuf = (char *)(output.buffer);
  outbytesleft = outbytesleftmax = slice_get_free_size(&output);

  iconv_status = -1;
  iconv_status = iconv(self->cd_to_wchar[text->charset],
					   &inbuf, &inbytesleft,
					   &outbuf, &outbytesleft);
  if (!iconv_status) {
	output.length = outbytesleftmax - outbytesleft;
	ret = inote_get_type_length_value(self, &output, state, tlv_message);
  } else if ((iconv_status != -1)
			 || (errno == E2BIG)) /* not sufficient room at output */ {
	output.length = outbytesleftmax - outbytesleft;
	ret = inote_get_type_length_value(self, &output, state, tlv_message);
	output.length = 0;
	outbuf = (char *)(output.buffer);
	outbytesleft = outbytesleftmax;
  } else {
	/* 
	   EINVAL: 
	   incomplete multibyte sequence in the input:
	   unexpected error, complete sequences are expected
	   EILSEQ:
	   invalid multibyte sequence in the input:
	   unexpected error thanks to //TRANSLIT
	*/
	iconv_status = 0;
  }
  
  /* initial state */
  iconv(self->cd_to_wchar[text->charset], NULL, NULL, NULL, NULL);
  *text_left +=  inbytesleft;
  //  DebugDump("tlv: ", tlv_message->buffer, min_size(tlv_message->length, 256));
  
 end0:
  if (ret > INOTE_INTERNAL_ERROR) {
	ret = INOTE_INTERNAL_ERROR;
  }
  return ret;
}

