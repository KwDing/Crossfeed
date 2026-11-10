"""Standard-library signal checks of an impulse rendered by the real runtime DLL."""
import argparse
import cmath
import csv
import json
import math
from pathlib import Path


def analyze(directory):
    with (directory / 'crossfeed_impulse.csv').open(newline='', encoding='utf-8-sig') as stream:
        rows = list(csv.DictReader(stream))
    direct = [float(row['direct']) for row in rows]
    cross = [float(row['cross']) for row in rows]
    feed = .316227766
    trim = 1.0  # New-instance Gain defaults to 0 dB.
    assert abs(direct[0] - .1 * trim / math.sqrt(1 + feed * feed)) < 1e-7, 'static-power gain (0.1 impulse)'
    assert all(x == 0 for x in direct[1:]), 'direct path has no delay/filter'
    onset = next(i for i, x in enumerate(cross) if abs(x) > 1e-12)
    assert onset == 14, 'integer delay onset'

    response = []
    for hz in (0, 100, 700, 1000, 10000, 20000):
        z = cmath.exp(-2j * math.pi * hz / 48000)
        shelf = (.41025591787086224 - .7537428187992803*z + .3485256619546197*z*z) / (1 - 1.897125376603263*z + .9021641376294647*z*z)
        thiran = (3/7 + z) / (1 + (3/7)*z)
        expected = feed * shelf * z**14 * thiran
        actual = sum(x * z**i for i, x in enumerate(cross)) / direct[0]
        assert abs(actual - expected) < 2e-6, f'transfer response at {hz} Hz'
        response.append({'hz': hz, 'cross_over_direct_db': 20*math.log10(abs(actual))})
    # Both RBJ poles are inside the unit circle. This checks the fixed coefficients only.
    discriminant = cmath.sqrt((-1.897125376603263)**2 - 4*.9021641376294647)
    poles = [(1.897125376603263 + s*discriminant)/2 for s in (-1, 1)]
    assert max(abs(p) for p in poles) < 1
    result = {'status': 'passed', 'source': 'actual runtime DLL impulse', 'sample_rate': 48000,
              'first_cross_sample': onset, 'low_frequency_delay_samples': 14.4,
              'shelf_pole_radius': max(abs(p) for p in poles), 'response': response,
              'perceptual_claim': 'not tested; RMS and sample peak are not perceived loudness or true peak'}
    (directory / 'signal_metrics.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, ensure_ascii=False))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--directory', type=Path, required=True)
    analyze(parser.parse_args().directory)


if __name__ == '__main__':
    main()
