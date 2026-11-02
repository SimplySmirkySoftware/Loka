#ifndef LOKA_TEST_TEXT_EDITOR_DIFF_CASES_HPP
#define LOKA_TEST_TEXT_EDITOR_DIFF_CASES_HPP
namespace text_editor_diff_cases
{
  const struct DiffCase
  {
    const char *before;
    const char *after;
    int first, oldCount, newCount;
  } cases[] = {{"aQcd\rabcd\rabcd", "abxcd\rabcd\rabcd", 0, 1, 1},
               {"a\rb", "a\rb", 2, 0, 0},
               {"abcd", "ab\rcd", 0, 1, 2},
               {"ab\rcd", "abcd", 0, 2, 1},
               {"abcd", "abcd\r", 0, 1, 2},
               {"abcd\r", "abcd", 0, 2, 1},
               {"abcd", "\rabcd", 0, 1, 2},
               {"\rabcd", "abcd", 0, 2, 1},
               {"", "\r", 0, 1, 2},
               {"a\rb\rc", "A\rb\rC", 0, 3, 3},
               {"a\rb\rc", "a\rB\rC", 1, 2, 2},
               {"a", "aQ\rR\rS", 0, 1, 3}};
} // namespace text_editor_diff_cases
#endif
