#ifndef INOTE_H
#define INOTE_H

#include <stdint.h>

#define INOTE_VERSION_MAJOR 1
#define INOTE_VERSION_MINOR 0
#define INOTE_VERSION_PATCH 1

typedef enum {
  INOTE_CHARSET_UNDEFINED = 0,
  INOTE_CHARSET_ISO_8859_1,
  INOTE_CHARSET_GBK,
  INOTE_CHARSET_UCS_2,
  INOTE_CHARSET_BIG_5,
  INOTE_CHARSET_SJIS,
  INOTE_CHARSET_UTF_8,
  INOTE_CHARSET_UTF_16,
  INOTE_CHARSET_UTF_32,
} inote_charset_t;

typedef enum {
  INOTE_TYPE_UNDEFINED,
  INOTE_TYPE_TEXT=(1<<0),
  INOTE_TYPE_CHARSET=(1<<1),
  INOTE_TYPE_PUNCTUATION=INOTE_TYPE_TEXT+(1<<2),
  INOTE_TYPE_ANNOTATION=INOTE_TYPE_TEXT+(1<<3),
} inote_type_t;

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
length = 1 + remaining text (first char=punctuation char, followed by text) 

Annotation
type = INOTE_TYPE_ANNOTATION
length = annotation length
*/
typedef struct {
  uint8_t type;
  uint8_t length;
} inote_tlv_t;

#define TLV_LENGTH_MAX (1+UINT8_MAX)
#define TLV_HEADER_LENGTH_MAX sizeof(inote_tlv_t)
#define TLV_VALUE_LENGTH_MAX (TLV_LENGTH_MAX - TLV_HEADER_LENGTH_MAX)

uint8_t *inote_tlv_get_value(const inote_tlv_t *self);

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
  INOTE_INVALID_MULTIBYTE,
  INOTE_INCOMPLETE_MULTIBYTE,
  INOTE_TLV_MESSAGE_FULL, // no other tlv can be added to tlv_message
  INOTE_TLV_FULL, // the current tlv is full
  INOTE_UNPROCESSED,
  INOTE_UNEMPTIED_BUFFER, // the internal char32_t buffer can't be fully processed
  INOTE_TLV_ERROR,
  INOTE_IO_ERROR,
  INOTE_ERRNO=0x1000 // return 0x1000 + errno  
} inote_error;

typedef inote_error (*inote_add_annotation_t)(inote_tlv_t *tlv, void *user_data);  
typedef inote_error (*inote_add_charset_t)(inote_tlv_t *tlv, void *user_data);  
typedef inote_error (*inote_add_punct_t)(inote_tlv_t *tlv, void *user_data);  
typedef inote_error (*inote_add_text_t)(inote_tlv_t *tlv, void *user_data);  

typedef struct {
  inote_add_annotation_t add_annotation;
  inote_add_charset_t add_charset;
  inote_add_punct_t add_punctuation;
  inote_add_text_t add_text;
  void *user_data;
} inote_cb_t;

#define TEXT_LENGTH_MAX 1024
#define TLV_MESSAGE_LENGTH_MAX (3*TEXT_LENGTH_MAX)

void *inote_create();
void inote_delete(void *handle);

/*
  The text and tlv_message slices are pre-allocated by the caller,
  with the max size details below.

  text: raw text or enriched with SSML tags or ECI annotations.
  null terminator not needed.
  text->length <= TEXT_LENGTH_MAX

  state: punctuation, current language,...

  tlv_message: type_length_value formated data
  tlv_message->length <= TLV_MESSAGE_LENGTH_MAX

  text_left: number of bytes not yet consumed in text->buffer 

  RETURN: INOTE_OK if no error, otherwise:
    - INOTE_INVALID_MULTIBYTE: text_left is set; the first byte left is the invalid byte.
    - INOTE_INCOMPLETE_MULTIBYTE: idem: text_left set; first byte left is the in.
    - ... 
  
  Example
  input: text="`Pf2()? <speak>Un &lt;éléphant&gt; (1)</speak>"  
  output:
  00000000  01 10 55 6e 20 3c c3 a9  6c c3 a9 70 68 61 6e 74  |..Un <..l..phant|
  00000010  3e 20 05 02 28 31 05 01  29                       |> ..(1..)|
  
  |-------------------+--------+------------------|
  | Type              | Length | Value            |
  |-------------------+--------+------------------|
  | text              |     16 | "Un <éléphant> " |
  | text+punctuation  |      2 | "(1"             |
  | text+punctuation  |      1 | ")"              |
  |-------------------+--------+------------------|

*/
  inote_error inote_convert_text_to_tlv(void *handle, const inote_slice_t *text, inote_state_t *state, inote_slice_t *tlv_message, size_t *text_left);

/*
  tlv_message and cb are supplied by the caller.

  tlv_message: type_length_value formated data
  tlv_message->length: no implicit limit on the length

  cb: callback to be called according to the tlv type

  RETURN: INOTE_OK if no error
  
  Example
  input:
  00000000  01 10 55 6e 20 3c c3 a9  6c c3 a9 70 68 61 6e 74  |..Un <..l..phant|
  00000010  3e 20 05 02 28 31 05 01  29                       |> ..(1..)|
  
  |-------------------+--------+------------------|
  | Type              | Length | Value            |
  |-------------------+--------+------------------|
  | text              |     16 | "Un <éléphant> " |
  | text+punctuation  |      2 | "(1"             |
  | text+punctuation  |      1 | ")"              |
  |-------------------+--------+------------------|

  possible output (according to the supplied callbacks): 
  text="Un <éléphant> (1)"  

*/
  inote_error inote_convert_tlv_to_text(inote_slice_t *tlv_message, inote_cb_t *cb);

// convert an inote_error to string
const char *inote_error_get_string(inote_error err);

// debug
void inoteDebugInit();

#endif
