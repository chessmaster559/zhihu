"""Read the current mmap asset container with strict bounds/name validation."""
import struct


def unpack(blob):
    if len(blob) < 12:
        raise ValueError('Truncated assets header')
    count, checksum, length = struct.unpack_from('<III', blob)
    payload = blob[12:]
    if length != len(payload) or sum(payload) & 0xffff != checksum:
        raise ValueError('Assets length/checksum mismatch')
    table_size = count * 44
    if table_size > length:
        raise ValueError('Truncated assets table')
    files = {}
    for i in range(count):
        name, size, offset, _, _ = struct.unpack_from('<32sIIHH', payload, i * 44)
        name = name.split(b'\0')[0].decode('utf-8')
        if not name or '/' in name or '\\' in name or name in ('.', '..') or name in files:
            raise ValueError('Unsafe or duplicate asset name')
        start = table_size + offset
        if start + 2 + size > length or payload[start:start + 2] != b'ZZ':
            raise ValueError('Invalid asset boundary: ' + name)
        files[name] = payload[start + 2:start + 2 + size]
    return files
