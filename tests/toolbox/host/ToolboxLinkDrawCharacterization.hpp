#ifndef LOKA_TOOLBOX_LINK_DRAW_CHARACTERIZATION_HPP
#define LOKA_TOOLBOX_LINK_DRAW_CHARACTERIZATION_HPP
namespace
{
  void LinkDrawCharacterization()
  {
    Pin("E3FullDrawTrace");
    SpanCharacterization();
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    const AttributedString values[] = {Styled(loka::core::String(std::string(7000, 'a')), Bold)
                                           + Styled("b", Italic + White),
                                       Styled("ab", Bold) + Styled("cdefgh", Italic + White),
                                       Styled("abcdefgh\n", FontSize<18>()) + Styled("z", Bold),
                                       Styled("abcd", Bold) + Styled("efgh", Italic + White)};
    const short widths[] = {0, 22, 5, 25};
    const char *traces[] = {
        "FSTIMDMDIFSTIMDIFST", "FSTWFSTIMDIFSTIIFSTMIDIFST", "FSTWFSTIMDIFST", "FSTWFSTIMDIFSTMIDIFST"};
    const long arguments0[] = {3, 12, bold,   blackColor, 32760, 32767, 6553, 32767,      32767, 447, blackColor,
                               3, 12, italic, whiteColor, 32767, 32767, 1,    blackColor, 3,     12,  0};
    const long arguments1[] = {3,      12,         italic, 3,          12,     bold,       blackColor, 7,  23,
                               2,      blackColor, 3,      12,         italic, whiteColor, blackColor, 3,  12,
                               italic, 17,         23,     whiteColor, 3,      blackColor, 3,          12, 0};
    const long arguments2[] = {3, 18, 0, 3, 12, bold, blackColor, 7, 46, 1, blackColor, 3, 12, 0};
    const long arguments3[] = {3, 12, italic, 3,  12, bold,       blackColor, 10,         23, 2,  blackColor,
                               3, 12, italic, 20, 23, whiteColor, 3,          blackColor, 3,  12, 0};
    const long *arguments[] = {arguments0, arguments1, arguments2, arguments3};
    const std::size_t counts[] = {sizeof(arguments0) / sizeof(long),
                                  sizeof(arguments1) / sizeof(long),
                                  sizeof(arguments2) / sizeof(long),
                                  sizeof(arguments3) / sizeof(long)};
    for (int i = 0; i < 4; ++i)
    {
      ToolboxAttributedTextTable table;
      const BlockStyle block = BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS).align(TEXT_ALIGN_RIGHT);
      LOKA_VERIFY(table.build(values[i], block, widths[i], controller));
      toolbox_host::reset();
      LOKA_VERIFY(table.draw(i == 0 ? 32760 : 7, i == 0 ? 32760 : 11, controller, block, widths[i]));
      std::printf("E3 %d trace %s\n", i, toolbox_host::textTrace.c_str());
      LOKA_VERIFY(toolbox_host::textTrace == traces[i]);
      LOKA_VERIFY(toolbox_host::textArguments.size() == counts[i]);
      for (std::size_t argument = 0; argument < counts[i]; ++argument)
        LOKA_VERIFY(toolbox_host::textArguments[argument] == arguments[i][argument]);
      if (i == 0)
      {
        LOKA_VERIFY(toolbox_host::draws.size() == 3);
        VerifySpanDraw(0, std::string(6553, 'a').c_str(), 32760, 32767, 12, bold, blackColor);
        VerifySpanDraw(1, std::string(447, 'a').c_str(), 32767, 32767, 12, bold, blackColor);
        VerifySpanDraw(2, "b", 32767, 32767, 12, italic, whiteColor);
      }
      if (i == 1)
      {
        VerifySpanDraw(0, "ab", 7, 23, 12, bold, blackColor);
        VerifySpanDraw(1, "...", 17, 23, 12, italic, whiteColor);
      }
      if (i == 2)
        VerifySpanDraw(0, "z", 7, 46, 12, bold, blackColor);
      if (i == 3)
      {
        VerifySpanDraw(0, "ab", 10, 23, 12, bold, blackColor);
        VerifySpanDraw(1, "...", 20, 23, 12, italic, whiteColor);
      }
      LOKA_VERIFY(window.port.txFont == 3 && window.port.txSize == 12 && window.port.txFace == 0
                  && window.port.fgColor == blackColor);
    }
  }
} // namespace
#endif
