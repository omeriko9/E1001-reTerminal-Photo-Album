import os
from PIL import Image

spiffs_dir = 'spiffs'

for filename in os.listdir(spiffs_dir):
    if filename.endswith('.jpg'):
        img_path = os.path.join(spiffs_dir, filename)
        img = Image.open(img_path)
        img_resized = img.resize((800, 480))
        img_resized.save(img_path)
        print(f"Resized {filename}")