#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <iconv.h>
#include <errno.h>
#include <wctype.h>
#include <uchar.h>
#include "inote.h"
#include "debug.h"

#define ICONV_ERROR ((iconv_t)-1)
#define MAX_INPUT_BYTES 1024
#define MAX_CHAR32 (TEXT_LENGTH_MAX*sizeof(char32_t))
#define MAX_PUNCT 50
#define MAX_TOK 100
#define MAGIC 0x7E40B171
#define TLV_VALUE_LENGTH_THRESHOLD 16

static const char* charset_name[] = {
  NULL,
  "ISO-8859-1//IGNORE",
  "GBK//IGNORE",
  "UCS2//IGNORE",
  "BIG5//IGNORE",
  "SJIS//IGNORE",
  "UTF8//IGNORE",
  "UTF16//IGNORE",
  "UTF32LE", // UTF32 instead of UTF32LE would add BOM
};
#define MAX_CHARSET (sizeof(charset_name)/sizeof(*charset_name))

typedef struct {
  const char32_t *str;
  char32_t c;
  int l;
} predef_t;

static const predef_t xml_predefined_entity[] = {
  {U"&quot;", U'"',6},
  {U"&amp;", U'&',5},
  {U"&apos;", U'\'',6},
  {U"&lt;", U'<',4},
  {U"&gt;", U'>',4},
};
#define MAX_ENTITY_NB (sizeof(xml_predefined_entity)/sizeof(xml_predefined_entity[0]))

typedef struct {
  uint32_t magic;
  char32_t char32_buf[MAX_CHAR32];
  iconv_t cd_to_char32[MAX_CHARSET];
  iconv_t cd_from_char32[MAX_CHARSET];
  char32_t punctuation_list[MAX_PUNCT];
  char32_t token[MAX_TOK];
  // removing_leading_space: true if leading space must still be removed
  // (legacy fix at init for vv in text mode and spaces from the
  // initial and filtered 'gfax)
  bool removing_leading_space; 
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


static const char *error_get_string[] = {
  "INOTE_OK",
  "INOTE_ARGS_ERROR",
  "INOTE_CHARSET_ERROR",
  "INOTE_INVALID_MULTIBYTE",
  "INOTE_INCOMPLETE_MULTIBYTE",
  "INOTE_TLV_MESSAGE_FULL",
  "INOTE_TLV_FULL",
  "INOTE_UNPROCESSED",
  "INOTE_UNEMPTIED_BUFFER",
  "INOTE_TLV_ERROR",
  "INOTE_IO_ERROR",
  "INOTE_ERRNO" // INOTE_ERRNO: must be last enum
};

#define DBG_PRINT_SLICE(slice) if (slice) { \
	dbg("slice(buffer=%p, length=%lu, charset=%d, end_of_buffer=%p)",	\
		(slice)->buffer,												\
		(long unsigned int)((slice)->length),							\
		(slice)->charset,												\
		(slice)->end_of_buffer);										\
  }

#define DBG_PRINT_TLV_HEADER(tlv) if ((tlv) && (tlv)->header) {			\
	dbg("tlv(previous_header=%p, header(addr=%p, type=%d, length=%d))", \
		(tlv)->previous_header,											\
		(tlv)->header,													\
		(tlv)->header->type,											\
		(tlv)->header->length);											\
  }

// TPDP DBG_PRINT_STATE: expected_lang
#define DBG_PRINT_STATE(state) if (state) {								\
	dbg("state(punct_mode=%d, spelling=%d, lang=%d, max_expected_lang=%d, ssml=%d, annotation=%d))", \
		(state)->punct_mode,											\
		(state)->spelling,												\
		(state)->lang,													\
		(state)->max_expected_lang,										\
		(state)->ssml,													\
		(state)->annotation)											\
  }

const char *inote_error_get_string(inote_error err) {
  return (err < INOTE_ERRNO)? error_get_string[err] : strerror(err-INOTE_ERRNO);
}

uint8_t *inote_tlv_get_value(const inote_tlv_t *self) {
  return  self ? (uint8_t *)self + TLV_HEADER_LENGTH_MAX : NULL;
}

static size_t min_size(size_t a, size_t b) {
  return (a<b) ? a : b;
}

static bool cb_check(const inote_cb_t *self) {
  return (self && self->add_text && self->add_punctuation && self->add_annotation && self->add_charset);
}

static bool slice_check(const inote_slice_t *self) {
  return (self && self->buffer
		  && (self->buffer + self->length <= self->end_of_buffer)
		  && (self->charset >= INOTE_CHARSET_UNDEFINED) && (self->charset < MAX_CHARSET));
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

static uint8_t *slice_get_last_byte(const inote_slice_t *self) {
  uint8_t *last_byte = NULL;
  if (self && self->buffer && self->length) {
	last_byte = self->buffer + self->length -1;
	if (last_byte >= self->end_of_buffer) {
	  last_byte = NULL;
	}
  }
  return last_byte;
}

static inote_error segment_init(segment_t *self, const inote_slice_t *text) {
  ENTER();
  inote_error ret = INOTE_ARGS_ERROR;  
  if (self && text) {
	inote_slice_t *s = &self->s;
	memset(self, 0, sizeof(*self));
	memcpy(s, text, sizeof(*text));
	self->type = INOTE_TYPE_UNDEFINED;
	s->end_of_buffer = text->buffer + text->length;
	s->length = 0;
	ret = INOTE_OK;
  }
  return ret;
}

static char32_t *segment_get_buffer(segment_t *self) {
  char32_t* buffer = NULL;
  if (self && self->s.buffer) {
	buffer = (char32_t*)(self->s.buffer);  
	DBG_PRINT_SLICE(&(self->s));
  }
  return buffer;
}

static char32_t *segment_get_max(segment_t *self) {
  char32_t* max = NULL;
  if (self && self->s.buffer) {
	max = (char32_t*)(self->s.end_of_buffer);  
  }
  return max;
}

static void segment_erase(segment_t *self, uint8_t* buffer) {
  ENTER();
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

static inote_error tlv_init(tlv_t *self, inote_slice_t *tlv_message) {
  ENTER();
  int ret = INOTE_ARGS_ERROR;  
  if (self && tlv_message) {
	inote_tlv_t *header = (inote_tlv_t *)(tlv_message->buffer + tlv_message->length);
	memset(header, 0, sizeof(*header));
	memset(self, 0, sizeof(*self));
	self->s = tlv_message;
	ret = INOTE_OK;
  }
  return ret;
}

static tlv_t *tlv_next(tlv_t *self, inote_type_t type) {
  tlv_t *next = self;
  inote_tlv_t *header;
  uint8_t *free_byte = NULL;
  inote_slice_t *s = NULL;
    
  if (!self || !self->s || (type == INOTE_TYPE_UNDEFINED)) {
	next = NULL;	
	goto exit0;
  }
  
  header = self->header;

  DBG_PRINT_TLV_HEADER(self);  
  dbg("type=%d", type);
  if (header) {
	if (header->type == INOTE_TYPE_UNDEFINED) {
	  header->length = 0;
	  goto exit0;
	}
	if ((type == header->type) && (header->type == INOTE_TYPE_TEXT)
		&& (header->length < TLV_VALUE_LENGTH_MAX-TLV_VALUE_LENGTH_THRESHOLD)) {
	  goto exit0;
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
	self->header = header; // the header of self points on the next tlv
  } else {
	dbg("out of tlv");
	next = NULL;
  }

 exit0:
  DBG_PRINT_TLV_HEADER(next);  
  return next;
}

static inote_error tlv_add_length(tlv_t *self, uint16_t *length) {
  inote_slice_t *s;
  inote_tlv_t *header;
  uint8_t len;
  inote_error ret = INOTE_OK;  

  if (!self || !self->header || !self->s || !length) {
	ret = INOTE_ARGS_ERROR;
	goto exit0;
  }
	  
  s = self->s;
  if (slice_get_free_size(s) < *length) {
	ret = INOTE_TLV_MESSAGE_FULL;
	goto exit0;
  }
  
  header = self->header;
  DBG_PRINT_TLV_HEADER(self);    
  dbg("*length=%d", length ? *length : 0);
  len = min_size(*length, TLV_VALUE_LENGTH_MAX - header->length);
  if (!len) {
	ret = INOTE_TLV_FULL;
	goto exit0;
  }
  *length -= len;
  header->length += len;
  self->s->length += len;

 exit0:
  DBG_PRINT_TLV_HEADER(self);    
  dbg("LEAVE(%s) *length=%d",
	  inote_error_get_string(ret),
	  length ? *length : 0);
  return ret;
}

static uint8_t *tlv_get_free_byte(tlv_t *self) {
  return (self) ? slice_get_free_byte(self->s) : NULL;
}

// get free size in the current tlv
static size_t tlv_get_free_size(tlv_t *self) {
  return (self && self->header) ? TLV_VALUE_LENGTH_MAX - self->header->length : 0;
}

static inote_error get_charset(const char *tocode, const char *fromcode, iconv_t *cd) {
  uint32_t ret = 0;
  if (!cd)
	return INOTE_ARGS_ERROR;
  
  if (*cd != ICONV_ERROR)
	return INOTE_OK;

  *cd = iconv_open(tocode, fromcode);
  if (*cd == ICONV_ERROR) {
	int status = errno;
	dbg("Error iconv_open: from %s to %s (%s)", fromcode, tocode, strerror(status));
	ret = INOTE_CHARSET_ERROR;
  }
  return ret;
}

static inote_error inote_remove_leading_space(inote_t *self, inote_type_t first, segment_t *segment) {
  ENTER();
  inote_error ret = INOTE_OK;  
  char32_t *t, *t0, *tmax;

  t = t0 = segment_get_buffer(segment);
  tmax = segment_get_max(segment);
  if (!self || !segment) {
	return INOTE_ARGS_ERROR;
  }

  if (first == INOTE_TYPE_TEXT) {
	while ((t < tmax) && (*t == U' ')) {
	  t++;
	}
	if (t == tmax)
	  goto exit0;

	if (!iswpunct(*t) || (t0 == t)) {
	  self->removing_leading_space = false;
	}
  } else {
	self->removing_leading_space = false;
  }	  

  if (!self->removing_leading_space) {
	dbg("removing_leading_space = false");			
  }

 exit0:
  segment_erase(segment, (uint8_t*)t);
  return ret;
}
  
static inote_error inote_push_text(inote_t *self, inote_type_t first, segment_t *segment, inote_state_t *state, tlv_t *tlv) {
  ENTER();
  char *outbuf;
  size_t outbytesleft, max_outbytesleft = 0;
  inote_error ret = INOTE_OK;  
  int status;
  int err = 0;
  char32_t *t, *t0, *tmax;

  if (!self || !segment || !tlv) {
	ret = INOTE_ARGS_ERROR;
	goto exit0;
  }
  
  if (self->removing_leading_space) {
	ret = inote_remove_leading_space(self, first, segment);	
	goto exit0;
  }

  t = t0 = segment_get_buffer(segment);
  tmax = segment_get_max(segment);

  // the first char is considered as text
  t++;

  tlv = tlv_next(tlv, first);
  if (!tlv) {
	ret = INOTE_TLV_MESSAGE_FULL;
	goto exit0;
  }

  if (first == INOTE_TYPE_ANNOTATION) {
	while (t < tmax) {
	  if (*t == U' ') {
		t++; // include trailing white space
		break;
	  }
	  t++;
	}
  } else {
	while ((t < tmax) && !iswpunct(*t)) {
	  t++;
	}
  }

  segment->s.length = (uint8_t*)t - (uint8_t*)t0;
  outbuf = (char*)tlv_get_free_byte(tlv);
  max_outbytesleft = outbytesleft = tlv_get_free_size(tlv);
  
  dbg("iconv");
  status = iconv(self->cd_from_char32[tlv->s->charset],
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
	   unexpected error thanks to //IGNORE
	*/
	dbg("unexpected error: %s", strerror(err));
	ret = INOTE_ARGS_ERROR;
  }
  iconv(self->cd_from_char32[tlv->s->charset], NULL, NULL, NULL, NULL);

 exit0:
  dbg("LEAVE(%s)", inote_error_get_string(ret));  
  return ret;
}

/* TODO: ssml parser */
/* Currently any tag is simply filtered. */
static inote_error inote_push_tag(inote_t *self, segment_t *segment, inote_state_t *state, tlv_t *tlv) {
  ENTER();
  char32_t *t, *tmax;

  if (!state->ssml)
  	return INOTE_UNPROCESSED;

  if (!self || !segment || !tlv) {
  	dbg("INOTE_ARGS_ERROR");
  	return INOTE_ARGS_ERROR;
  }

  t = segment_get_buffer(segment);
  tmax = segment_get_max(segment);
  while ((t < tmax) && (*t != U'>')) {
	t++;
  }

  segment_erase(segment, (uint8_t*)(t+1));
  return INOTE_OK;
}

static inote_error inote_push_punct(inote_t *self, segment_t *segment, inote_state_t *state, tlv_t *tlv) {
  ENTER();
  int ret = 0;
  char32_t *t, *tmax;
  bool signal_punctuation = false;
  
  if (!self || !segment || !tlv) {
	dbg("INOTE_ARGS_ERROR");
	return INOTE_ARGS_ERROR;
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

  ret = INOTE_UNPROCESSED;
  if (!signal_punctuation) {
	ret = inote_push_text(self, INOTE_TYPE_TEXT, segment, state, tlv);	  
  } else if (t < tmax) {
	ret = inote_push_text(self, INOTE_TYPE_PUNCTUATION, segment, state, tlv);	  
  }

  return ret;
}

static inote_error inote_push_annotation(inote_t *self, segment_t *segment, inote_state_t *state, tlv_t *tlv) {
  ENTER();
  char32_t *t0, *t, *tmax;  
  size_t len;
  inote_type_t first = INOTE_TYPE_UNDEFINED;
  int ret = INOTE_OK;
  
  if (!state->annotation)
	return INOTE_UNPROCESSED;

  if (!self || !segment || !tlv) {
  	dbg("INOTE_ARGS_ERROR");
  	return INOTE_ARGS_ERROR;
  }

  t0 = t = segment_get_buffer(segment);
  tmax = segment_get_max(segment);
  while ((t < tmax) && (*t != U' ')) {
	t++;
  }

  if (t >= tmax) {
	ret = INOTE_UNPROCESSED;
	goto exit0;
  }	

  char32_t gfa[] = U"`gfa";
  char32_t pf[] = U"`Pf";
  if (!memcmp(t0, gfa, sizeof(gfa) - 4)) {
	switch(t0[4]) {
	case U'1':
	  state->ssml = 1;
	  goto exit0;
	case U'2':
	  // obsolete punc filter
	  goto exit0;
	}
  }
  
  if (!memcmp(t0, pf, sizeof(pf) - 4)) {
	switch(t0[3]) {
	case U'0':
	  state->punct_mode = INOTE_PUNCT_MODE_NONE;
	  break;
	case U'1':
	  state->punct_mode = INOTE_PUNCT_MODE_ALL;
	  break;
	case U'2':
	  state->punct_mode = INOTE_PUNCT_MODE_SOME;	  
	  len = min_size(t - (t0 + 4), MAX_PUNCT-1);
	  memcpy(self->punctuation_list, t0+4, len*sizeof(*t0));
	  self->punctuation_list[len] = 0;
	  break;
	default:
	  first = INOTE_TYPE_TEXT; // unexpected value
	  ret = INOTE_UNPROCESSED;
	  break;
	}
	goto exit0;
  }
  
  first = INOTE_TYPE_ANNOTATION;
  ret = INOTE_UNPROCESSED;

 exit0:
  if (ret == INOTE_UNPROCESSED) {
	ret = inote_push_text(self, first, segment, state, tlv);	
  } else {
	segment_erase(segment, (uint8_t*)(t+1));
  }
  return ret;
}

static inote_error inote_push_entity(inote_t *self, segment_t *segment, inote_state_t *state, tlv_t *tlv) {
  ENTER();
  char32_t *t, *tmax;  
  size_t len;  
  int i;
  inote_error ret = INOTE_UNPROCESSED;
  
  if (!state->ssml)
	return INOTE_UNPROCESSED;

  if (!self || !segment || !tlv) {
	dbg("INOTE_ARGS_ERROR");
	return INOTE_ARGS_ERROR;
  }

  t = segment_get_buffer(segment);
  tmax = segment_get_max(segment);
  
  len = tmax - t;
  for (i=0; i < MAX_ENTITY_NB; i++) {	
	if ((len >= xml_predefined_entity[i].l) && !memcmp(t, xml_predefined_entity[i].str, sizeof(*t)*xml_predefined_entity[i].l)) {
	  break;
	}
  }
  if (i == MAX_ENTITY_NB) {
	return INOTE_ARGS_ERROR;
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

static inote_error inote_get_type_length_value(inote_t *self, const inote_slice_t *text, inote_state_t *state, inote_slice_t *tlv_message) {
  ENTER();
  char32_t *tmax;
  char32_t *t;
  segment_t segment;
  tlv_t tlv;
  inote_error ret = INOTE_ARGS_ERROR;
  
  if (!self || !slice_check(text) || !state || !slice_check(tlv_message))
	return INOTE_ARGS_ERROR;

  segment_init(&segment, text);
  tlv_init(&tlv, tlv_message);
  
  tmax = segment_get_max(&segment);  
  while (((t=segment_get_buffer(&segment)) < tmax) && t) {
	ret = INOTE_UNPROCESSED;
	if (iswpunct(*t)) {
	  switch(*t) {
	  case U'<':
		ret = inote_push_tag(self, &segment, state, &tlv);
		break;
	  case U'`':
		ret = inote_push_annotation(self, &segment, state, &tlv);
		break;
	  case U'&':
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
	dbg("Error: char32_t text not fully processed!");
	ret = INOTE_UNEMPTIED_BUFFER;
  }

  return ret;
}

void *inote_create() {
  ENTER();
  inote_t *self = (inote_t*)calloc(1, sizeof(inote_t));
  if (self) {
	int i;
	self->magic = MAGIC;
	self->removing_leading_space = true;
	dbg("removing_leading_space = true");	
	for (i=0; i<MAX_CHARSET; i++) {
	  self->cd_to_char32[i] = ICONV_ERROR;
	  self->cd_from_char32[i] = ICONV_ERROR;
	}	
  }
  return self;
}

void inote_delete(void *handle) {
  ENTER();
  inote_t *self;
  if (!handle)
	return;

  self = (inote_t*)handle;
  if (self->magic == MAGIC) {
	int i;
	for (i=0; i<MAX_CHARSET; i++) {
	  if (self->cd_to_char32[i] != ICONV_ERROR) {
		iconv_close(self->cd_to_char32[i]);
	  }
	  if (self->cd_from_char32[i] != ICONV_ERROR) {
		iconv_close(self->cd_from_char32[i]);
	  }
	}	
	memset(self, 0, sizeof(*self));
	free(self);
  }
}

inote_error inote_convert_text_to_tlv(void *handle, const inote_slice_t *text, inote_state_t *state, inote_slice_t *tlv_message, size_t *text_left) {
  ENTER();
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
	ret = INOTE_ARGS_ERROR;
	goto exit0;
  }
  if (!slice_check(text) || !slice_check(tlv_message)) {
	ret = INOTE_ARGS_ERROR;
	goto exit0;
  }  

  if (slice_get_free_size(text) > TEXT_LENGTH_MAX) {
	ret = INOTE_ARGS_ERROR;
	goto exit0;
  }  

  if (slice_get_free_size(tlv_message) > TLV_MESSAGE_LENGTH_MAX) {
	ret = INOTE_ARGS_ERROR;
	goto exit0;
  }  

  if (!state || !text_left) {
	ret = INOTE_ARGS_ERROR;
	goto exit0;
  }	
  
  *text_left = 0;

  if (!text->length) {
	tlv_message->length = 0;
	ret = INOTE_OK;
	goto exit0;
  }

  DBG_PRINT_SLICE(text);
  DBG_PRINT_STATE(state);
  
  output.buffer = (uint8_t*)self->char32_buf;
  output.length = 0;
  output.charset = INOTE_CHARSET_UTF_32;
  output.end_of_buffer = output.buffer + sizeof(self->char32_buf);
  
  if (get_charset("UTF32LE", charset_name[text->charset], &self->cd_to_char32[text->charset])
	  || get_charset(charset_name[tlv_message->charset], "UTF32LE", &self->cd_from_char32[tlv_message->charset]))  {
	ret = INOTE_CHARSET_ERROR;
	goto exit0;
  }
  
  inbuf = (char *)(text->buffer);
  inbytesleft = text->length;
  outbuf = (char *)(output.buffer);
  outbytesleft = outbytesleftmax = slice_get_free_size(&output);

  iconv_status = -1;
  dbg("iconv");
  iconv_status = iconv(self->cd_to_char32[text->charset],
					   &inbuf, &inbytesleft,
					   &outbuf, &outbytesleft);
  if (iconv_status != -1) {
	*text_left = inbytesleft;
	output.length = outbytesleftmax - outbytesleft;
	ret = inote_get_type_length_value(self, &output, state, tlv_message);
  } else {
	int err = errno;
	uint8_t *byte_error = (uint8_t *)inbuf;
	uint8_t *last_byte = slice_get_last_byte(text);
	if (byte_error && last_byte && (byte_error <= last_byte)) {
	  *text_left = last_byte - byte_error + 1;
	}
	switch (err) {
	case EILSEQ:
	  ret = INOTE_INVALID_MULTIBYTE;
	  break;
	case EINVAL:
	  ret = INOTE_INCOMPLETE_MULTIBYTE;
	  break;
	default:
	  ret = INOTE_ERRNO + err;
	  break;
	}
	dbg("%s", strerror(err));
  }
  
  /* initialize iconv state */
  iconv(self->cd_to_char32[text->charset], NULL, NULL, NULL, NULL);
  //  DebugDump("tlv: ", tlv_message->buffer, min_size(tlv_message->length, 256));
  
 exit0:
  DBG_PRINT_SLICE(tlv_message);
  dbg("LEAVE(%s), *text_left=%lu", inote_error_get_string(ret), text_left ? (long unsigned int)(*text_left) : 0);  
  return ret;
}

inote_error inote_convert_tlv_to_text(inote_slice_t *tlv_message, inote_cb_t *cb) {
  ENTER();
  inote_error ret = INOTE_OK;
  inote_tlv_t *tlv;
  uint8_t *t, *tmax;

  if (!cb_check(cb)) {
	ret = INOTE_ARGS_ERROR;
	goto exit0;
  }
  
  if (!slice_check(tlv_message)) {
	ret = INOTE_ARGS_ERROR;
	goto exit0;
  }  

  DBG_PRINT_SLICE(tlv_message);

  tmax = slice_get_last_byte(tlv_message) + 1 - TLV_HEADER_LENGTH_MAX;
  t = tlv_message->buffer;
  while ((t <= tmax) && !ret) {
	tlv = (inote_tlv_t*)t;
	switch (tlv->type) {
	case INOTE_TYPE_ANNOTATION:
	  cb->add_annotation(tlv, cb->user_data);
	  break;
	case INOTE_TYPE_CHARSET:
	  cb->add_charset(tlv, cb->user_data);
	  break;
	case INOTE_TYPE_PUNCTUATION:
	  cb->add_punctuation(tlv, cb->user_data);
	  break;
	case INOTE_TYPE_TEXT:
	  cb->add_text(tlv, cb->user_data);
	  break;
	default:
	  dbg("wrong tlv (%p)", (void*)tlv);
	  ret = INOTE_TLV_ERROR;
	  break;
	}
	t += TLV_HEADER_LENGTH_MAX + (uint8_t)tlv->length;
  }

 exit0:
  dbg("LEAVE(%s)", inote_error_get_string(ret));  
  return ret;
}
