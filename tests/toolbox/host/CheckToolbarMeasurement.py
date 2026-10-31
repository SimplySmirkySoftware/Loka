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

    def test_row_report(self):
        spec = importlib.util.spec_from_file_location('report', ROOT / 'tests/toolbox/toolbar-measurement-report.py')
        report = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(report)
        lines = ['toolbar_row_summary complete=1 cases=7',
                 'toolbar_row_environment clock=Microseconds observed_resolution_us=1']
        for name, k, scans in [('k1', 1, 0), ('k2', 2, 2), ('k4', 4, 2), ('fit', 4, 0), ('floor', 4, 1), ('b-equals-s', 4, 0), ('aligned-title', 6, 0)]:
            offer, occupied = (27, 28) if name == 'b-equals-s' else (10, 10)
            source = 'synthetic-unit' if name == 'b-equals-s' else 'toolbox'
            lines += [f'toolbar_row case={name} repetitions=31 median_us=3 maximum_us=5 offer={offer} occupied={occupied}',
                      f'toolbar_row_inputs case={name} k={k} scans={scans} source={source}']
        lines += ['toolbar_row_height case=aligned-title answered=1 query=23 leaf=23 equal=1']
        valid = '\n'.join(lines)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'LOG.TXT'
            path.write_text(valid)
            with contextlib.redirect_stdout(io.StringIO()):
                report.rows(path)
            base = valid.replace('answered=1 query=23 leaf=23 equal=1',
                                 'answered=0 query=-1 leaf=23 equal=0')
            path.write_text(base)
            with contextlib.redirect_stdout(io.StringIO()):
                report.rows(path)
            for invalid in (base.replace('equal=0', 'equal=1'),
                            base.replace('query=-1', 'query=0'),
                            valid.replace('query=23', 'query=22'),
                            valid.replace('equal=1', 'equal=0'),
                            valid.replace(lines[-1], ''),
                            valid.replace('repetitions=31', 'repetitions=2'),
                            valid.replace('complete=1', 'complete=0'),
                            valid.replace('maximum_us=5', 'maximum_us=1'),
                            valid.replace('case=k4 k=4 scans=2', 'case=k4 k=4 scans=1'),
                            valid.replace('occupied=10', 'occupied=11'),
                            valid + '\n' + lines[2],
                            valid.replace('source=toolbox', 'source=synthetic-unit'),
                            valid.replace('observed_resolution_us=1', 'observed_resolution_us=0')):
                path.write_text(invalid)
                with self.assertRaises(ValueError):
                    report.rows(path)

    def test_clock_and_empty_bracket(self):
        clock = (ROOT / 'tests/toolbox/src/ToolbarMeasurementClock.hpp').read_text()
        self.assertIn('Microseconds(&value);', clock)
        self.assertNotIn('TickCount(', clock)
        driver = re.sub(r'\s+', '', (ROOT / 'tests/toolbox/src/ToolbarMeasurementMain.cpp').read_text())
        self.assertIn('constunsignedlongstart=this->now();constunsignedlongend=this->now();', driver)

    def test_native_and_mutations(self):
        files = [ROOT / 'apple/toolbox/src' / name for name in (
            'ToolboxFullDrawPresenter.hpp', 'ToolboxWindowPaint.cpp', 'ToolboxRender.cpp', 'ToolboxDrawMeasurement.hpp')]
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
