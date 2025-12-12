from PIL import Image, ImageFont, ImageDraw
import os

def generate_c_font(font_path, size, output_file):
    try:
        font = ImageFont.truetype(font_path, size)
    except IOError:
        print(f"Could not load font: {font_path}")
        return

    # Character range (ASCII 32-126)
    chars = range(32, 127)
    
    # Font dimensions
    char_width = 16
    char_height = 24
    
    # Prepare the output content
    c_code = []
    c_code.append("#ifndef FONT_16X24_H")
    c_code.append("#define FONT_16X24_H")
    c_code.append("")
    c_code.append("#include <stdint.h>")
    c_code.append("")
    c_code.append(f"// Font: Consolas {size}px")
    c_code.append(f"// Size: {char_width}x{char_height}")
    c_code.append("")
    c_code.append(f"static const uint16_t font_16x24[{len(chars)} * {char_height}] = {{")

    for char_code in chars:
        char = chr(char_code)
        
        # Create an image for the character
        image = Image.new("1", (char_width, char_height), 0)
        draw = ImageDraw.Draw(image)
        
        # Get bounding box to center
        bbox = font.getbbox(char)
        if bbox:
            w = bbox[2] - bbox[0]
            h = bbox[3] - bbox[1]
            x = (char_width - w) // 2 - bbox[0]
            y = (char_height - h) // 2 - bbox[1]
        else:
            x = 0
            y = 0
            
        # Draw the character
        draw.text((x, y), char, font=font, fill=1)
        
        # Safe comment for C
        safe_char = char
        if char == '\\':
            safe_char = '\\\\'
        c_code.append(f"    // '{safe_char}'")
        
        # Convert to bits
        pixels = image.load()
        for r in range(char_height):
            row_val = 0
            for c in range(char_width):
                if pixels[c, r]:
                    row_val |= (1 << (15 - c)) # MSB first
            
            c_code.append(f"    0x{row_val:04X},")

    c_code.append("};")
    c_code.append("")
    c_code.append("#endif // FONT_16X24_H")
    
    with open(output_file, "w", encoding="utf-8") as f:
        f.write("\n".join(c_code))
    
    print(f"Generated {output_file}")

if __name__ == "__main__":
    # Try to find Consolas
    font_path = "C:\\Windows\\Fonts\\consola.ttf"
    if not os.path.exists(font_path):
        print("Consolas not found, trying Arial")
        font_path = "C:\\Windows\\Fonts\\arial.ttf"
        
    generate_c_font(font_path, 24, "main/font_16x24.h")
