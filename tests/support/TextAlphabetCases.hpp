#ifndef LOKA_TEST_TEXT_ALPHABET_CASES_HPP
#define LOKA_TEST_TEXT_ALPHABET_CASES_HPP
#include "app/nodes/controls/TextEditorDocument.hpp"
namespace text_alphabet_cases
{
  using namespace loka::app;
  struct Case
  {
    const char *bytes;
    std::size_t length;
    EditorResult closed, open;
    std::size_t scalars; // complete code points when the bytes are well-formed, else 0
  };
  static const Case cases[] = {{"\303\251", 2, EDITOR_NON_ASCII, EDITOR_OK, 1},
                               {"\360\237\230\200", 4, EDITOR_NON_ASCII, EDITOR_OK, 1},
                               {"\200", 1, EDITOR_NON_ASCII, EDITOR_NON_ASCII, 0},
                               {"\300\200", 2, EDITOR_NON_ASCII, EDITOR_NON_ASCII, 0},
                               {"\355\240\200", 3, EDITOR_NON_ASCII, EDITOR_NON_ASCII, 0},
                               {"\303", 1, EDITOR_NON_ASCII, EDITOR_NON_ASCII, 0},
                               {"\0", 1, EDITOR_NON_ASCII, EDITOR_NON_ASCII, 1},
                               {"plain ASCII", 11, EDITOR_OK, EDITOR_OK, 11},
                               {"\364\220\200\200", 4, EDITOR_NON_ASCII, EDITOR_NON_ASCII, 0},
                               {"a\302\205b", 4, EDITOR_NON_ASCII, EDITOR_OK, 3},
                               {"a\342\200\250b", 5, EDITOR_NON_ASCII, EDITOR_OK, 3},
                               {"a\342\200\251b", 5, EDITOR_NON_ASCII, EDITOR_OK, 3},
                               {"\346\227\245\346\234\254", 6, EDITOR_NON_ASCII, EDITOR_OK, 2}};
  struct MacCaretCase
  {
    const char *before;
    const char *after;
    bool storage;
    unsigned inputUnits, outputUnits, byteColumn;
  };
  static const MacCaretCase macCarets[] = {{"\360\220\200\200", "\360\220\220\200", true, 0, 2, 4},
                                           {"\360\220\200\200", "\360\220\220\200", false, 1, 1, 0},
                                           {"x\360\220\200\200", "x\360\220\220\200", true, 1, 3, 5},
                                           {"x\360\220\200\200", "x", true, 1, 1, 1},
                                           {"\346\227\245", "\346\227\245\346\234\254", true, 1, 2, 6}};
  static const char *const macOpenPins[] = {
      "Japanese row insertion/deletion and caret round trip",
      "U+10000 to U+10400 storage caret byte 4 / unit 2; observed interior byte 0",
      "request boundary clamp and storage BEFORE hint",
      "multibyte run styling and reload round trip",
      "lone surrogate paste restores committed text",
      "NEL/U+2028/U+2029 logical row content and partial/full fragment visibility"};
  // The opening PR changes this one test-only selection.
  static EditorResult Case::*const expected = &Case::closed;
  struct Boundary
  {
    unsigned caseIndex;
    std::size_t column;
    bool boundary;
    std::size_t retreat;
  };
  // Boundary helpers require well-formed input; malformed cases have no offsets.
  static const Boundary boundaries[] = {{0, 0, true, 0},
                                        {0, 1, false, 0},
                                        {0, 2, true, 2},
                                        {1, 0, true, 0},
                                        {1, 1, false, 0},
                                        {1, 2, false, 0},
                                        {1, 3, false, 0},
                                        {1, 4, true, 4},
                                        {6, 0, true, 0},
                                        {6, 1, true, 1},
                                        {7, 0, true, 0},
                                        {7, 5, true, 5},
                                        {7, 11, true, 11}};
} // namespace text_alphabet_cases
#endif
