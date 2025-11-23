#!/usr/bin/env python3
"""
BMF Font Generator - Converts TrueType/bitmap fonts to BMF format
Usage: python3 fontgen.py [options] input_file sizes...

Options:
  -o <file>       Output file (default: output.bmf)
  -n <name>       Font name stored in header (default: based on input)
  -h, --help      Show this help

Examples:
  python3 fontgen.py font.ttf 8 12 16 -o console.bmf -n "Console"
  python3 fontgen.py bitmap.png 8 16 -o bitmap.bmf
"""

import struct
import sys
import os
from PIL import Image, ImageDraw, ImageFont

def print_help():
    print(__doc__)
    sys.exit(0)

def generate_bmf_from_ttf(ttf_path, sizes, output_path, name="Default"):
    """Generate BMF from TrueType font"""
    
    print(f"Generating BMF from {ttf_path}")
    print(f"Sizes: {sizes}")
    print(f"Output: {output_path}")
    print(f"Name: {name}")
    
    with open(output_path, 'wb') as out:
        # Header
        out.write(b'BMF\0')
        font_name = name[:28].encode('ascii').ljust(28, b'\x00')
        out.write(font_name)
        out.write(struct.pack('<B', 1))  # version
        out.write(struct.pack('<B', 0))  # flags
        out.write(struct.pack('<H', len(sizes)))
        
        for size in sizes:
            print(f"\nProcessing size {size}...")
            font = ImageFont.truetype(ttf_path, size)
            
            # Calculate metrics using actual text
            test_img = Image.new('1', (200, 200), 0)
            test_draw = ImageDraw.Draw(test_img)
            
            # Measure a string with ascenders and descenders
            bbox = test_draw.textbbox((0, 0), 'Agyjpq|', font=font)
            height = bbox[3] - bbox[1]
            
            if height < 1:
                height = size
            
            # Calculate baseline - use font metrics if available
            try:
                ascent, descent = font.getmetrics()
                baseline = ascent
                height = ascent + descent
            except:
                # Fallback: measure from top to baseline
                bbox_base = test_draw.textbbox((0, 0), 'Hx', font=font)
                baseline = bbox_base[3] - bbox_base[1] - 1
            
            # Sanity check baseline
            if baseline < 1:
                baseline = int(height * 0.75)  # 75% from top as fallback
            if baseline >= height:
                baseline = height - 2
            
            print(f"  Height: {height}, Baseline: {baseline}")
            
            # Sequence header
            out.write(struct.pack('<B', height))
            out.write(struct.pack('<B', baseline))
            out.write(struct.pack('<H', 256))  # glyph count
            
            glyph_count = 0
            total_bytes = 0
            
            # Generate glyphs
            for ascii_code in range(256):
                char = chr(ascii_code)
                
                # Measure character
                bbox = test_draw.textbbox((0, 0), char, font=font)
                width = max(1, bbox[2] - bbox[0])
                
                # Handle space specially
                if ascii_code == 32:
                    width = max(width, size // 3)
                
                pitch = (width + 7) // 8
                
                # Render to bitmap
                img = Image.new('1', (width, height), 0)
                draw = ImageDraw.Draw(img)
                draw.text((0, 0), char, font=font, fill=1)
                
                # Convert to packed bytes (MSB first)
                bitmap = []
                for y in range(height):
                    byte_val = 0
                    bit_count = 0
                    
                    for x in range(width):
                        pixel = img.getpixel((x, y))
                        if pixel:
                            byte_val |= (1 << (7 - bit_count))
                        
                        bit_count += 1
                        if bit_count == 8:
                            bitmap.append(byte_val)
                            byte_val = 0
                            bit_count = 0
                    
                    # Flush remaining bits
                    if bit_count > 0:
                        bitmap.append(byte_val)
                
                # Write glyph
                out.write(struct.pack('<B', ascii_code))
                out.write(struct.pack('<B', width))
                out.write(struct.pack('<B', pitch))
                out.write(bytes(bitmap))
                
                glyph_count += 1
                total_bytes += 3 + len(bitmap)
            
            print(f"  Wrote {glyph_count} glyphs ({total_bytes} bytes)")
    
    print(f"\nSuccessfully generated {output_path}")

def generate_bmf_from_image(image_path, glyph_width, glyph_height, 
                            output_path, name="Bitmap"):
    """Generate BMF from bitmap strip (256 chars, 16x16 grid)"""
    
    print(f"Generating BMF from {image_path}")
    print(f"Glyph size: {glyph_width}x{glyph_height}")
    print(f"Output: {output_path}")
    print(f"Name: {name}")
    
    img = Image.open(image_path).convert('1')
    
    with open(output_path, 'wb') as out:
        # Header
        out.write(b'BMF\0')
        font_name = name[:28].encode('ascii').ljust(28, b'\x00')
        out.write(font_name)
        out.write(struct.pack('<B', 1))
        out.write(struct.pack('<B', 0))
        out.write(struct.pack('<H', 1))  # one size
        
        # Sequence header
        baseline = glyph_height - 2
        out.write(struct.pack('<B', glyph_height))
        out.write(struct.pack('<B', baseline))
        out.write(struct.pack('<H', 256))
        
        pitch = (glyph_width + 7) // 8
        
        print(f"Processing glyphs...")
        
        for ascii_code in range(256):
            x_offset = (ascii_code % 16) * glyph_width
            y_offset = (ascii_code // 16) * glyph_height
            
            bitmap = []
            for y in range(glyph_height):
                byte_val = 0
                bit_count = 0
                
                for x in range(glyph_width):
                    px = img.getpixel((x_offset + x, y_offset + y))
                    if px:
                        byte_val |= (1 << (7 - bit_count))
                    
                    bit_count += 1
                    if bit_count == 8:
                        bitmap.append(byte_val)
                        byte_val = 0
                        bit_count = 0
                
                if bit_count > 0:
                    bitmap.append(byte_val)
            
            out.write(struct.pack('<B', ascii_code))
            out.write(struct.pack('<B', glyph_width))
            out.write(struct.pack('<B', pitch))
            out.write(bytes(bitmap))
        
        print(f"Wrote 256 glyphs")
    
    print(f"\nSuccessfully generated {output_path}")

def parse_args():
    """Parse command line arguments"""
    args = sys.argv[1:]
    
    if not args or '-h' in args or '--help' in args:
        print_help()
    
    output_file = "output.bmf"
    font_name = None
    input_file = None
    sizes = []
    
    i = 0
    while i < len(args):
        arg = args[i]
        
        if arg == '-o':
            if i + 1 < len(args):
                output_file = args[i + 1]
                i += 2
            else:
                print("Error: -o requires output filename")
                sys.exit(1)
        
        elif arg == '-n':
            if i + 1 < len(args):
                font_name = args[i + 1]
                i += 2
            else:
                print("Error: -n requires font name")
                sys.exit(1)
        
        elif arg.startswith('-'):
            print(f"Error: Unknown option {arg}")
            print_help()
        
        elif input_file is None:
            input_file = arg
            i += 1
        
        else:
            try:
                sizes.append(int(arg))
            except ValueError:
                print(f"Error: Invalid size '{arg}' - must be integer")
                sys.exit(1)
            i += 1
    
    if not input_file:
        print("Error: No input file specified")
        print_help()
    
    if not sizes:
        print("Error: No sizes specified")
        print_help()
    
    if not font_name:
        font_name = os.path.splitext(os.path.basename(input_file))[0].title()
    
    return input_file, sizes, output_file, font_name

if __name__ == '__main__':
    try:
        input_file, sizes, output_file, font_name = parse_args()
        
        if not os.path.exists(input_file):
            print(f"Error: Input file '{input_file}' not found")
            sys.exit(1)
        
        if input_file.lower().endswith('.ttf') or input_file.lower().endswith('.otf'):
            generate_bmf_from_ttf(input_file, sizes, output_file, font_name)
        
        elif input_file.lower().endswith('.png'):
            if len(sizes) != 2:
                print("Error: PNG bitmap requires exactly 2 size arguments (width height)")
                sys.exit(1)
            width, height = sizes[0], sizes[1]
            generate_bmf_from_image(input_file, width, height, output_file, font_name)
        
        else:
            print(f"Error: Unsupported file format. Use .ttf, .otf, or .png")
            sys.exit(1)
            
    except Exception as e:
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)