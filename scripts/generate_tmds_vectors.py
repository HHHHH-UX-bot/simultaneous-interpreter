"""Independent integer TMDS model; fixtures stay in tmp/sim."""
from pathlib import Path
import random

root = Path(__file__).resolve().parents[1]
target = root / 'tmp' / 'sim'
target.mkdir(parents=True, exist_ok=True)
controls = (0b1101010100, 0b0010101011, 0b0101010100, 0b1010101011)
randomizer = random.Random(7010)
vectors = [(0, c, randomizer.randrange(256)) for c in range(4) for _ in range(8)]
vectors += [(1, 0, value) for value in range(256)]
vectors += [(int(i % 73 != 0), i % 4, randomizer.randrange(256)) for i in range(2048)]
vectors += [(0, 0, 0)] * 8
disparity = 0
inputs, expected = [], []
for enabled, control, value in vectors:
    inputs.append(f'{(enabled << 10) | (control << 8) | value:03x}')
    if not enabled:
        symbol, disparity = controls[control], 0
    else:
        invert_xor = value.bit_count() > 4 or (value.bit_count() == 4 and not value & 1)
        qm = value & 1
        previous = qm
        for bit in range(1, 8):
            previous = previous ^ ((value >> bit) & 1) ^ invert_xor
            qm |= previous << bit
        qm8 = int(not invert_xor)
        balance = 2 * qm.bit_count() - 8
        if disparity == 0 or balance == 0:
            symbol = ((1-qm8) << 9) | (qm8 << 8) | (qm if qm8 else qm ^ 255)
            disparity += balance if qm8 else -balance
        elif (disparity > 0 and balance > 0) or (disparity < 0 and balance < 0):
            symbol = 512 | (qm8 << 8) | (qm ^ 255)
            disparity += 2 * qm8 - balance
        else:
            symbol = (qm8 << 8) | qm
            disparity += balance - 2 * (1-qm8)
        assert abs(disparity) <= 8
    expected.append(f'{symbol:03x}')
(target / 'tmds_input.mem').write_text('\n'.join(inputs)+'\n', encoding='ascii')
(target / 'tmds_expected.mem').write_text('\n'.join(expected)+'\n', encoding='ascii')
print(f'TMDS fixture: {len(vectors)} symbols, all 256 values and all 4 control tokens')
