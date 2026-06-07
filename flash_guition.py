import os
import subprocess
import sys

# Konfiguracja ścieżek
IDF_PATH = r"C:\esp\v6.0.1\esp-idf"
IDF_TOOLS_PATH = r"C:\Espressif\tools"
VENV_PYTHON = r"C:\Espressif\tools\python\v6.0.1\venv\Scripts\python.exe"
PROJECT_DIR = r"C:\PROJEKTY\ESP32-S3-JC8048W550-LVGL-ESPIDF-EEZ-main"

def run_flash(port="COM12", baud="115200", monitor=False):
    env = os.environ.copy()
    
    # Ustawienie zmiennych środowiskowych z profilu PowerShell
    env["IDF_PATH"] = IDF_PATH
    env["IDF_TOOLS_PATH"] = IDF_TOOLS_PATH
    env["IDF_PYTHON_ENV_PATH"] = r"C:\Espressif\tools\python\v6.0.1\venv"
    env["ESP_ROM_ELF_DIR"] = r"C:\Espressif\tools\esp-rom-elfs\20241011"
    env["OPENOCD_SCRIPTS"] = r"C:\Espressif\tools\openocd-esp32\v0.12.0-esp32-20260304\openocd-esp32\share\openocd\scripts"
    env["IDF_COMPONENT_LOCAL_STORAGE_URL"] = r"file://C:\Espressif\tools"
    env["ESP_IDF_VERSION"] = "6.0.1"
    
    # Budowa pełnego PATH do narzędzi IDF (wymagane przez Ninja/CMake/esptool)
    idf_tools_bin = [
        r"C:\Espressif\tools\ccache\4.12.1\ccache-4.12.1-windows-x86_64",
        r"C:\Espressif\tools\cmake\4.0.3\bin",
        r"C:\Espressif\tools\dfu-util\0.11\dfu-util-0.11-win64",
        r"C:\Espressif\tools\esp-clang\esp-20.1.1_20250829\esp-clang\bin",
        r"C:\Espressif\tools\esp-rom-elfs\20241011",
        r"C:\Espressif\tools\esp32ulp-elf\2.38_20240113\esp32ulp-elf\bin",
        r"C:\Espressif\tools\idf-exe\1.0.3",
        r"C:\Espressif\tools\ninja\1.12.1",
        r"C:\Espressif\tools\openocd-esp32\v0.12.0-esp32-20260304\openocd-esp32\bin",
        r"C:\Espressif\tools\riscv32-esp-elf-gdb\16.3_20250913\riscv32-esp-elf-gdb\bin",
        r"C:\Espressif\tools\riscv32-esp-elf\esp-15.2.0_20251204\riscv32-esp-elf\bin",
        r"C:\Espressif\tools\xtensa-esp-elf-gdb\16.3_20250913\xtensa-esp-elf-gdb\bin",
        r"C:\Espressif\tools\xtensa-esp-elf\esp-15.2.0_20251204\xtensa-esp-elf\bin",
        r"C:\Espressif\tools\python\v6.0.1\venv\Scripts"
    ]
    
    env["PATH"] = os.pathsep.join(idf_tools_bin) + os.pathsep + env["PATH"]
    
    # Ścieżka do idf.py
    idf_py = os.path.join(IDF_PATH, "tools", "idf.py")

    action = "flash"
    if monitor:
        action = "flash monitor"

    print(f"--- Rozpoczynam wgrywanie oprogramowania na port {port} (Baud: {baud}) ---")
    
    try:
        cmd = [VENV_PYTHON, idf_py, "-p", port, "-b", baud, "flash"]
        if monitor:
            cmd.append("monitor")
            
        result = subprocess.run(
            cmd,
            cwd=PROJECT_DIR,
            env=env,
            text=True
        )

        if result.returncode == 0:
            print("\n" + "="*40 + "\nSUKCES: Oprogramowanie wgrane pomyślnie!\n" + "="*40)
        else:
            print("\n" + "!"*40 + f"\nBŁĄD: Wgrywanie nie powiodło się (kod: {result.returncode})\n" + "!"*40)
            
    except Exception as e:
        print(f"Błąd krytyczny: {e}")

if __name__ == "__main__":
    monitor = "--monitor" in sys.argv or "-m" in sys.argv
    run_flash(monitor=monitor)
