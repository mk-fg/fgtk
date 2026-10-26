import pathlib
import subprocess
import sys
import tempfile
import unittest


SCRIPT = pathlib.Path(__file__).with_name('repr')


class ReprConversionTests(unittest.TestCase):
    def convert(self, data, options):
        with tempfile.NamedTemporaryFile(dir=SCRIPT.parent, delete=False) as source:
            source.write(data)
            source_path = pathlib.Path(source.name)
        try:
            result = subprocess.run(
                [sys.executable, '-B', str(SCRIPT), '-c', options, str(source_path)],
                capture_output=True, check=True)
            return source_path.read_bytes() if 'i' in options else result.stdout
        finally:
            source_path.unlink()

    def test_trim_preserves_lines_and_ensures_final_newline(self):
        for ending in (b'\n', b'\r\n'):
            for final in (b'', ending):
                for options in ('+t', '+ti', '+tr', '+tri'):
                    with self.subTest(ending=ending, final=final, options=options):
                        data = b'first \t' + ending + b' \t' + ending + b'second  ' + final
                        newline = b'\r\n' if 'r' in options else b'\n'
                        self.assertEqual(
                            self.convert(data, options),
                            newline.join((b'first', b'', b'second', b'')))

    def test_empty_input_stays_empty(self):
        for options in ('+t', '+ti', '+tr'):
            with self.subTest(options=options):
                self.assertEqual(self.convert(b'', options), b'')

    def test_conversion_without_trim_preserves_whitespace(self):
        self.assertEqual(
            self.convert(b'first \t\r\nsecond  ', 'utf-8'),
            b'first \t\nsecond  ')


if __name__ == '__main__':
    unittest.main()
