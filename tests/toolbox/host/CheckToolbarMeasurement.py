#!/usr/bin/env python3
"""Pin the native bracket order; exercise the three required wiring mutations."""
from pathlib import Path
import re
import importlib.util
import tempfile
import contextlib
import io
import unittest

ROOT = Path(__file__).resolve().parents[3]

def check(presenter, window, render, bracket):
    # Ignore whitespace but keep directives: the hooks must disappear in shipped builds.
    presenter, window, render, bracket = [re.sub(r'\s+', '', x) for x in (presenter, window, render, bracket)]
    assert '#ifdefTEST_BUILDthis->beginEraseForTesting();#endifEraseRect(&this->port_);' in presenter
    assert 'this->drawGrowBox();#ifdefTEST_BUILDpresenter.finishDrawForTesting();#endif' in window
    assert 'beginLayoutForTesting();#endifLayoutNode(rootNode_,state,this,0);#ifdefTEST_BUILDif(presenter)presenter->endLayoutForTesting();' in render
    assert 'this->measurement_.beginErase();' in presenter
    assert 'this->measurement_.finish();' in presenter
    assert 'this->start_=this->receiver_->now();' in bracket
    assert 'constunsignedlongend=this->receiver_->now();this->receiver_->accept(' in bracket

class Wiring(unittest.TestCase):
    def test_report_refuses_missing_and_overflow(self):
        spec = importlib.util.spec_from_file_location('report', ROOT / 'tests/toolbox/toolbar-measurement-report.py')
        report = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(report)
        rows = [f'toolbar_draw operation=step2-increment step={i} erase_us=17000 layout_us=50000 draws=1 operation_total_us=17000 verdict_sample=1 complete=1' for i in range(1, 11)]
        valid = ('toolbar_environment observed_resolution_us=1 app=3\n' + '\n'.join(rows) +
                 '\ntoolbar_summary verdict=PASS max_us=17000 count=10 operations=10 missing=0 overflow=0 limit_us=17000\n')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'LOG.TXT'
            path.write_text(valid)
            with contextlib.redirect_stdout(io.StringIO()):
                report.verdict(path)
            for invalid in (valid.replace(rows[0] + '\n', ''), valid.replace('overflow=0', 'overflow=1'),
                            valid.replace('max_us=17000', 'max_us=17001'), valid.replace('missing=0', 'missing=1'),
                            valid.split('toolbar_summary')[0], valid.replace('operation_total_us=17000', 'operation_total_us=0')):
                path.write_text(invalid)
                with self.assertRaises(ValueError):
                    report.verdict(path)

    def test_clock_and_empty_bracket(self):
        clock = (ROOT / 'tests/toolbox/src/ToolbarMeasurementClock.hpp').read_text()
        self.assertIn('Microseconds(&value);', clock)
        self.assertNotIn('TickCount(', clock)
        driver = re.sub(r'\s+', '', (ROOT / 'tests/toolbox/src/ToolbarMeasurementMain.cpp').read_text())
        self.assertIn('constunsignedlongstart=this->now();constunsignedlongend=this->now();', driver)

    def test_native_and_mutations(self):
        files = [ROOT / 'apple/toolbox/src' / name for name in (
            'ToolboxFullDrawPresenter.hpp', 'ToolboxWindow.cpp', 'ToolboxRender.cpp', 'ToolboxDrawMeasurement.hpp')]
        source = [p.read_text() for p in files]
        check(*source)
        for index, before, after in (
            (3, 'this->start_ = this->receiver_->now();', 'this->start_ = 0;'),
            (3, 'const unsigned long end = this->receiver_->now();', 'const unsigned long end = 0;'),
            (0, 'this->beginEraseForTesting();', ''),
        ):
            mutated = source.copy()
            self.assertIn(before, mutated[index])
            mutated[index] = mutated[index].replace(before, after)
            with self.assertRaises(AssertionError):
                check(*mutated)
        moved = source.copy()
        moved[0] = moved[0].replace('    this->beginEraseForTesting();', '')
        moved[0] = moved[0].replace('    EraseRect(&this->port_);',
                                  '    EraseRect(&this->port_);\n#ifdef TEST_BUILD\n    this->beginEraseForTesting();\n#endif')
        with self.assertRaises(AssertionError):
            check(*moved)

if __name__ == '__main__':
    unittest.main()
