#!/usr/bin/env python3
"""Native configure-only pins for the production Web catalog producer."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def bracket(value):
    return '[==[' + value + ']==]'


class WebAppCatalogTest(unittest.TestCase):
    def setUp(self):
        parent = ROOT / 'build' / 'catalog-tests'
        parent.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=parent)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'source'
        self.binary = Path(self.temp.name) / 'binary'
        (self.root / 'cmake').mkdir(parents=True)
        shutil.copyfile(ROOT / 'cmake/LokaWebApp.cmake', self.root / 'cmake/LokaWebApp.cmake')
        (self.root / 'web/entry').mkdir(parents=True)
        (self.root / 'web/entry/WebEntry.cpp').write_text('int main() { return 0; }\n')
        self.header = '''cmake_minimum_required(VERSION 3.15)
project(CatalogFixture LANGUAGES CXX)
include(cmake/LokaWebApp.cmake)
function(loka_enable_warnings TARGET)
endfunction()
add_library(LokaWebCore INTERFACE)
'''

    def configure(self, body, error=None):
        (self.root / 'CMakeLists.txt').write_text(self.header + body + '\nloka_finalize_web_apps()\n')
        result = subprocess.run(['cmake', '-S', str(self.root), '-B', str(self.binary),
                                 '-DCMAKE_BUILD_TYPE=Debug'],
                                capture_output=True, text=True)
        output = result.stdout + result.stderr
        if error is not None:
            self.assertNotEqual(result.returncode, 0, 'expected refusal: ' + error)
            self.assertIn(error, output)
            self.assertFalse((self.binary / 'loka-web-apps.json').exists())
            return
        self.assertEqual(result.returncode, 0, output)
        return json.loads((self.binary / 'loka-web-apps.json').read_text())

    def app(self, directory='example/Hello World 日本', target='HelloTarget', export='HelloFactory', extra=''):
        path = self.root / directory
        path.mkdir(parents=True, exist_ok=True)
        (path / 'CMakeLists.txt').write_text(
            'project(NestedProject LANGUAGES CXX)\n'
            f'loka_add_web_app({target} {export})\n' + extra)
        return f'add_subdirectory({bracket(directory)})\n'

    def test_membership_schema_nested_project(self):
        body = self.app() + self.app('web/driver', 'Counter', 'CounterFactory')
        body += self.app('example-other/Decoy', 'Decoy', 'DecoyFactory')
        body += self.app('example', 'ExampleRoot', 'RootFactory')
        data = self.configure(body)
        self.assertEqual(data, {'version': 1, 'apps': [{
            'id': 'HelloTarget', 'name': 'Hello World 日本', 'target': 'HelloTarget',
            'exportName': 'HelloFactory', 'sourceDir': 'example/Hello World 日本',
            'js': 'example/Hello World 日本/HelloFactory.js'}]})

    def test_empty_and_replacement(self):
        body = self.app()
        self.assertEqual(len(self.configure(body)['apps']), 1)
        # Stale artifacts must not restore a removed registration.
        artifact = self.binary / 'example/Hello World 日本/HelloFactory.js'
        artifact.write_text('stale')
        self.assertEqual(self.configure(''), {'version': 1, 'apps': []})
        self.assertTrue(artifact.exists())

    def test_semicolon_registration(self):
        extra = '''set(CMAKE_CURRENT_SOURCE_DIR "${LOKA_WEB_REPOSITORY_ROOT}/example/Semi;colon 日本")
_loka_web_register(Second SecondFactory "${CMAKE_CURRENT_BINARY_DIR}")
'''
        body = self.app(extra='add_executable(Second "${LOKA_WEB_RAIL_DIR}/entry/WebEntry.cpp")\n'
                        'set_target_properties(Second PROPERTIES OUTPUT_NAME SecondFactory '
                        'RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" PREFIX "" SUFFIX ".js")\n' + extra)
        data = self.configure(body)
        self.assertEqual(len(data['apps']), 2)
        self.assertEqual(data['apps'][1]['name'], 'Semi;colon 日本')
        self.assertEqual(data['apps'][1]['sourceDir'], 'example/Semi;colon 日本')

    def test_serializer(self):
        value = 'space 日本; "quote" \\ slash\b\t\n\f\r'
        body = f'_loka_web_json_string(encoded {bracket(value)})\n'
        body += 'file(WRITE "${CMAKE_BINARY_DIR}/scalar.json" "${encoded}")\n'
        self.configure(body)
        self.assertEqual(json.loads((self.binary / 'scalar.json').read_text()), value)

    def test_unsupported_control(self):
        self.configure('string(ASCII 1 bad)\n_loka_web_json_string(encoded "${bad}")',
                       'Web catalog: unsupported control character 1')

    def test_duplicate_identity(self):
        body = self.app(extra='_loka_web_register(HelloTarget HelloFactory "${CMAKE_CURRENT_BINARY_DIR}")\n')
        self.configure(body, 'Web catalog: duplicate identity: HelloTarget')

    def test_target_name(self):
        self.configure(self.app(target='Hello-Target'), 'Web catalog: invalid target name')

    def test_export_name(self):
        self.configure(self.app(export='7Factory'), 'Web catalog: invalid export name')

    def test_source_membership_guard(self):
        self.configure(self.app() + 'set_property(GLOBAL PROPERTY LOKA_WEB_APP_1_sourceDir other/Hello)\n',
                       'Web catalog: source directory must be below example/')

    def test_path_guard(self):
        self.configure(self.app() + 'set_property(GLOBAL PROPERTY LOKA_WEB_APP_1_sourceDir example/../Hello)\n',
                       'Web catalog: invalid relative path')

    def test_path_forms(self):
        for value in ('', '/absolute', 'C:/drive', 'example/../escape',
                      'example/back\\slash', 'example/$<CONFIG>'):
            with self.subTest(value=value):
                self.configure(f'_loka_web_relative_path({bracket(value)})',
                               'Web catalog: invalid relative path')

    def test_binary_base(self):
        self.configure(self.app() + '''set_property(GLOBAL PROPERTY LOKA_WEB_APP_1_outputDir "${CMAKE_BINARY_DIR}/../outside")
set_target_properties(HelloTarget PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/../outside")
''', 'Web catalog: invalid relative path')

    def test_output_property(self):
        self.configure(self.app() + 'set_target_properties(HelloTarget PROPERTIES OUTPUT_NAME Changed)\n',
                       'Web catalog: output policy override: OUTPUT_NAME')

    def test_output_property_family(self):
        for prop, value in (('RUNTIME_OUTPUT_DIRECTORY', '${CMAKE_BINARY_DIR}/other'),
                            ('PREFIX', 'prefix'), ('SUFFIX', '.html'),
                            ('OUTPUT_NAME', '$<CONFIG>')):
            with self.subTest(prop=prop):
                self.configure(self.app() + f'set_target_properties(HelloTarget PROPERTIES {prop} "{value}")\n',
                               'Web catalog: output policy override: ' + prop)

    def test_output_alternate(self):
        self.configure(self.app() + 'set_target_properties(HelloTarget PROPERTIES RUNTIME_OUTPUT_NAME Changed)\n',
                       'Web catalog: output policy override: RUNTIME_OUTPUT_NAME')

    def test_output_config_family(self):
        for prop in ('OUTPUT_NAME_DEBUG', 'DEBUG_OUTPUT_NAME', 'RUNTIME_OUTPUT_NAME_DEBUG',
                     'RUNTIME_OUTPUT_DIRECTORY_DEBUG', 'DEBUG_POSTFIX'):
            with self.subTest(prop=prop):
                self.configure(self.app() + f'set_target_properties(HelloTarget PROPERTIES {prop} Changed)\n',
                               'Web catalog: output policy override: ' + prop)

    def test_multi_configuration(self):
        self.configure('set(CMAKE_CONFIGURATION_TYPES Debug Release)',
                       'Web catalog: single-configuration builds only')


if __name__ == '__main__':
    unittest.main()
