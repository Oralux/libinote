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
  INOTE_CHARSET_WCHAR_T
} inote_charset_t;


typedef enum {
  INOTE_TYPE_UNDEFINED,
  INOTE_TYPE_TEXT,
  INOTE_TYPE_PUNCTUATION,
  INOTE_TYPE_ANNOTATION,
  INOTE_TYPE_TAG,
  INOTE_TYPE_ENTITY
} inote_type_t;

typedef enum {
  INOTE_PUNCT_MODE_NONE=0, /* does not pronounce punctuation */
  INOTE_PUNCT_MODE_ALL=1, /* pronounce all punctuation character */
  INOTE_PUNCT_MODE_SOME=2 /* pronounce any punctuation character in the punctuation list */
} inote_punct_mode_t;

/* inote_tlv_t 

type, length, value description
with 
- type: value from type1 (see inote_type_t) + type2 (optional info)
- length: length2<<8 + length1
- value: array of length bytes

Text
type1 = INOTE_TYPE_TEXT
type2 = charset

Punctuation
type1 = TYPE_PUNCTUATION
- type2 = mode (see inote_punct_t)
  if mode=some, value=<punctuation list in ascii>

- type2 = ascii punctuation char
  and length = 0

Annotation
type1 = INOTE_TYPE_ANNOTATION
*/
typedef struct {
  uint8_t type1;
  uint8_t type2;
  uint8_t length1;
  uint8_t length2;
} inote_tlv_t;
#define MAX_TLV_LENGTH (2<<16)

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
  uint32_t entity; /* 1 = xml entities must be interpreted; 0 = no interpretation */
} inote_state_t;

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
  text_offset: the first not yet consumed byte in text->buffer 
  RETURN: 0 if no error
  
  Example
  input: text="`Pf2()? <speak>Un&nbsp;éléphant,"  
  output:
  
  |-------------------+--------+---------------|
  | Type              | Length | Value         |
  |-------------------+--------+---------------|
  | some punctuation  |      3 | "()?"         |
  | utf8 text         |     11 | "Un éléphant" |
  | punctuation=","   |      0 |               |
  |-------------------+--------+---------------|

*/
  uint32_t inote_get_annotated_text(void *handle, const inote_slice_t *text, inote_state_t *state, inote_slice_t *tlv_message, size_t *text_offset);

#endif
