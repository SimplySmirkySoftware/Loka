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
  };
  static const Case cases[] = {{"\303\251", 2, EDITOR_NON_ASCII, EDITOR_OK},
                               {"\360\237\230\200", 4, EDITOR_NON_ASCII, EDITOR_OK},
                               {"\200", 1, EDITOR_NON_ASCII, EDITOR_NON_ASCII},
                               {"\300\200", 2, EDITOR_NON_ASCII, EDITOR_NON_ASCII},
                               {"\355\240\200", 3, EDITOR_NON_ASCII, EDITOR_NON_ASCII},
                               {"\303", 1, EDITOR_NON_ASCII, EDITOR_NON_ASCII},
                               {"\0", 1, EDITOR_NON_ASCII, EDITOR_NON_ASCII},
                               {"plain ASCII", 11, EDITOR_OK, EDITOR_OK},
                               {"\364\220\200\200", 4, EDITOR_NON_ASCII, EDITOR_NON_ASCII}};
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
