#ifndef INOTE_H
#define INOTE_H

#include <stdint.h>


struct input_t {
  char *text; // utf-8 text (raw or enriched with SSML tags or ECI annotations)
  uint32_t index; // index in the text buffer of the first byte to process
  uint32_t max_size; // max size in bytes of the text buffer
};

struct state_t {
  uint32_t punctuation_mode; // '0'=none, '1'=all or '2'=some
  uint32_t spelling_enabled; // 1 = spelling command already set
  uint32_t lang; // 0=unknown, otherwise probable language
  uint32_t *expected_lang; // array of the expected languages
  uint32_t max_expected_lang; // max number of elements of expected_lang
  uint32_t ssml_enabled; // 1 = SSML tags must be interpreted; 0 = no interpretation
};

struct output_t {
  uint8_t *buffer; // type-length-value buffer
  uint32_t length; // real length in bytes of the buffer
  uint32_t max_size; // max allocated size in bytes of the buffer
};

// input: utf-8 text 
// state: punctuation mode, current language,...
// output: 
// RETURN: 0 if no error
// 
uint32_t inote_get_annotated_text(struct input_t *input, struct state_t *state, struct output_t *output);

#endif
