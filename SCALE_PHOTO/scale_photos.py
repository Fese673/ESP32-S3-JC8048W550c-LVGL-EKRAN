#!/usr/bin/env python3
"""
Skaler zdjec - produkcyjny, zoptymalizowany skrypt do skalowania obrazow.
Uzywa Pillow z BICUBIC dla najlepszej jakosci i szybkosci.
"""

import os
import sys
from pathlib import Path
from PIL import Image
from concurrent.futures import ThreadPoolExecutor, as_completed
import threading

ZDJ_DIR = Path(__file__).parent / "ZDJ"
SCALED_DIR = Path(__file__).parent / "scaled"
SUPPORTED_EXTENSIONS = {".png", ".jpg", ".jpeg", ".bmp", ".tiff", ".webp"}
RESAMPLE = Image.Resampling.BICUBIC

stdout_lock = threading.Lock()


def get_image_files(folder: Path) -> list[str]:
    if not folder.exists():
        return []
    return sorted([f for f in folder.iterdir() if f.suffix.lower() in SUPPORTED_EXTENSIONS])


def get_scaled_files() -> set[str]:
    if not SCALED_DIR.exists():
        return set()
    return {f.name for f in SCALED_DIR.iterdir() if f.suffix.lower() in SUPPORTED_EXTENSIONS}


def scale_image(args: tuple) -> tuple[str, bool, str]:
    input_path, output_path, scale_x, scale_y = args

    try:
        with Image.open(input_path) as img:
            original = img.size
            new_size = (int(original[0] * scale_x), int(original[1] * scale_y))

            resized = img.resize(new_size, RESAMPLE)

            SCALED_DIR.mkdir(parents=True, exist_ok=True)

            ext = output_path.suffix.lower()
            if ext == ".png":
                resized.save(output_path, format="PNG", compress_level=1)
            else:
                resized.save(output_path, format="JPEG", quality=95, subsampling=0)

            with stdout_lock:
                print(f"  [{output_path.name}] {original[0]}x{original[1]} -> {new_size[0]}x{new_size[1]}")

            return output_path.name, True, f"{new_size[0]}x{new_size[1]}"
    except Exception as e:
        with stdout_lock:
            print(f"  BLAD {input_path.name}: {e}")
        return input_path.name, False, str(e)


def main() -> None:
    print("\n=== SKALER ZDJEC (Pillow BICUBIC) ===\n")

    if not ZDJ_DIR.exists():
        print(f"BLAD: folder ZDJ nie istnieje: {ZDJ_DIR}")
        sys.exit(1)

    all_images = get_image_files(ZDJ_DIR)
    scaled_files = get_scaled_files()
    to_process = [f for f in all_images if f.name not in scaled_files]

    if not to_process:
        print("WSZYSTKIE zdjecia zostaly przeskalowane!")
        print(f"Przerobione: {len(scaled_files)}/{len(all_images)}")
        sys.exit(0)

    print(f"Znaleziono: {len(all_images)} plikow, {len(to_process)} do przetworzenia\n")

    REF_SIZE = 256

    while True:
        try:
            target_w = float(input("Podaj docelowa SZEROKOSC (px, oryginal=256): ").strip().replace(",", "."))
            target_h = float(input("Podaj docelowa WYSOKOSC (px, oryginal=256): ").strip().replace(",", "."))
            if 0 < target_w <= 4096 and 0 < target_h <= 4096:
                break
            print("BLAD: zakres 1-4096\n")
        except ValueError:
            print("BLAD: podaj liczbe\n")

    scale_x = target_w / REF_SIZE
    scale_y = target_h / REF_SIZE

    num_workers = min(os.cpu_count() or 4, len(to_process), 8)

    print(f"\nSkaluje {len(to_process)} zdjec (wspolczynnik: {scale_x:.4f}x{scale_y:.4f}) na {num_workers} watkach...\n")

    tasks = [
        (img_path, SCALED_DIR / img_path.name, scale_x, scale_y)
        for img_path in to_process
    ]

    success = 0
    with ThreadPoolExecutor(max_workers=num_workers) as executor:
        futures = [executor.submit(scale_image, task) for task in tasks]
        for future in as_completed(futures):
            name, ok, _ = future.result()
            if ok:
                success += 1

    print(f"\nGOTOWE! Przerobiono {success}/{len(to_process)} zdjec.")


if __name__ == "__main__":
    main()