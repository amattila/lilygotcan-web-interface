#!/usr/bin/env python3
import re
import os

def replace_base64_with_file_refs(html_file, images_dir):
    """Replace base64 image data with file references in HTML"""
    with open(html_file, 'r', encoding='utf-8') as f:
        content = f.read()

    # Pattern to match base64 images with their comments
    pattern = r'(<!-- /(icon-[^>]+)\.png -->\s*<img[^>]+)src="data:image/png;base64,[^"]+"([^>]*>)'

    def replace_match(match):
        full_tag_start = match.group(1)
        icon_name = match.group(2)
        tag_end = match.group(3)

        # Create new src attribute pointing to the image file
        new_src = f'src="images/{icon_name}.png"'

        # Replace the src attribute
        # Find the src part and replace it
        src_pattern = r'src="[^"]*"'
        new_tag = re.sub(src_pattern, new_src, full_tag_start + 'src="data:image/png;base64,placeholder"' + tag_end)

        return new_tag

    # Apply replacements
    new_content = re.sub(pattern, replace_match, content, flags=re.MULTILINE | re.DOTALL)

    # Write back to file
    with open(html_file, 'w', encoding='utf-8') as f:
        f.write(new_content)

    print(f"Replaced base64 images with file references in {html_file}")

if __name__ == "__main__":
    html_file = "/Users/antti/e9/esp32-web-interface-lilygo_tcan/data/index.html"
    images_dir = "/Users/antti/e9/esp32-web-interface-lilygo_tcan/data/images"

    replace_base64_with_file_refs(html_file, images_dir)

