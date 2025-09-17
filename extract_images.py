#!/usr/bin/env python3
import re
import base64
import os

def extract_base64_images(html_file, output_dir):
    """Extract base64 images from HTML file to separate PNG files"""
    with open(html_file, 'r', encoding='utf-8') as f:
        content = f.read()

    # Find all base64 image patterns
    pattern = r'<!-- /(icon-[^>]+)\.png -->\s*<img[^>]+src="data:image/png;base64,([^"]+)"'
    matches = re.findall(pattern, content, re.MULTILINE | re.DOTALL)

    extracted_count = 0

    for icon_name, base64_data in matches:
        try:
            # Decode base64 data
            image_data = base64.b64decode(base64_data)

            # Create output filename
            output_file = os.path.join(output_dir, f"{icon_name}.png")

            # Write image data to file
            with open(output_file, 'wb') as img_file:
                img_file.write(image_data)

            print(f"Extracted: {icon_name}.png")
            extracted_count += 1

        except Exception as e:
            print(f"Error extracting {icon_name}: {e}")

    print(f"\nExtracted {extracted_count} images to {output_dir}")

if __name__ == "__main__":
    html_file = "/Users/antti/e9/esp32-web-interface-lilygo_tcan/data/index.html"
    output_dir = "/Users/antti/e9/esp32-web-interface-lilygo_tcan/data/images"

    os.makedirs(output_dir, exist_ok=True)
    extract_base64_images(html_file, output_dir)

