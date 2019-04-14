#include <stdlib.h>
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
} inote_t;

typedef struct {
  inote_type_t type;
  inote_slice_t s;
} segment_t;

typedef struct {
  inote_slice_t *s;
  inote_tlv_t *header;
  inote_tlv_t *previous_header;
} tlv_t;

static size_t min_size(size_t a, size_t b) {
  return (a<b) ? a : b;
}

static uint32_t slice_check(const inote_slice_t *self) {
  return (self && self->buffer
		  && (self->buffer + self->length <= self->end_of_buffer)
		  && (self->charset > INOTE_CHARSET_UNDEFINED) && (self->charset < MAX_CHARSET));
}

static size_t slice_get_free_size(inote_slice_t *self) {
  size_t s = 0;
  if (self && self->buffer) {
	uint8_t *free_byte = self->buffer + self->length;
	if (free_byte < self->end_of_buffer) {
	  s = self->end_of_buffer - free_byte;
	}
  }
  return s;
}

static uint8_t *slice_get_free_byte(inote_slice_t *self) {
  uint8_t *free_byte = NULL;
  if (self && self->buffer) {
	free_byte = self->buffer + self->length;
	if (free_byte >= self->end_of_buffer) {
	  free_byte = NULL;
	}
  }
  return free_byte;
}

static int segment_init(segment_t *self, const inote_slice_t *text) {
  int ret = 1;  
  if (self && text) {
	inote_slice_t *s = &self->s;
	memset(self, 0, sizeof(*self));
	memcpy(s, text, sizeof(*text));
	self->type = INOTE_TYPE_UNDEFINED;
	s->end_of_buffer = text->buffer + text->length;
	s->length = 0;
	ret = 0;
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

static int tlv_init(tlv_t *self, inote_slice_t *tlv_message) {
  int ret = 1;  
  if (self && tlv_message) {
	inote_tlv_t *header = (inote_tlv_t *)(tlv_message->buffer + tlv_message->length);
	memset(header, 0, sizeof(*header));
	memset(self, 0, sizeof(*self));
	self->s = tlv_message;
	ret = 0;
  }
  return ret;
}

static tlv_t *tlv_next(tlv_t *self, inote_type_t type1, uint8_t type2) {
  tlv_t *next = NULL;
  inote_tlv_t *header;

  if (!self || !self->s || (type1 == INOTE_TYPE_UNDEFINED)) {
	return NULL;
  }
  
  header = self->header;
  if (header) {
	if (header->type1 == INOTE_TYPE_UNDEFINED) {
	  header->length1 = header->length2 = 0;
	  return self;
	}
	if ((type1 == header->type1) && (header->type1 == INOTE_TYPE_TEXT)) {
	  return self;
	}
  }  

  inote_slice_t *s = self->s;
  uint8_t *max = s->buffer + s->length;
  if (max + sizeof(*header) <= s->end_of_buffer) {
	self->previous_header = header;
	header = (inote_tlv_t *)max;
	header->type1 = type1;
	header->type2 = type2;
	header->length1 = header->length2 = 0;
	s->length += sizeof(*header);
	self->header = header;
	next = self;
  } else {
	dbg1("out of tlv");	
  }
  
  return next;
}

static int tlv_add_length(tlv_t *self, uint16_t length) {
  int ret = 1;
  if (self && self->header && self->s) {
	inote_slice_t *s = self->s;
	if (slice_get_free_size(s) >= length) {
	  inote_tlv_t *header = self->header;
	  uint16_t len = length + header->length1 + ((header->length2)<<8);
	  header->length1 = (len & 0xff);
	  header->length2 = (len >> 8);
	  self->s->length += length;
	  ret = 0;
	} else {
	  dbg1("out of tlv");	
	}
  }
  return ret;
}

static uint8_t *tlv_get_free_byte(tlv_t *self) {
  uint8_t *free_byte = NULL;  
  if (self) {
	free_byte = slice_get_free_byte(self->s);
  }
  return free_byte;
}

static size_t tlv_get_free_size(tlv_t *self) {
  size_t s = 0;  
  if (self) {
	s = min_size(slice_get_free_size(self->s), MAX_TLV_LENGTH);
  }
  return s;
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

static uint32_t inote_push_text(inote_t *self, segment_t *segment, inote_state_t *state, tlv_t *tlv) {
  char *outbuf;
  size_t outbytesleft, max_outbytesleft = 0;
  int ret = 0;
  int err = 0;
  wchar_t *t, *t0, *tmax;
  
  if (!self || !segment || !tlv) {
	dbg1("EINVAL");
	return EINVAL;
  }

  tlv = tlv_next(tlv, INOTE_TYPE_TEXT, tlv->s->charset);
  if (!tlv) {
	return ENOMEM;
  }

  t = t0 = segment_get_buffer(segment);
  tmax = segment_get_max(segment);
  // the first char is considered as text
  t++;
  while ((t < tmax) && !iswpunct(*t)) {
	t++;
  }

  segment->s.length = (uint8_t*)t - (uint8_t*)t0;
  outbuf = (char*)tlv_get_free_byte(tlv);
  max_outbytesleft = outbytesleft = tlv_get_free_size(tlv);
  
  ret = iconv(self->cd_from_wchar[tlv->s->charset],
			  (char**)&segment->s.buffer, &segment->s.length,
			  &outbuf, &outbytesleft);
  err = errno;
  if (!ret || (err == E2BIG)) {
	tlv_add_length(tlv, max_outbytesleft - outbytesleft);
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
  iconv(self->cd_from_wchar[tlv->s->charset], NULL, NULL, NULL, NULL);
  return ret;
}

/* TODO: ssml parser */
/* Currently any tag is simply filtered. */
static uint32_t inote_push_tag(inote_t *self, segment_t *segment, inote_state_t *state, tlv_t *tlv) {
  wchar_t *t, *tmax;

  if (!state->ssml)
  	return 1;

  if (!self || !segment || !tlv) {
  	dbg1("EINVAL");
  	return EINVAL;
  }

  t = segment_get_buffer(segment);
  tmax = segment_get_max(segment);
  while ((t < tmax) && (*t != L'>')) {
	t++;
  }

  segment_erase(segment, (uint8_t*)(t+1));
  return 0;
}

static uint32_t inote_push_punct(inote_t *self, segment_t *segment, inote_state_t *state, tlv_t *tlv) {
  int ret = 0;
  wchar_t *t, *tmax;
  tlv_t *next = NULL;
  
  if (!self || !segment || !tlv) {
	dbg1("EINVAL");
	return EINVAL;
  }

  t = segment_get_buffer(segment);
  tmax = segment_get_max(segment);
  
  switch (state->punct_mode) {
  case INOTE_PUNCT_MODE_NONE:
	  ret = inote_push_text(self, segment, state, tlv);	  
	break;
  case INOTE_PUNCT_MODE_ALL: {
	if (t < tmax) {	  
	  next = tlv_next(tlv, INOTE_TYPE_PUNCTUATION, INOTE_PUNCT_FOUND);
	  if (!next) {
		return ENOMEM;
	  }  
	  // then process the punctuation character as usual text
	  ret = inote_push_text(self, segment, state, tlv);	  
	}
  }
	break;
  default:
	break;
  }
  return ret;
}

static uint32_t inote_push_annotation(inote_t *self, segment_t *segment, inote_state_t *state, tlv_t *tlv) {
  if (!state->annotation)
	return 1;
  
  return 0;
}

static uint32_t inote_push_entity(inote_t *self, segment_t *segment, inote_state_t *state, tlv_t *tlv) {
  wchar_t *t, *tmax;  
  size_t len;  
  int i;
  int ret;
  
  if (!state->ssml)
	return 1;

  if (!self || !segment || !tlv) {
	dbg1("EINVAL");
	return EINVAL;
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
	return EINVAL;
  }

  t += xml_predefined_entity[i].l -1;
  *t = xml_predefined_entity[i].c;

  segment_erase(segment, (uint8_t*)t);
  ret = inote_push_text(self, segment, state, tlv);	    
  
  return ret;
}

static uint32_t inote_get_type_length_value(inote_t *self, const inote_slice_t *text, inote_state_t *state, inote_slice_t *tlv_message) {
  wchar_t *tmax;
  wchar_t *t;
  segment_t segment;
  tlv_t tlv;
  int ret = 1;
  
  if (!self || !slice_check(text) || !state || !slice_check(tlv_message))
	return 1;

  segment_init(&segment, text);
  tlv_init(&tlv, tlv_message);
  
  tmax = segment_get_max(&segment);  
  while (((t=segment_get_buffer(&segment)) < tmax) && t) {
	ret = 1;
	if (iswpunct(*t)) {
	  switch(*t) {
	  case L'<':
		ret = inote_push_tag(self, &segment, state, &tlv);
		break;
	  case L'\'':
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
	  ret = inote_push_text(self, &segment, state, &tlv);
	  if (ret) {
		dbg1("TODO");
	  }
	}
  };
  
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

uint32_t inote_get_annotated_text(void *handle, const inote_slice_t *text, inote_state_t *state, inote_slice_t *tlv_message, size_t *input_offset) {
  uint32_t a_status = 0;
  inote_slice_t output;
  char *inbuf;
  size_t inbytesleft = 0;
  char *outbuf;
  size_t outbytesleft = 0;
  size_t outbytesleftmax = 0;

  inote_t *self;
  int ret;
  
  if (!handle || ( (self=(inote_t*)handle)->magic != MAGIC)) {
	dbg("Args error (%p, %d)", handle, __LINE__);
	return 1;
  }
  if (!slice_check(text) || !slice_check(tlv_message)) {
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

  output.buffer = (uint8_t*)self->wchar_buf;
  output.length = 0;
  output.charset = INOTE_CHARSET_WCHAR_T;
  output.end_of_buffer = output.buffer + sizeof(self->wchar_buf);
  
  if (get_charset(&self->cd_to_wchar[text->charset], "WCHAR_T", charset_name[text->charset])
	  || get_charset(&self->cd_from_wchar[text->charset], charset_name[text->charset], "WCHAR_T"))  {
	a_status = 1;
	goto end0;
  }
  
  inbuf = (char *)(text->buffer);
  inbytesleft = text->length;
  outbuf = (char *)(output.buffer);
  outbytesleft = outbytesleftmax = slice_get_free_size(&output);

  ret = -1;
  while (ret) {
	ret = iconv(self->cd_to_wchar[text->charset],
				&inbuf, &inbytesleft,
				&outbuf, &outbytesleft);
	if (!ret) {
	  output.length = outbytesleftmax - outbytesleft;
	  inote_get_type_length_value(self, &output, state, tlv_message);
	} else if (errno == E2BIG) { /* not sufficient room at output */
	  output.length = outbytesleftmax - outbytesleft;
	  inote_get_type_length_value(self, &output, state, tlv_message);
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
	  ret = 0;
	}
  }
  /* initial state */
  ret = iconv(self->cd_to_wchar[text->charset], NULL, NULL, NULL, NULL);
    
  if (inbytesleft) {
	dbg("Failed to convert inbytesleft=%ld bytes",  (long int)inbytesleft);
	a_status = 1;
  }

  DebugDump("tlv: ", tlv_message->buffer, min_size(tlv_message->length, 256));
  
 end0:
  return a_status;
}

