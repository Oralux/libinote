/*
  Copyright 2019-2020, Gilles Casse <gcasse@oralux.org>

  This is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2.1, or
  (at your option) any later version.

  This software is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

*/

/**
   @file
   @brief libinote API 
   
   The libinote library preprocesses a text aimed to a text-to-speech engine:
   - input: raw text or enriched with SSML tags or ECI annotations,
   - output: type-length-value format.

   Links:
   - Libinote sources: https://github.com/Oralux/libinote 
   
*/
#ifndef INOTE_H
#define INOTE_H

#include <stdint.h>

#define INOTE_VERSION_MAJOR 1
#define INOTE_VERSION_MINOR 1
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
  INOTE_TYPE_CAPITAL=INOTE_TYPE_TEXT+(1<<4),
  INOTE_TYPE_CAPITALS=INOTE_TYPE_TEXT+(1<<4)+(1<<1),
} inote_type_t;

typedef enum {
  INOTE_PUNCT_MODE_NONE=0, /**< do not pronounce punctuation */
  INOTE_PUNCT_MODE_ALL=1, /**< pronounce all punctuation character */
  INOTE_PUNCT_MODE_SOME=2, /**< pronounce any punctuation character in the punctuation list */
  INOTE_PUNCT_FOUND=UINT8_MAX, /**< punctuation character */
} inote_punct_mode_t;

/**
   inote_tlv_t 
   
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

   Capital
   type = INOTE_TYPE_CAPITAL
   length = number of capital letters

*/
typedef struct {
  uint8_t type;
  uint8_t length;
} inote_tlv_t;

#define TLV_LENGTH_MAX (1+UINT8_MAX)
#define TLV_HEADER_LENGTH_MAX sizeof(inote_tlv_t)
#define TLV_VALUE_LENGTH_MAX (TLV_LENGTH_MAX - TLV_HEADER_LENGTH_MAX)

uint8_t *inote_tlv_get_value(const inote_tlv_t *tlv);

typedef struct {
  uint8_t *buffer; 
  size_t length; /**< data length in bytes */
  inote_charset_t charset;
  uint8_t *end_of_buffer; /**< allocated buffer size (buffer + length <= end_of_buffer) */
} inote_slice_t;
  
typedef struct {
  inote_punct_mode_t punct_mode;
  uint32_t spelling; /**< 1 = spelling command already set */
  uint32_t lang; /**< 0=unknown, otherwise probable language */
  uint32_t *expected_lang; /**< array of the expected languages */
  uint32_t max_expected_lang; /**< max number of elements of expected_lang */
  uint32_t ssml; /**< 1 = SSML tags must be interpreted; 0 = no interpretation */
  uint32_t annotation; /**< 1 = annotations must be interpreted; 0 = no interpretation */
} inote_state_t;

typedef enum {
  INOTE_OK=0,
  INOTE_ARGS_ERROR,
  INOTE_CHARSET_ERROR,
  INOTE_INVALID_MULTIBYTE,
  INOTE_INCOMPLETE_MULTIBYTE,
  INOTE_TLV_MESSAGE_FULL, /**< no other tlv can be added to tlv_message */
  INOTE_TLV_FULL, /**< the current tlv is full */
  INOTE_UNPROCESSED,
  INOTE_UNEMPTIED_BUFFER, /**< the internal char32_t buffer can't be fully processed */
  INOTE_TLV_ERROR,
  INOTE_IO_ERROR,
  INOTE_LANGUAGE_SWITCHING, /**< the remaining input text concerns another language (see below 'Language Switching') */
  INOTE_ERRNO=0x1000 /**< return 0x1000 + errno */
} inote_error;

typedef inote_error (*inote_add_annotation_t)(inote_tlv_t *tlv, void *user_data);  
typedef inote_error (*inote_add_charset_t)(inote_tlv_t *tlv, void *user_data);  
typedef inote_error (*inote_add_punct_t)(inote_tlv_t *tlv, void *user_data);  
typedef inote_error (*inote_add_text_t)(inote_tlv_t *tlv, void *user_data);  
typedef inote_error (*inote_add_capital_t)(inote_tlv_t *tlv, bool capitals, void *user_data);  

typedef struct {
  inote_add_annotation_t add_annotation;
  inote_add_charset_t add_charset;
  inote_add_punct_t add_punctuation;
  inote_add_text_t add_text;
  inote_add_capital_t add_capital;
  void *user_data;
} inote_cb_t;

#define TEXT_LENGTH_MAX 1024
#define TLV_MESSAGE_LENGTH_MAX (3*TEXT_LENGTH_MAX)

/**
   create an inote instance

   @return instance
*/
void *inote_create();


/**
   delete an inote instance

   @param instance
*/
void inote_delete(void *handle);

/**
   The text and tlv_message slices are pre-allocated by the caller,
   with the max size details below.
   
   text: raw text or enriched with SSML tags or ECI annotations.
   null terminator not needed.
   text->length <= TEXT_LENGTH_MAX
   
   state: punctuation, current language,...
   
   tlv_message: type_length_value formated data
   tlv_message->length <= TLV_MESSAGE_LENGTH_MAX
   
   text_left: number of bytes not yet consumed in text->buffer 
   
   Language switching
   If an annotation requires to change the language,
   inote_convert_text_to_tlv returns INOTE_LANGUAGE_SWITCHING, and the
   remaining text points on this annotation.
   
   RETURN: INOTE_OK if no error, otherwise:
   - INOTE_INVALID_MULTIBYTE: text_left is set; the first byte left is the invalid byte.
   - INOTE_INCOMPLETE_MULTIBYTE: idem: text_left set; first byte left is the invalid byte.
   - INOTE_LANGUAGE_SWITCHING: text_left is set; the first byte left is the annotation.
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
   
   @param[in] handle  inote instance
   @param[in] text  text to convert
   @param[in,out] state  Can be updated according e.g. to the annotations processed internally (punctuation or ssml mode,...)
   @param[out] tlv_message  tlv resulting from the conversion of text
   @param[out] text_left  end of the supplied text not yet converted
   @return inote_error
*/
inote_error inote_convert_text_to_tlv(void *handle, const inote_slice_t *text, inote_state_t *state, inote_slice_t *tlv_message, size_t *text_left);

/**
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

   @param tlv_message  tlv to convert
   @param cb  callbacks to call according to the recognized type
   @return inote_error
*/
inote_error inote_convert_tlv_to_text(inote_slice_t *tlv_message, inote_cb_t *cb);

/**
   obtain the type of a tlv message
   
   @param[in] tlv_message  tlv messageresulting from the conversion of text
   @param[out] type  type of the tlv_message
   @return inote_error
*/
inote_error inote_slice_get_type(const inote_slice_t *tlv_message, inote_type_t *type);

/**
   stringify an inote_error
   
   @param err
   @return string
*/
const char *inote_error_get_string(inote_error err);

/**
   Generate tlv compatible with an older version.
   
   This function indicates the version of libinote which will be used to decode the tlv.
   inote_convert_text_to_tlv() will then generate TLV compatible with this version.

   For example, TLV indicating capitalized words (INOTE_TYPE_CAPITAL)
   will be generated only if the version supplied is greater or equal
   to 1.1.0.

   @param handle  inote instance
   @param major  e.g. 1 for version 1.2.3
   @param minor  e.g. 2 for version 1.2.3
   @param major  e.g. 3 for version 1.2.3
   @return inote_error
*/
inote_error inote_set_compatibility(void *handle, int major, int minor, int patch);

/**
   Enable TVL for capitalized words
   
   By default, no INOTE_TYPE_CAPITAL(S) TLV is generated.

   @param handle  inote instance
   @param with_capital  if set to true, enable TLV for capitalized words
   @return inote_error
*/
inote_error inote_enable_capital(void *handle, bool with_capital);

/** debug */
void inoteDebugInit();

#endif

/* local variables: */
/* c-basic-offset: 2 */
/* end: */
