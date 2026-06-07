#!/usr/bin/env python3
"""
Skaler zdjec + recolor - interaktywny skrypt.
Zdjecia zrodlowe:zdjecia przed/
Wynik:zdjecia po/
"""

import os
import sys
from pathlib import Path
from PIL import Image
from concurrent.futures import ThreadPoolExecutor, as_completed
import threading

SRC_DIR = Path(__file__).parent / "zdjęcia przed"
DST_DIR = Path(__file__).parent / "zdjęcia po"
SUPPORTED_EXTENSIONS = {".png", ".jpg", ".jpeg", ".bmp", ".tiff", ".webp"}
RESAMPLE = Image.Resampling.BICUBIC

stdout_lock = threading.Lock()


def get_image_files(folder: Path) -> list:
    if not folder.exists():
        return []
    return sorted([f for f in folder.iterdir() if f.suffix.lower() in SUPPORTED_EXTENSIONS])


def get_processed_files() -> set:
    if not DST_DIR.exists():
        return set()
    return {f.name for f in DST_DIR.iterdir() if f.suffix.lower() in SUPPORTED_EXTENSIONS}


def parse_hex_color(hex_str: str) -> tuple:
    hex_str = hex_str.strip().lstrip("#")
    if len(hex_str) == 3:
        hex_str = "".join(c * 2 for c in hex_str)
    if len(hex_str) != 6:
        raise ValueError(f"Nieprawidlowy format koloru: #{hex_str}")
    return int(hex_str[0:2], 16), int(hex_str[2:4], 16), int(hex_str[4:6], 16)


def apply_recolor(img: Image.Image, color: tuple, opacity: int) -> Image.Image:
    """LVGL-style recolor: result = src * (1 - opa) + color * opa, alpha zachowana."""
    if opacity == 0:
        return img

    img = img.convert("RGBA")
    r, g, b = color
    src_r, src_g, src_b, src_a = img.split()

    if opacity == 255:
        result = Image.merge("RGBA", (
            Image.new("L", img.size, r),
            Image.new("L", img.size, g),
            Image.new("L", img.size, b),
            src_a,
        ))
    else:
        alpha = opacity / 255.0
        result = Image.merge("RGBA", (
            Image.blend(src_r, Image.new("L", img.size, r), alpha),
            Image.blend(src_g, Image.new("L", img.size, g), alpha),
            Image.blend(src_b, Image.new("L", img.size, b), alpha),
            src_a,
        ))

    return result


def process_image(args: tuple) -> tuple:
    input_path, output_path, scale_x, scale_y, recolor, recolor_opacity = args

    try:
        with Image.open(input_path) as img:
            original = img.size

            if scale_x is not None and scale_y is not None:
                new_size = (int(original[0] * scale_x), int(original[1] * scale_y))
                img = img.resize(new_size, RESAMPLE)
            else:
                new_size = original

            if recolor is not None:
                img = apply_recolor(img, recolor, recolor_opacity)

            output_path.parent.mkdir(parents=True, exist_ok=True)

            ext = output_path.suffix.lower()
            if ext == ".png":
                img.save(output_path, format="PNG", compress_level=1)
            else:
                img.save(output_path, format="JPEG", quality=95, subsampling=0)

            with stdout_lock:
                info = f"[{output_path.name}] {original[0]}x{original[1]}"
                if scale_x is not None:
                    info += f" -> {new_size[0]}x{new_size[1]}"
                if recolor is not None:
                    info += f" recolor=#{recolor[0]:02X}{recolor[1]:02X}{recolor[2]:02X}@{recolor_opacity}"
                print(f"  {info}")

            return output_path.name, True
    except Exception as e:
        with stdout_lock:
            print(f"  BLAD {input_path.name}: {e}")
        return input_path.name, False


def menu_choice(options: list) -> int:
    for i, opt in enumerate(options, 1):
        print(f"  {i}. {opt}")
    while True:
        try:
            choice = int(input("\nWybierz [1]: ").strip() or "1")
            if 1 <= choice <= len(options):
                return choice - 1
            print(f"BLAD: podaj 1-{len(options)}")
        except ValueError:
            print("BLAD: podaj liczbe")


def ask_scale() -> tuple:
    REF_SIZE = 256
    print(f"\nDocelowe wymiary w px (256 = oryginal, 128 = polowa, 512 = podwojone):")
    while True:
        try:
            w = float(input("Szerokosc (px): ").strip().replace(",", "."))
            h = float(input("Wysokosc (px): ").strip().replace(",", "."))
            if 0 < w <= 4096 and 0 < h <= 4096:
                return w / REF_SIZE, h / REF_SIZE
            print("BLAD: zakres 1-4096\n")
        except ValueError:
            print("BLAD: podaj liczbe\n")


def ask_recolor() -> tuple:
    while True:
        hex_color = input("Podaj kolor hex (np. #D0CECF): ").strip()
        if not hex_color:
            print("BLAD: podaj kolor")
            continue
        try:
            color = parse_hex_color(hex_color)
        except ValueError as e:
            print(f"BLAD: {e}")
            continue

        while True:
            try:
                opa = int(input("Opacity 0-255 [255]: ").strip() or "255")
                if 0 <= opa <= 255:
                    return color, opa
                print("BLAD: zakres 0-255")
            except ValueError:
                print("BLAD: podaj liczbe")


def main() -> None:
    print("\n=== SKALER ZDJEC + RECOLOR ===\n")

    if not SRC_DIR.exists():
        print(f"BLAD: folder ze zdjeciami nie istnieje: {SRC_DIR}")
        sys.exit(1)

    all_images = get_image_files(SRC_DIR)
    if not all_images:
        print(f"Brak zdjec w folderze: {SRC_DIR}")
        sys.exit(1)

    processed_files = get_processed_files()
    to_process = [f for f in all_images if f.name not in processed_files]

    print(f"Zdjecia: {len(all_images)} | Gotowe: {len(processed_files)} | Do przetworzenia: {len(to_process)}")

    if not to_process:
        print("\nWSZYSTKIE zdjecia zostaly juz przetworzone!")
        sys.exit(0)

    print("\nTryb pracy:")
    mode = menu_choice([
        "Skalowanie + Recolor",
        "Tylko skalowanie",
        "Tylko recolor",
    ])

    do_scale = mode in (0, 1)
    do_recolor = mode in (0, 2)

    scale_x, scale_y = None, None
    recolor, recolor_opacity = None, 255

    if do_scale:
        scale_x, scale_y = ask_scale()
        pw = int(256 * scale_x)
        ph = int(256 * scale_y)
        print(f"Skalowanie: {pw}x{ph} px")

    if do_recolor:
        recolor, recolor_opacity = ask_recolor()
        print(f"Recolor: #{recolor[0]:02X}{recolor[1]:02X}{recolor[2]:02X} opacity={recolor_opacity}")

    num_workers = min(os.cpu_count() or 4, len(to_process), 8)

    print(f"\nPrzetwarzam {len(to_process)} zdjec na {num_workers} watkach...\n")

    DST_DIR.mkdir(parents=True, exist_ok=True)

    tasks = [
        (img_path, DST_DIR / img_path.name, scale_x, scale_y, recolor, recolor_opacity)
        for img_path in to_process
    ]

    success = 0
    with ThreadPoolExecutor(max_workers=num_workers) as executor:
        futures = [executor.submit(process_image, task) for task in tasks]
        for future in as_completed(futures):
            _, ok = future.result()
            if ok:
                success += 1

    print(f"\nGOTOWE! Przerobiono {success}/{len(to_process)} zdjec.")
    print(f"Folder: {DST_DIR}")


if __name__ == "__main__":
    main()
