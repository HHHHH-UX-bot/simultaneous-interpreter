#!/usr/bin/env python3
"""Export complete Qwen2 ByteLevel BPE and Unicode NFC tables for tiny_tokenizer.c.

No vocabulary pruning. Binary integers are little endian. Unicode data uses the
Python runtime's official Unicode database; the selected model's normalizer is NFC.
"""
import argparse
import hashlib
import json
import pathlib
import struct
import sys
import unicodedata as ud

# The official Rust tokenizer currently uses Unicode-9 normalization tables,
# independently of the regex engine's Unicode categories. Query that exact
# implementation when exporting, rather than silently using Python's newer NFC.
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'test_deps'))
import tokenizers
from tokenizers import Regex, normalizers, pre_tokenizers

EXPECTED_REGEX = r"(?i:'s|'t|'re|'ve|'m|'ll|'d)|[^\r\n\p{L}\p{N}]?\p{L}+|\p{N}| ?[^\s\p{L}\p{N}]+[\r\n]*|\s*[\r\n]+|\s+(?!\S)|\s+"

def byte_decoder():
    bs = list(range(33, 127)) + list(range(161, 173)) + list(range(174, 256))
    cs = bs[:]
    n = 0
    for b in range(256):
        if b not in bs:
            bs.append(b)
            cs.append(256 + n)
            n += 1
    return dict(zip(map(chr, cs), bs))

def export(source, output):
    source = pathlib.Path(source)
    obj = json.loads(source.read_text(encoding='utf-8'))
    assert obj['normalizer'] == {'type': 'NFC'}, 'Unsupported normalizer'
    pre = obj['pre_tokenizer']['pretokenizers']
    assert pre[0]['pattern']['Regex'] == EXPECTED_REGEX, 'Unexpected pretokenizer regex'
    assert pre[1]['type'] == 'ByteLevel' and not pre[1]['add_prefix_space'] and not pre[1]['use_regex']
    model = obj['model']
    assert model['type'] == 'BPE' and model['dropout'] is None and model['unk_token'] is None
    dec = byte_decoder()
    vocab = model['vocab']
    added = obj['added_tokens']
    n_vocab = max([*vocab.values(), *(x['id'] for x in added)]) + 1
    tokens = [None] * n_vocab
    for word, tid in vocab.items():
        tokens[tid] = (bytes(dec[c] for c in word), 0)
    for item in added:
        assert not item['single_word'] and not item['lstrip'] and not item['rstrip'] and not item['normalized']
        tokens[item['id']] = (item['content'].encode('utf-8'), 1 | (2 if item['special'] else 0))
    assert all(x is not None for x in tokens)
    byte_ids = [0] * 256
    for word, tid in vocab.items():
        if len(word) == 1:
            byte_ids[dec[word]] = tid
    assert len(set(byte_ids)) == 256
    metadata, raw = [], bytearray()
    for data, flags in tokens:
        metadata.extend((len(raw), len(data), flags))
        raw.extend(data)
    merges = []
    for rank, pair in enumerate(model['merges']):
        left, right = pair.split(' ') if isinstance(pair, str) else pair
        merges.extend((vocab[left], vocab[right], vocab[left + right], rank))
    # Generate exact Unicode L/N/space ranges from the same Oniguruma engine as
    # the model pipeline. Offsets returned by the Python API are codepoint based.
    category_mask = bytearray(0x110000)
    for pattern, bit in [(r'\p{L}+',1),(r'\p{N}+',2),(r'\s+',4)]:
        splitter = pre_tokenizers.Split(Regex(pattern), behavior='removed')
        for base, end in [(0,0xd800),(0xe000,0x110000)]:
            for start in range(base,end,16384):
                stop=min(start+16384,end)
                text=''.join(chr(x) for x in range(start,stop))
                mask=bytearray([bit])*(stop-start)
                for _,(left,right) in splitter.pre_tokenize_str(text):
                    mask[left:right]=b'\0'*(right-left)
                for k,value in enumerate(mask):
                    category_mask[start+k] |= value
    nfd, nfc = normalizers.NFD(), normalizers.NFC()
    categories, ccc_ranges, decomp, comp = [], [], [], []
    last_flags, last_ccc, flag_start, ccc_start = None, None, 0, 0
    for cp in range(0x110000):
        char = chr(cp)
        flags = category_mask[cp]
        ccc = ud.combining(char)
        if ccc:
            # An assigned combining mark in Python may be unassigned in the
            # official normalizer. Check for canonical reordering on either
            # side of the known minimum/maximum combining-class probes.
            nd = nfd.normalize_str(char)
            if nfd.normalize_str('\u0345'+char) == '\u0345'+nd and nfd.normalize_str(char+'\u0334') == nd+'\u0334':
                ccc = 0
        if flags != last_flags:
            if last_flags:
                categories.extend((flag_start, cp - 1, last_flags))
            flag_start, last_flags = cp, flags
        if ccc != last_ccc:
            if last_ccc:
                ccc_ranges.extend((ccc_start, cp - 1, last_ccc))
            ccc_start, last_ccc = cp, ccc
        dd = ud.decomposition(char)
        if dd and not dd.startswith('<') and nfd.normalize_str(char) != char:
            dd = [int(x, 16) for x in dd.split()]
            assert len(dd) <= 2
            decomp.extend((cp, dd[0], dd[1] if len(dd) == 2 else 0, len(dd)))
            if len(dd) == 2 and nfc.normalize_str(''.join(map(chr, dd))) == char:
                comp.append((dd[0], dd[1], cp))
    if last_flags:
        categories.extend((flag_start, 0x10ffff, last_flags))
    if last_ccc:
        ccc_ranges.extend((ccc_start, 0x10ffff, last_ccc))
    comp.sort()
    def words(values):
        return struct.pack('<' + 'I' * len(values), *values)
    sections = [words(metadata), bytes(raw), words(merges), words(byte_ids), words(categories), words(ccc_ranges), words(decomp), words([x for row in comp for x in row])]
    # Header: magic[8], version, total_bytes, vocabulary_count, merge_count,
    # category_count, ccc_count, decomposition_count, composition_count,
    # section offsets[8], raw_bytes_count, reserved[5]. Total 128 bytes.
    offsets, offset = [], 128
    for data in sections:
        offsets.append(offset)
        offset += len(data)
        offset = (offset + 3) & ~3
    header = b'QWTOK01\0' + words([1, offset, n_vocab, len(merges)//4, len(categories)//3, len(ccc_ranges)//3, len(decomp)//4, len(comp), *offsets, len(raw), *([0]*5)])
    assert len(header) == 96
    result = bytearray(header.ljust(128, b'\0'))
    for pos, data in zip(offsets, sections):
        result.extend(b'\0' * (pos-len(result)))
        result.extend(data)
    result.extend(b'\0' * (offset-len(result)))
    output = pathlib.Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(result)
    info = dict(file=str(output), bytes=len(result), vocab=n_vocab, merges=len(merges)//4, unicode_database=ud.unidata_version, unicode_normalizer='unicode-normalization-alignments 0.1.12 / Unicode 9.0.0', reference_tokenizers=tokenizers.__version__, sha256=hashlib.sha256(result).hexdigest(), source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(), regex=EXPECTED_REGEX)
    output.with_suffix(output.suffix+'.json').write_text(json.dumps(info, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(info, ensure_ascii=True))

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('source', nargs='?', default='work/llm/models/tokenizer.json')
    parser.add_argument('output', nargs='?', default='work/llm/output/qwen2_0.5b.tokenizer.bin')
    args = parser.parse_args()
    export(args.source, args.output)
