import os
from PIL import Image

def png_to_xbm(png_path, var_name):
    img = Image.open(png_path).convert('RGBA')
    width, height = img.size
    
    # Pack into bytes for XBM format
    bytes_list = []
    for y in range(height):
        for x in range(0, width, 8):
            byte = 0
            for bit in range(8):
                if x + bit < width:
                    r, g, b, a = img.getpixel((x + bit, y))
                    # Consider pixel "on" if it's bright OR has high opacity
                    # This works better for white-on-transparent icons
                    if a > 128 and (r > 128 or g > 128 or b > 128):
                        byte |= (1 << bit)
            bytes_list.append(f"0x{byte:02x}")
            
    header = f"const unsigned char {var_name}[] PROGMEM = {{\n  "
    header += ", ".join(bytes_list)
    header += "\n};\n"
    return header

icons_dir = "icons"
output_file = "icons.h"

icon_files = {
    "play.png": "icon_play",
    "pause.png": "icon_pause",
    "skip_next.png": "icon_next",
    "skip_previous.png": "icon_prev",
    "shuffle_on.png": "icon_shuffle",
    "favorite_on.png": "icon_heart"
}

with open(output_file, "w") as f:
    f.write("#ifndef ICONS_H\n#define ICONS_H\n\n#include <Arduino.h>\n\n// Icons are 24x24 pixels, 1-bit (monochrome)\n\n")
    for filename, var_name in icon_files.items():
        path = os.path.join(icons_dir, filename)
        if os.path.exists(path):
            print(f"Processing {filename}...")
            f.write(png_to_xbm(path, var_name))
        else:
            print(f"Warning: {filename} not found.")
    f.write("\n#endif\n")

print("Done! icons.h has been updated.")