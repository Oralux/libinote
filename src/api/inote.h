#ifndef INOTE_H
#define INOTE_H

#include <stdint.h>

typedef enum {
  INOTE_CHARSET_UNDEFINED = 0,
  INOTE_CHARSET_ISO_8859_1,
  INOTE_CHARSET_GBK,
  INOTE_CHARSET_UCS_2,
  INOTE_CHARSET_BIG_5,
  INOTE_CHARSET_SJIS,
  INOTE_CHARSET_UTF_8,
  INOTE_CHARSET_UTF_16,
  INOTE_CHARSET_WCHAR_T,
} inote_charset_t;


typedef enum {
  INOTE_TYPE_UNDEFINED,
  INOTE_TYPE_TEXT=(1<<0),
  INOTE_TYPE_CHARSET=(1<<1),
  INOTE_TYPE_PUNCTUATION=INOTE_TYPE_TEXT+(1<<2),
  INOTE_TYPE_ANNOTATION=INOTE_TYPE_TEXT+(1<<3),
} inote_type_t;

#define INOTE_ANNOTATION

typedef enum {
  INOTE_PUNCT_MODE_NONE=0, /* does not pronounce punctuation */
  INOTE_PUNCT_MODE_ALL=1, /* pronounce all punctuation character */
  INOTE_PUNCT_MODE_SOME=2, /* pronounce any punctuation character in the punctuation list */
  INOTE_PUNCT_FOUND=UINT8_MAX, /* punctuation character */
} inote_punct_mode_t;

/* inote_tlv_t 

type, length, value description
with 
- type: see inote_type_t
- length: 0..255
- value: array of length bytes

Text
type = INOTE_TYPE_TEXT

Punctuation character (to be said)
type = INOTE_TYPE_PUNCTUATION
length = 1 + remaining text (first char=punctuation cra, followed by text) 

Annotation
type = INOTE_TYPE_ANNOTATION
length = annotation + remaining text
*/
typedef struct {
  uint8_t type;
  uint8_t length;
} inote_tlv_t;

#define TLV_LENGTH_MAX (1+UINT8_MAX)
#define TLV_HEADER_LENGTH_MAX sizeof(inote_tlv_t)
#define TLV_VALUE_LENGTH_MAX (TLV_LENGTH_MAX - TLV_HEADER_LENGTH_MAX)

typedef struct {
  uint8_t *buffer; 
  size_t length; /* data length in bytes */
  inote_charset_t charset;
  uint8_t *end_of_buffer; /* allocated buffer size */
} inote_slice_t;
  
typedef struct {
  inote_punct_mode_t punct_mode;
  uint32_t spelling; /* 1 = spelling command already set */
  uint32_t lang; /* 0=unknown, otherwise probable language */
  uint32_t *expected_lang; /* array of the expected languages */
  uint32_t max_expected_lang; /* max number of elements of expected_lang */
  uint32_t ssml; /* 1 = SSML tags must be interpreted; 0 = no interpretation */
  uint32_t annotation; /* 1 = annotations must be interpreted; 0 = no interpretation */
} inote_state_t;

typedef enum {
  INOTE_OK=0,
  INOTE_ARGS_ERROR,
  INOTE_CHARSET_ERROR,
  INOTE_ERROR_MAX,
} inote_error;

void *inote_create();
void inote_delete(void *handle);

/*
  text: null terminated text (raw or enriched with SSML tags or ECI
  annotations)
  text->length does not count the terminator
  if text->charset defines multibytes sequences, then text-buffer must
  supply complete sequences

  state: punctuation, current language,...

  tlv: type_length_value formated data; text sections encoded in the
  indicated charset
  text_left: number of bytes not yet consumed in text->buffer 
  RETURN: 0 if no error
  
  Example
  input: text="`Pf2()? <speak>Un &lt;éléphant&gt; (1)</speak>"  
  output:
  
  |-------------------+--------+------------------|
  | Type              | Length | Value            |
  |-------------------+--------+------------------|
  | some punctuation  |      3 | "()?"            |
  | utf8 text         |     16 | "Un <éléphant> " |
  | found punctuation |      0 |                  |
  | utf8 text         |      2 | "(1"             |
  | found punctuation |      0 |                  |
  | utf8 text         |      1 | ")"              |
  |-------------------+--------+------------------|

*/
  inote_error inote_convert_text_to_tlv(void *handle, const inote_slice_t *text, inote_state_t *state, inote_slice_t *tlv_message, size_t *text_left);

  inote_error inote_convert_tlv_to_text(void *handle, inote_slice_t *tlv_message, inote_state_t *state, const inote_slice_t *text, size_t *tlva_offset);


#endif
