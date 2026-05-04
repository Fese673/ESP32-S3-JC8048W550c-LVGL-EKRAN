import os
import shutil

src_dir = r"C:\PROJEKTY\ESP32-S3-JC8048W550-LVGL-ESPIDF-EEZ-main\ekran_zegar_kopia\src\ui"
dst_dir = r"C:\PROJEKTY\ESP32-S3-JC8048W550-LVGL-ESPIDF-EEZ-main\main\ui\ui"

def copy_ui():
    if not os.path.exists(src_dir):
        print(f"Błąd: Folder źródłowy nie istnieje: {src_dir}")
        return

    parent_dir = os.path.dirname(dst_dir)
    if os.path.exists(parent_dir):
        print(f"Usuwanie starego folderu: {parent_dir}")
        shutil.rmtree(parent_dir)

    os.makedirs(parent_dir, exist_ok=True)

    # Kopiowanie folderu src do main\ui\ui
    print(f"Kopiowanie z {src_dir} do {dst_dir}...")
    shutil.copytree(src_dir, dst_dir)
    print("Gotowe! Folder ui został pomyślnie zsynchronizowany.")

if __name__ == "__main__":
    copy_ui()
