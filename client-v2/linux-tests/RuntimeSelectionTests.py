#!/usr/bin/env python3
"""Exercise host-runtime selection and AppRun's fallback without a display."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


PROBE, APPRUN, COMPILER = map(Path, sys.argv[1:4])
del sys.argv[1:4]
BASELINE = 'GLIBCXX_3.4\nCXXABI_1.3\n'


class RuntimeSelectionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.versions = self.root / 'versions'
        self.versions.write_text(BASELINE)
        self.env = dict(os.environ, LD_LIBRARY_PATH='')
        self.env.pop('LD_PRELOAD', None)

    def probe(self, env=None):
        return subprocess.run([PROBE, self.versions], env=env or self.env,
                              text=True, capture_output=True)

    def fake_app(self):
        app = self.root / 'AppDir'
        binary = app / 'usr/bin/RuTool'
        binary.parent.mkdir(parents=True)
        binary.write_text('#!/bin/sh\nprintf "%s\\n" "${LD_PRELOAD-}" "$LD_LIBRARY_PATH"\n')
        binary.chmod(0o755)
        runtime = app / 'usr/libexec/rutool'
        runtime.mkdir(parents=True)
        shutil.copy2(PROBE, runtime / 'runtime-check')
        shutil.copy2(self.versions, runtime / 'libstdc++.versions')
        shutil.copy2(APPRUN, app / 'AppRun')
        return app

    def test_compatible_host_with_more_abi_versions(self):
        result = self.probe()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(Path(result.stdout.strip()).is_file())

    def test_host_missing_required_version(self):
        self.versions.write_text(BASELINE + 'GLIBCXX_999.0\n')
        result = self.probe()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, '')

    def test_incomplete_or_missing_manifest(self):
        for contents in ('', 'GLIBCXX_3.4\n', 'CXXABI_1.3\n', BASELINE + 'invalid\n'):
            self.versions.write_text(contents)
            self.assertNotEqual(self.probe().returncode, 0)
        self.versions.unlink()
        self.assertNotEqual(self.probe().returncode, 0)

    def test_library_without_abi_markers(self):
        stub = self.root / 'stub'
        stub.mkdir()
        source = stub / 'empty.c'
        source.write_text('int placeholder(void) { return 0; }\n')
        subprocess.run([COMPILER, '-shared', '-fPIC', source,
                        '-Wl,-soname,libstdc++.so.6', '-o', stub / 'libstdc++.so.6'], check=True)
        result = self.probe(dict(self.env, LD_LIBRARY_PATH=str(stub)))
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, '')

    def test_launcher_prefers_host_and_preserves_explicit_preload(self):
        host = self.probe()
        self.assertEqual(host.returncode, 0, host.stderr)
        app = self.fake_app()
        env = dict(self.env, LD_PRELOAD='libm.so.6', LD_LIBRARY_PATH='/existing/path')
        result = subprocess.run([app / 'AppRun'], env=env, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines(),
                         [host.stdout.strip() + ':libm.so.6', str(app / 'usr/lib') + ':/existing/path'])

    def test_launcher_keeps_bundle_when_host_is_too_old(self):
        self.versions.write_text(BASELINE + 'GLIBCXX_999.0\n')
        app = self.fake_app()
        result = subprocess.run([app / 'AppRun'], env=dict(self.env, LD_PRELOAD='libm.so.6'),
                                text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines(), ['libm.so.6', str(app / 'usr/lib')])

    def test_restart_rechecks_previous_appimage_runtime(self):
        host = self.probe()
        self.assertEqual(host.returncode, 0, host.stderr)
        self.versions.write_text(BASELINE + 'GLIBCXX_999.0\n')
        app = self.fake_app()
        for original in ('', 'libm.so.6'):
            env = dict(self.env, LD_PRELOAD=host.stdout.strip(), RUTOOL_ORIGINAL_LD_PRELOAD=original)
            result = subprocess.run([app / 'AppRun'], env=env, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.splitlines(), [original, str(app / 'usr/lib')])

    def test_launcher_saves_desktop_environment_once(self):
        app = self.fake_app()
        binary = app / 'usr/bin/RuTool'
        binary.write_text('#!/bin/sh\n/usr/bin/env\n')
        env = dict(self.env, LD_LIBRARY_PATH='/session/lib', QT_PLUGIN_PATH='/session/qt',
                   QML_IMPORT_PATH='', LD_PRELOAD='libm.so.6')
        env.pop('QT_QUICK_CONTROLS_CONF', None)
        for key in list(env):
            if key.startswith('RUTOOL_DESKTOP_'):
                del env[key]
        def launch(environment):
            result = subprocess.run([app / 'AppRun'], env=environment, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            return dict(line.split('=', 1) for line in result.stdout.splitlines())
        saved = launch(env)
        self.assertEqual(saved['RUTOOL_DESKTOP_ENV_SAVED'], '1')
        self.assertEqual(saved['RUTOOL_DESKTOP_LD_LIBRARY_PATH'], '/session/lib')
        self.assertEqual(saved['RUTOOL_DESKTOP_QT_PLUGIN_PATH'], '/session/qt')
        self.assertEqual(saved['RUTOOL_DESKTOP_LD_PRELOAD'], 'libm.so.6')
        self.assertEqual(saved['RUTOOL_DESKTOP_QML_IMPORT_PATH'], '')
        self.assertNotIn('RUTOOL_DESKTOP_QT_QUICK_CONTROLS_CONF', saved)
        restarted = launch(saved)
        for key in saved:
            if key.startswith('RUTOOL_DESKTOP_'):
                self.assertEqual(restarted[key], saved[key])


if __name__ == '__main__':
    unittest.main()
