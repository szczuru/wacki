#!/usr/bin/env python3
"""Create a 48x48 PNG icon for 3DS from a larger source image."""
import struct
import zlib
import sys

def create_simple_48x48_icon(output_path):
    """Create a simple 48x48 PNG with a placeholder design."""
    width, height = 48, 48
    
    # Create a simple gradient pattern
    pixels = []
    for y in range(height):
        row = []
        for x in range(width):
            # Create a simple pattern - blue gradient
            r = int(50 + (x / width) * 100)
            g = int(100 + (y / height) * 100)
            b = int(200 + (min(x, y) / 48) * 55)
            a = 255
            row.extend([r, g, b, a])
        pixels.append(bytes(row))
    
    # PNG file structure
    def png_chunk(chunk_type, data):
        chunk_data = chunk_type + data
        crc = zlib.crc32(chunk_data) & 0xffffffff
        return struct.pack('>I', len(data)) + chunk_data + struct.pack('>I', crc)
    
    # PNG signature
    png_data = b'\x89PNG\r\n\x1a\n'
    
    # IHDR chunk
    ihdr = struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)  # 6 = RGBA
    png_data += png_chunk(b'IHDR', ihdr)
    
    # IDAT chunk (compressed pixel data)
    raw_data = b''.join(b'\x00' + row for row in pixels)  # Filter byte + row data
    idat = zlib.compress(raw_data, 9)
    png_data += png_chunk(b'IDAT', idat)
    
    # IEND chunk
    png_data += png_chunk(b'IEND', b'')
    
    with open(output_path, 'wb') as f:
        f.write(png_data)
    
    print(f"Created {width}x{height} icon at {output_path}")

if __name__ == '__main__':
    create_simple_48x48_icon('assets/icons/wacki-3ds-48x48.png')
