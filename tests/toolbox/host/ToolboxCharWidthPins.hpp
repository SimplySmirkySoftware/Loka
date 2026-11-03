#ifndef LOKA_TOOLBOX_CHAR_WIDTH_PINS_HPP
#define LOKA_TOOLBOX_CHAR_WIDTH_PINS_HPP
#include "ToolboxUtf8TestString.hpp"
#include <climits>

namespace
{
  void CharWidthFailurePins()
  {
    for (int mode = 1; mode <= 4; ++mode)
    {
      Pin("CharWidthFailureSameScopeAndTerminalRefusal");
      ToolboxWindow window, other;
      HostPortScope current(other.window());
      ToolboxScenePlatformController controller(&window);
      unsigned copies = 0;
      const loka::core::String text(
          loka::core::Managed<loka::platform::String>::Wrap(new ToolboxUtf8TestString("Abc", false, false, copies)));
      toolbox_host::reset();
      toolbox_host::positionMode = mode;
      short width = -1;
      LOKA_VERIFY(controller.measureTextAdvance(FontSize<18>() + Bold, text, width) && width == 21);
      LOKA_VERIFY(copies == 1 && toolbox_host::measures == 1 && toolbox_host::widths == 1);
      LOKA_VERIFY(toolbox_host::fonts == 2 && controller.cursor.entries == 1 && controller.cursor.depth == 0);
      LOKA_VERIFY(toolbox_host::widthPayloads[0] == "Abc");
      LOKA_VERIFY(window.port.txSize == 12 && window.port.txFont == 3 && window.port.txFace == 0);
      GrafPtr selected = 0;
      GetPort(&selected);
      LOKA_VERIFY(selected == other.window());
      toolbox_host::positionMode = 0;
      const unsigned classifications = toolbox_host::fontScriptReads;
      LOKA_VERIFY(controller.measureTextAdvance(FontSize<18>() + Bold, text, width) && width == 21);
      LOKA_VERIFY(copies == 2 && toolbox_host::measures == 1 && toolbox_host::widths == 2);
      LOKA_VERIFY(toolbox_host::fontScriptReads == classifications);
    }
  }
  void CharWidthAllocationPins()
  {
    for (int failure = 0; failure < 3; ++failure)
    {
      Pin("CharWidthAllocationRefusalRecordsNothingAndRetries");
      unsigned copies = 0;
      const loka::core::String text(
          loka::core::Managed<loka::platform::String>::Wrap(new ToolboxUtf8TestString("Abc", false, false, copies)));
      loka::core::testing::failLokaAllocRaw("ToolboxCharWidthCache", failure == 0 ? "ToolboxCharWidths" : "Slot", 1);
      {
        ToolboxWindow window, other;
        HostPortScope current(other.window());
        ToolboxScenePlatformController controller(&window);
        const int live = loka::core::testing::lokaAllocRawLive();
        const int attempts = loka::core::testing::lokaAllocRawAttempts();
        const unsigned classifications = toolbox_host::fontScriptReads;
        const ToolboxTextSelection key = ToolboxTextSelection::resolve(ToolboxTextFontDescriptor(FontSize<18>() + Bold),
                                                                       ToolboxTextPortFont::read(window.window()));
        toolbox_host::reset();
        toolbox_host::positionMode = failure == 2 ? 1 : 0;
        short width = -1;
        LOKA_VERIFY(controller.measureTextAdvance(FontSize<18>() + Bold, text, width) && width == 21);
        LOKA_VERIFY(!controller.charWidthCache()->find(key));
        LOKA_VERIFY(loka::core::testing::lokaAllocRawLive() == live);
        LOKA_VERIFY(toolbox_host::measures == 1 && toolbox_host::widths == 1 && copies == 1);
        LOKA_VERIFY(toolbox_host::fonts == 2 && controller.cursor.entries == 1 && controller.cursor.depth == 0);
        LOKA_VERIFY(toolbox_host::widthPayloads[0] == "Abc");
        LOKA_VERIFY(window.port.txFont == 3 && window.port.txSize == 12 && window.port.txFace == 0);
        GrafPtr selected = 0;
        GetPort(&selected);
        LOKA_VERIFY(selected == other.window());
        toolbox_host::positionMode = 0;
        LOKA_VERIFY(controller.measureTextAdvance(FontSize<18>() + Bold, text, width) && width == 21);
        LOKA_VERIFY(controller.charWidthCache()->find(key));
        LOKA_VERIFY(toolbox_host::measures == 2 && toolbox_host::widths == 1 && copies == 2);
        LOKA_VERIFY(toolbox_host::fontScriptReads == classifications + 2);
        LOKA_VERIFY(loka::core::testing::lokaAllocRawLive() == live + 2);
        LOKA_VERIFY(loka::core::testing::lokaAllocRawAttempts() == attempts + (failure == 1 ? 4 : 3));
        LOKA_VERIFY(controller.measureTextAdvance(FontSize<18>() + Bold, text, width) && width == 21);
        LOKA_VERIFY(toolbox_host::measures == 2 && toolbox_host::widths == 1 && copies == 3);
        LOKA_VERIFY(toolbox_host::fonts == 4 && controller.cursor.entries == 2 && controller.cursor.depth == 0);
      }
      LOKA_VERIFY(loka::core::testing::lokaAllocRawLive() == 0);
      loka::core::testing::allowLokaAllocRaw();
    }
    Pin("CharWidthRefusedSlotAllocationReclassifiesWithoutBuild");
    const loka::core::String text = loka::core::String::Literal("Abc");
    toolbox_host::fontScript = 5;
    loka::core::testing::failLokaAllocRaw("ToolboxCharWidthCache", "Slot", 1);
    {
      ToolboxWindow window;
      ToolboxScenePlatformController controller(&window);
      const int live = loka::core::testing::lokaAllocRawLive();
      const int attempts = loka::core::testing::lokaAllocRawAttempts();
      const unsigned classifications = toolbox_host::fontScriptReads;
      const ToolboxTextSelection key =
          ToolboxTextSelection::resolve(ToolboxTextFontDescriptor(), ToolboxTextPortFont::read(window.window()));
      toolbox_host::reset();
      short width = 0;
      LOKA_VERIFY(controller.measureTextAdvance(TextStyle(), text, width) && width == 12);
      LOKA_VERIFY(!controller.charWidthCache()->find(key));
      LOKA_VERIFY(loka::core::testing::lokaAllocRawLive() == live);
      LOKA_VERIFY(controller.measureTextAdvance(TextStyle(), text, width) && width == 12);
      LOKA_VERIFY(controller.charWidthCache()->find(key)->kind == TOOLBOX_CHAR_REFUSED);
      LOKA_VERIFY(controller.measureTextAdvance(TextStyle(), text, width) && width == 12);
      LOKA_VERIFY(toolbox_host::fontScriptReads == classifications + 2);
      LOKA_VERIFY(toolbox_host::measures == 0 && toolbox_host::widths == 3);
      LOKA_VERIFY(loka::core::testing::lokaAllocRawAttempts() == attempts + 2);
      LOKA_VERIFY(loka::core::testing::lokaAllocRawLive() == live + 1);
    }
    LOKA_VERIFY(loka::core::testing::lokaAllocRawLive() == 0);
    loka::core::testing::allowLokaAllocRaw();
    toolbox_host::fontScript = smRoman;
  }
  void CharWidthSelectionProofPin()
  {
    Pin("CharWidthSelectedScopeProof");
    ToolboxWindow window, other;
    HostPortScope current(other.window());
    ToolboxScenePlatformController controller(&window);
    const ToolboxTextSelection key =
        ToolboxTextSelection::resolve(ToolboxTextFontDescriptor(), ToolboxTextPortFont::read(window.window()));
    ToolboxTextMeasureScope selected(controller, key);
    LOKA_VERIFY(selected.applies(key));
    SetPort(other.window());
    LOKA_VERIFY(!selected.applies(key));
    SetPort(window.window());
    TextFace(bold);
    LOKA_VERIFY(!selected.applies(key));
    TextFace(0);
    LOKA_VERIFY(selected.applies(key));
  }
  void CharWidthGuardsPins()
  {
    for (int guard = 0; guard < 4; ++guard)
      for (int warm = 0; warm < 2; ++warm)
      {
        Pin(warm ? "CharWidthGuardedHitRestoresHit" : "CharWidthUnsafeFirstConsumesNoSlot");
        ToolboxWindow window;
        ToolboxScenePlatformController controller(&window);
        const loka::core::String text = loka::core::String::Literal("Abc");
        short width = 0;
        toolbox_host::reset();
        if (warm)
          LOKA_VERIFY(controller.measureTextAdvance(FontSize<18>(), text, width) && width == 18);
        QDProcs procs;
        switch (guard)
        {
        case 0:
          window.port.spExtra = 1;
          break;
        case 1:
          toolbox_host::fractionalWidths = true;
          break;
        case 2:
          window.port.portBits.rowBytes = static_cast<short>(0xC000);
          window.port.chExtra = 1;
          break;
        case 3:
          window.port.grafProcs = &procs;
          break;
        }
        const unsigned classifications = toolbox_host::fontScriptReads;
        LOKA_VERIFY(controller.measureTextAdvance(FontSize<18>(), text, width) && width == 18);
        LOKA_VERIFY(toolbox_host::widths == 1 && toolbox_host::measures == warm);
        LOKA_VERIFY(toolbox_host::fontScriptReads == classifications);
        window.port.spExtra = 0;
        window.port.chExtra = 0;
        window.port.grafProcs = 0;
        window.port.portBits.rowBytes = 0;
        toolbox_host::fractionalWidths = false;
        LOKA_VERIFY(controller.measureTextAdvance(FontSize<18>(), text, width) && width == 18);
        LOKA_VERIFY(toolbox_host::measures == 1 && toolbox_host::widths == 1);
        const int entries = controller.cursor.entries;
        LOKA_VERIFY(controller.measureTextAdvance(FontSize<18>(), text, width) && width == 18);
        LOKA_VERIFY(controller.cursor.entries == entries && toolbox_host::fonts == 4);
      }
  }
  void CharWidthCapacityPins()
  {
    for (int japanese = 0; japanese < 2; ++japanese)
    {
      Pin("CharWidthOutOfRangeThenEightAndNine");
      toolbox_host::fontScript = japanese ? smJapanese : smRoman;
      ToolboxWindow window;
      ToolboxScenePlatformController controller(&window);
      short width = 0;
      toolbox_host::reset();
      const loka::core::String outside =
          japanese ? loka::core::String::Literal("\xc3\xa9") : loka::core::String::Literal("\x7f");
      LOKA_VERIFY(controller.measureTextAdvance(TextStyle(), outside, width));
      LOKA_VERIFY(toolbox_host::widths == 1 && toolbox_host::measures == 0);
      for (short key = 0; key < 8; ++key)
      {
        window.port.txFont = key + 3;
        LOKA_VERIFY(controller.measureTextAdvance(TextStyle(), loka::core::String::Literal("Ab"), width) && width == 8);
      }
      LOKA_VERIFY(toolbox_host::measures == 8 && toolbox_host::widths == 1);
      const std::string &bytes = toolbox_host::measurePayloads[0];
      LOKA_VERIFY(bytes.size() == (japanese ? 95U : 223U));
      LOKA_VERIFY(static_cast<unsigned char>(bytes[0]) == 32);
      LOKA_VERIFY(static_cast<unsigned char>(bytes[bytes.size() - 1]) == (japanese ? 126 : 255));
      window.port.txFont = 20;
      const unsigned classifications = toolbox_host::fontScriptReads;
      LOKA_VERIFY(controller.measureTextAdvance(TextStyle(), loka::core::String::Literal("Ab"), width) && width == 8);
      LOKA_VERIFY(toolbox_host::measures == 8 && toolbox_host::widths == 2);
      LOKA_VERIFY(toolbox_host::fontScriptReads == classifications);
      window.port.txFont = 10;
      LOKA_VERIFY(controller.measureTextAdvance(TextStyle(), loka::core::String::Literal("different"), width)
                  && width == 36);
      LOKA_VERIFY(toolbox_host::measures == 8 && toolbox_host::widths == 2);
      toolbox_host::fontScript = smRoman;
    }
  }
  void CharWidthSemanticsPins()
  {
    Pin("CharWidthEmptyZeroNegativeAndOverflowRetainsTable");
    ToolboxWindow window;
    ToolboxScenePlatformController controller(&window);
    short width = -1;
    toolbox_host::reset();
    LOKA_VERIFY(controller.measureTextAdvance(TextStyle(), loka::core::String::Literal(""), width) && width == 0);
    window.port.txFont = 4;
    window.port.txSize = 2; // Equal positions are valid, with zero advances.
    LOKA_VERIFY(controller.measureTextAdvance(TextStyle(), loka::core::String::Literal("abc"), width) && width == 0);
    LOKA_VERIFY(toolbox_host::widths == 0 && toolbox_host::measures == 2);
    loka::core::MutableState<loka::core::String> value(loka::core::String::Literal("abc"));
    TextNode node((TextProps(&value)));
    width = 99;
    LOKA_VERIFY(!controller.queryNaturalWidth(&node, width) && width == 99);
    window.port.txFont = 5;
    window.port.txSize = 420; // 223*140 fits; 255*140 overflows short.
    const loka::core::String longText(std::string(255, 'x'));
    LOKA_VERIFY(controller.measureTextAdvance(TextStyle(), longText, width) && width == static_cast<short>(35700));
    LOKA_VERIFY(toolbox_host::measures == 3 && toolbox_host::widths == 1);
    LOKA_VERIFY(controller.measureTextAdvance(TextStyle(), longText, width) && width == static_cast<short>(35700));
    LOKA_VERIFY(toolbox_host::measures == 3 && toolbox_host::widths == 2);
    LOKA_VERIFY(controller.measureTextAdvance(TextStyle(), loka::core::String::Literal("x"), width) && width == 140);
    LOKA_VERIFY(toolbox_host::measures == 3 && toolbox_host::widths == 2);
    value.set(longText);
    width = 99;
    LOKA_VERIFY(!controller.queryNaturalWidth(&node, width) && width == 99);

    Pin("CharWidthEncodedLengthsNulAndLateNewline");
    window.port.txSize = 12;
    for (unsigned length = 254; length <= 256; ++length)
    {
      LOKA_VERIFY(controller.measureTextAdvance(TextStyle(), loka::core::String(std::string(length, 'x')), width));
      LOKA_VERIFY(width == static_cast<short>((length > 255 ? 255 : length) * 4));
    }
    const int native = toolbox_host::widths;
    LOKA_VERIFY(controller.measureTextAdvance(TextStyle(), loka::core::String(std::string("a\0b", 3)), width));
    LOKA_VERIFY(width == 12 && toolbox_host::widths == native + 1);
    value.set(loka::core::String(std::string(255, 'x') + "\n"));
    width = 99;
    LOKA_VERIFY(!controller.queryNaturalWidth(&node, width) && width == 99);
    LOKA_VERIFY(toolbox_host::widths == native + 1);

    Pin("CharWidthAbsentAndEncodeFailureBeforeHit");
    unsigned copies = 0;
    const loka::core::String refused(
        loka::core::Managed<loka::platform::String>::Wrap(new ToolboxUtf8TestString("x", false, true, copies)));
    width = 99;
    LOKA_VERIFY(!controller.measureTextAdvance(TextStyle(), refused, width) && width == 99 && copies == 1);
    ToolboxScenePlatformController absent(0);
    LOKA_VERIFY(!absent.measureTextAdvance(TextStyle(), longText, width) && width == 99);
  }
  void CharWidthClassificationPins()
  {
    for (int refused = 0; refused < 2; ++refused)
    {
      Pin("CharWidthUnsupportedOrForcedTerminalRefusal");
      toolbox_host::fontScript = refused ? smRoman : 5;
      toolbox_host::fontForce = refused;
      ToolboxWindow window;
      ToolboxScenePlatformController controller(&window);
      toolbox_host::reset();
      short width = 0;
      const unsigned classifications = toolbox_host::fontScriptReads;
      for (int query = 0; query < 2; ++query)
        LOKA_VERIFY(controller.measureTextAdvance(TextStyle(), loka::core::String::Literal("abc"), width)
                    && width == 12);
      LOKA_VERIFY(toolbox_host::fontScriptReads == classifications + 1);
      LOKA_VERIFY(toolbox_host::widths == 2 && toolbox_host::measures == 0);
      toolbox_host::fontForce = 0;
      toolbox_host::fontScript = smRoman;
    }
    Pin("CharWidthAliasesFacesAndAppSharing");
    ToolboxCharWidthCache shared;
    ToolboxWindow window, other;
    HostPortScope current(other.window());
    ToolboxScenePlatformController controller(&window, 0, &shared), sibling(&other, 0, &shared);
    toolbox_host::reset();
    short width = 0;
    toolbox_host::systemFont = 16384;
    window.port.txFont = 0;
    LOKA_VERIFY(controller.measureTextAdvance(TextStyle(), loka::core::String::Literal("ab"), width) && width == 8);
    LOKA_VERIFY(toolbox_host::lastScriptFont == 16384);
    window.port.txFont = 1;
    LOKA_VERIFY(controller.measureTextAdvance(TextStyle(), loka::core::String::Literal("ab"), width) && width == 8);
    LOKA_VERIFY(toolbox_host::lastScriptFont == 1);
    other.port.txFont = 1;
    LOKA_VERIFY(sibling.measureTextAdvance(TextStyle(), loka::core::String::Literal("abcd"), width) && width == 16);
    LOKA_VERIFY(toolbox_host::measures == 2 && toolbox_host::widths == 0);
    LOKA_VERIFY(controller.measureTextAdvance(Bold, loka::core::String::Literal("ab"), width) && width == 10);
    LOKA_VERIFY(toolbox_host::measures == 3);
    toolbox_host::defaultFontSize = 10;
    window.port.txSize = 0;
    Pin("CharWidthResolvedSelectionReachesScope");
    const unsigned defaults = toolbox_host::defaultFontReads;
    LOKA_VERIFY(controller.measureTextAdvance(TextStyle(), loka::core::String::Literal("ab"), width) && width == 6);
    LOKA_VERIFY(toolbox_host::defaultFontReads == defaults + 2);
    toolbox_host::defaultFontSize = 12;
    LOKA_VERIFY(controller.measureTextAdvance(TextStyle(), loka::core::String::Literal("ab"), width) && width == 8);
    LOKA_VERIFY(toolbox_host::measures == 4);
    toolbox_host::systemFont = 0;
    Pin("CharWidthRomanHighByteRetainsTable");
    window.port.txFont = 1;
    window.port.txSize = 12;
    const int builds = toolbox_host::measures;
    LOKA_VERIFY(controller.measureTextAdvance(TextStyle(), loka::core::String::Literal("\xc3\xa9"), width)
                && width == 4);
    LOKA_VERIFY(toolbox_host::measures == builds && toolbox_host::widths == 0);
  }
  void CharWidthLifetimePins()
  {
    Pin("CharWidthAppLifetimeReleasesCompletedTables");
    const loka::core::String text = loka::core::String::Literal("x");
    loka::core::testing::failLokaAllocRaw("ToolboxCharWidthCache", "ToolboxCharWidths", 0);
    {
      ToolboxWindow window;
      ToolboxScenePlatformController controller(&window);
      short width = 0;
      const int attempts = loka::core::testing::lokaAllocRawAttempts();
      const int live = loka::core::testing::lokaAllocRawLive();
      LOKA_VERIFY(controller.measureTextAdvance(TextStyle(), text, width));
      LOKA_VERIFY(loka::core::testing::lokaAllocRawAttempts() == attempts + 2);
      LOKA_VERIFY(loka::core::testing::lokaAllocRawLive() == live + 2);
    }
    LOKA_VERIFY(loka::core::testing::lokaAllocRawLive() == 0);
    loka::core::testing::allowLokaAllocRaw();
  }
  void CharWidthPins()
  {
    CharWidthFailurePins();
    CharWidthAllocationPins();
    CharWidthSelectionProofPin();
    CharWidthGuardsPins();
    CharWidthCapacityPins();
    CharWidthSemanticsPins();
    CharWidthClassificationPins();
    CharWidthLifetimePins();
  }
} // namespace
#endif
