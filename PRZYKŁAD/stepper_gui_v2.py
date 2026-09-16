"""Sterownik silnika 28BYJ-48 + ULN2003/2803, wersja 2 (oszczedna na pasmo).

Tryb ZADANIOWY (pod wolny most 9600 ESP32->STM32): wpisujesz kroki + predkosc
i wysylasz JEDNA komende MOVE:kroki,predkosc,accel. Arduino/STM32 wykonuje caly
ruch samodzielnie i odsylia DONE. Zero spamowania linku.

Tryb NA ZYWO (szybki link USB 115200): strzalki + suwak jak w v1.

Uzycie: pip install -r requirements.txt + python stepper_gui_v2.py
"""

import queue
import threading
import time
import tkinter as tk
from tkinter import ttk, messagebox

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    serial = None
    list_ports = None

BAUDS = ("9600", "57600", "115200", "921600")  # 921600 = USB ESP32 (ZEGAR most)
DEFAULT_BAUD = "115200"
STEPS_MIN, STEPS_MAX = -32768, 32768
SPEED_MIN, SPEED_MAX = 1, 1000
ACCEL_MIN, ACCEL_MAX = 50, 2000
JOB_TIMEOUT_S = 60.0
SEQ_TIMEOUT_S = 30.0
LIVE_CHUNK = 2000          # wielkość chunka w trybie live (łańcuchowany na DONE)
LIVE_BUSY_TIMEOUT_S = 20.0

BG = "#16191f"
BG2 = "#1f242d"
BG3 = "#2a313c"
FG = "#e8eaf0"
MUTED = "#9aa3b2"
ACCENT = "#3b82f6"
GREEN = "#22c55e"
RED = "#ef4444"
AMBER = "#f59e0b"

STEP_PRESETS = (512, 1024, 2048, 4096, 8192)  # 1/4, 1/2, 1, 2, 4 obrotu


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Sterownik silnika 28BYJ-48 v2  |  tryb zadaniowy + na zywo")
        self.geometry("760x780")
        self.minsize(680, 700)
        self.configure(bg=BG)

        # --- stan polaczenia ---
        self.ser = None
        self.connected = False
        self.reader_thread = None
        self.reader_stop = threading.Event()
        self.rx_queue = queue.Queue()
        self.poll_id = None

        # --- telemetria pasma ---
        self.tx_cmds = 0
        self.tx_bytes = 0
        self.rx_lines = 0

        # --- stan zadania ---
        self.job_busy = False
        self.job_deadline = 0.0
        self.link_ready = False   # True po READY/PONG (Uno resetuje sie przy otwarciu portu)
        self.next_ping = 0.0
        self.ping_tries = 0
        self.seq_lines = []
        self.seq_index = 0
        self.seq_active = False
        self.seq_waiting_done = False
        self.seq_deadline = 0.0
        # --- tryb testowy: petla +N/-N na polach z zadan ---
        self.test_active = False
        self.test_waiting = False
        self.test_plus = True
        self.test_steps = 0
        self.test_speed = 400
        self.test_accel = 400
        self.test_legs_left = 0   # None = nieskonczonosc
        self.test_legs_done = 0
        self.test_deadline = 0.0
        self.live_throttle_id = None
        # --- przytrzymanie live: trzymasz = łańcuch chunków aż puścisz ---
        self.live_held = None    # None / "LEFT" / "RIGHT"
        self.live_busy = False   # chunk w locie (czekamy na DONE)
        self.live_since = 0.0

        # --- zmienne UI ---
        self.var_port = tk.StringVar()
        self.var_baud = tk.StringVar(value=DEFAULT_BAUD)
        self.var_conn = tk.StringVar(value="Rozlaczony")
        self.var_steps = tk.StringVar(value="2048")
        self.var_speed = tk.StringVar(value="400")
        self.var_accel = tk.StringVar(value="400")
        self.var_dir = tk.StringVar(value="RIGHT")  # RIGHT=+ / LEFT=-
        self.var_job_state = tk.StringVar(value="Bezczynny")
        self.var_seq_state = tk.StringVar(value="Brak sekwencji")
        self.var_test_state = tk.StringVar(value="Pętla zatrzymana")
        self.var_test_cycles = tk.StringVar(value="10")
        self.var_live_speed = tk.IntVar(value=400)
        self.var_stats = tk.StringVar(value="TX: 0 komend / 0 B   RX: 0 linii")
        self.var_pos = tk.StringVar(value="POS: ?")
        self.var_autoscroll = tk.BooleanVar(value=True)
        self.var_autostatus = tk.BooleanVar(value=False)

        self._build_style()
        self._build_ui()
        self._poll()

    # ================= STYL =================
    def _build_style(self):
        s = ttk.Style(self)
        try:
            s.theme_use("clam")
        except tk.TclError:
            pass
        s.configure(".", background=BG, foreground=FG, fieldbackground=BG2,
                    bordercolor=BG3, lightcolor=BG3, darkcolor=BG3)
        s.configure("TFrame", background=BG)
        s.configure("Card.TFrame", background=BG2)
        s.configure("TLabel", background=BG, foreground=FG)
        s.configure("Card.TLabel", background=BG2, foreground=FG)
        s.configure("Muted.TLabel", background=BG, foreground=MUTED)
        s.configure("CardMuted.TLabel", background=BG2, foreground=MUTED)
        s.configure("Title.TLabel", background=BG, foreground=FG,
                    font=("Segoe UI", 14, "bold"))
        s.configure("State.TLabel", background=BG2, foreground=AMBER,
                    font=("Segoe UI", 11, "bold"))
        s.configure("TButton", padding=6)
        s.configure("Big.TButton", font=("Segoe UI", 18, "bold"), padding=8)
        s.configure("Go.TButton", font=("Segoe UI", 11, "bold"), padding=8)
        s.configure("TNotebook", background=BG, borderwidth=0)
        s.configure("TNotebook.Tab", background=BG3, foreground=FG, padding=(14, 6))
        s.map("TNotebook.Tab", background=[("selected", BG2)])
        s.configure("TProgressbar", background=ACCENT, troughcolor=BG3,
                    bordercolor=BG2, lightcolor=ACCENT, darkcolor=ACCENT)

    # ================= UI =================
    def _card(self, parent, title):
        f = ttk.Frame(parent, style="Card.TFrame", padding=10)
        f.pack(fill="x", padx=10, pady=6)
        ttk.Label(f, text=title, style="Card.TLabel",
                  font=("Segoe UI", 10, "bold")).pack(anchor="w", pady=(0, 6))
        return f

    def _build_ui(self):
        # --- naglowek ---
        head = ttk.Frame(self)
        head.pack(fill="x", padx=10, pady=(10, 0))
        ttk.Label(head, text="\u2699  Sterownik 28BYJ-48 v2",
                  style="Title.TLabel").pack(side="left")
        self.lbl_conn = ttk.Label(head, textvariable=self.var_conn,
                                  font=("Segoe UI", 10, "bold"), foreground=RED)
        self.lbl_conn.pack(side="right")

        # --- polaczenie ---
        c = self._card(self, "Polaczenie  (Uno: 115200  |  most ESP32/STM32: 9600)")
        grid = ttk.Frame(c, style="Card.TFrame")
        grid.pack(fill="x")
        grid.columnconfigure(1, weight=1)
        ttk.Label(grid, text="Port:", style="Card.TLabel").grid(row=0, column=0,
                                                                sticky="w", padx=(0, 6))
        self.port_box = ttk.Combobox(grid, textvariable=self.var_port, width=16,
                                     state="readonly")
        self.port_box.grid(row=0, column=1, sticky="ew", padx=(0, 6))
        ttk.Label(grid, text="Baud:", style="Card.TLabel").grid(row=0, column=2,
                                                                sticky="w", padx=(0, 6))
        self.baud_box = ttk.Combobox(grid, textvariable=self.var_baud, width=9,
                                     state="readonly", values=BAUDS)
        self.baud_box.grid(row=0, column=3, sticky="ew", padx=(0, 6))
        ttk.Button(grid, text="Odswiez",
                   command=self._refresh_ports).grid(row=0, column=4, padx=(0, 6))
        self.btn_conn = ttk.Button(grid, text="Polacz",
                                   command=self._toggle_conn, width=10)
        self.btn_conn.grid(row=0, column=5)
        ttk.Label(c, textvariable=self.var_stats,
                  style="CardMuted.TLabel").pack(anchor="w", pady=(6, 0))

        # --- zakladki ---
        self.tabs = ttk.Notebook(self)
        self.tabs.pack(fill="both", expand=True, padx=10, pady=6)
        self.tab_job = ttk.Frame(self.tabs)
        self.tab_live = ttk.Frame(self.tabs)
        self.tab_log = ttk.Frame(self.tabs)
        self.tabs.add(self.tab_job, text="  Zadania (oszczedne)  ")
        self.tabs.add(self.tab_live, text="  Na zywo  ")
        self.tabs.add(self.tab_log, text="  Log UART  ")
        self._build_job_tab()
        self._build_live_tab()
        self._build_log_tab()

        # --- pasek statusu ---
        bar = ttk.Frame(self)
        bar.pack(fill="x", padx=10, pady=(0, 10))
        ttk.Label(bar, textvariable=self.var_pos,
                  font=("Segoe UI", 10, "bold")).pack(side="left")
        ttk.Label(bar, text="strzalki dzialaja na zakladce Na zywo, spacja = STOP",
                  style="Muted.TLabel").pack(side="right")

        self.bind("<Left>", self._key_left)
        self.bind("<KeyRelease-Left>", self._key_release)
        self.bind("<Right>", self._key_right)
        self.bind("<KeyRelease-Right>", self._key_release)
        self.bind("<space>", lambda e: self._stop())
        self.protocol("WM_DELETE_WINDOW", self._on_close)
        self._refresh_ports()

    # ---------- zakladka: zadania ----------
    def _build_job_tab(self):
        c = self._card(self.tab_job,
                       "1. Ruch zadaniowy  —  1 komenda = caly ruch + DONE (nie spamuje linku)")
        form = ttk.Frame(c, style="Card.TFrame")
        form.pack(fill="x")
        for i in (0, 1, 2, 3):
            form.columnconfigure(i, weight=1, uniform="job")

        ttk.Label(form, text="Kroki (+/-):", style="Card.TLabel").grid(row=0, column=0,
                                                                       sticky="w")
        ttk.Label(form, text="Predkosc (1-1000):", style="Card.TLabel").grid(row=0, column=1,
                                                                              sticky="w")
        ttk.Label(form, text="Accel (50-2000):", style="Card.TLabel").grid(row=0, column=2,
                                                                            sticky="w")
        ttk.Label(form, text="Kierunek:", style="Card.TLabel").grid(row=0, column=3,
                                                                     sticky="w")
        ttk.Entry(form, textvariable=self.var_steps, width=10).grid(row=1, column=0,
                                                                     sticky="ew", padx=(0, 6))
        ttk.Entry(form, textvariable=self.var_speed, width=10).grid(row=1, column=1,
                                                                     sticky="ew", padx=(0, 6))
        ttk.Entry(form, textvariable=self.var_accel, width=10).grid(row=1, column=2,
                                                                     sticky="ew", padx=(0, 6))
        dirbox = ttk.Frame(form, style="Card.TFrame")
        dirbox.grid(row=1, column=3, sticky="ew")
        ttk.Radiobutton(dirbox, text="\u25b6 +", value="RIGHT",
                        variable=self.var_dir).pack(side="left")
        ttk.Radiobutton(dirbox, text="\u25c0 -", value="LEFT",
                        variable=self.var_dir).pack(side="left")

        pre = ttk.Frame(c, style="Card.TFrame")
        pre.pack(fill="x", pady=(8, 0))
        ttk.Label(pre, text="Presety:", style="CardMuted.TLabel").pack(side="left",
                                                                        padx=(0, 4))
        for v in STEP_PRESETS:
            ttk.Button(pre, text=str(v), width=6,
                       command=lambda v=v: self.var_steps.set(str(v))).pack(side="left",
                                                                             padx=2)

        btns = ttk.Frame(c, style="Card.TFrame")
        btns.pack(fill="x", pady=(8, 0))
        btns.columnconfigure(0, weight=2)
        btns.columnconfigure(1, weight=1)
        btns.columnconfigure(2, weight=1)
        self.btn_go = ttk.Button(btns, text="\u25b6  WYKONAJ RUCH", style="Go.TButton",
                                 command=self._do_job)
        self.btn_go.grid(row=0, column=0, sticky="ew", padx=(0, 6))
        ttk.Button(btns, text="\u25a0 STOP",
                   command=self._stop).grid(row=0, column=1, sticky="ew", padx=(0, 6))
        ttk.Button(btns, text="GOTO 0 (powrot)",
                   command=self._goto_zero).grid(row=0, column=2, sticky="ew")

        self.lbl_job_err = ttk.Label(c, text="", style="Card.TLabel", foreground=RED,
                                     wraplength=640, justify="left")
        self.lbl_job_err.pack(anchor="w", pady=(4, 0))
        self.lbl_job_state = ttk.Label(c, textvariable=self.var_job_state,
                                       style="State.TLabel")
        self.lbl_job_state.pack(anchor="w", pady=(2, 0))
        self.job_bar = ttk.Progressbar(c, mode="indeterminate", length=200)
        self.job_bar.pack(fill="x", pady=(4, 0))

        # --- sekwencja ---
        s = self._card(self.tab_job, "2. Sekwencja  —  linie wykona sie po kolei (czekajac na DONE)")
        ttk.Label(s, text="Jedna komenda na linie. Komentarz po #:",
                  style="CardMuted.TLabel").pack(anchor="w")
        self.seq_box = tk.Text(s, height=5, bg="#11141a", fg="#c9d1d9",
                               insertbackground=FG, font=("Consolas", 10))
        self.seq_box.pack(fill="x", pady=(4, 0))
        self.seq_box.insert("1.0", "MOVE:2048,400,400   # 1 obrot w prawo\n"
                                   "MOVE:-2048,400,400  # 1 obrot w lewo\n"
                                   "GOTO:0,500          # powrot do zera\n")
        row = ttk.Frame(s, style="Card.TFrame")
        row.pack(fill="x", pady=(6, 0))
        self.btn_seq = ttk.Button(row, text="Wyslij sekwencje",
                                  command=self._seq_start)
        self.btn_seq.pack(side="left", padx=(0, 6))
        ttk.Button(row, text="Przerwij", command=self._seq_abort).pack(side="left")
        ttk.Label(row, textvariable=self.var_seq_state,
                  style="Card.TLabel").pack(side="left", padx=10)

        # --- tryb testowy: petla +N / -N ---
        t = self._card(self.tab_job, "3. Tryb testowy  —  petla +N / -N na polach z karty 1 (luz, gubienie krokow)")
        ttk.Label(t, text="Bierze Kroki / Predkosc / Accel z karty 1 i jezdzi +N, -N, +N... "
                          "Liczba cykli 0 = w nieskonczonosc (do STOP).",
                  style="CardMuted.TLabel", wraplength=640,
                  justify="left").pack(anchor="w")
        trow = ttk.Frame(t, style="Card.TFrame")
        trow.pack(fill="x", pady=(6, 0))
        ttk.Label(trow, text="Cykle:", style="Card.TLabel").pack(side="left")
        ttk.Entry(trow, textvariable=self.var_test_cycles, width=6).pack(side="left",
                                                                          padx=(4, 6))
        self.btn_test = ttk.Button(trow, text="\u25b6 Start petli",
                                   command=self._test_start)
        self.btn_test.pack(side="left", padx=(0, 6))
        ttk.Button(trow, text="\u25a0 Stop petli",
                   command=lambda: self._test_abort("Petla zatrzymana")).pack(side="left")
        ttk.Label(trow, textvariable=self.var_test_state,
                  style="Card.TLabel").pack(side="left", padx=10)

    # ---------- zakladka: na zywo ----------
    def _build_live_tab(self):
        c = self._card(self.tab_live, "Sterowanie na zywo  —  przytrzymaj, jedzie chunkami 2000 na DONE")
        ttk.Label(c, text="Przytrzymaj przycisk / strzalke: jedzie dopoki trzymasz. Pusc: stop.",
                  style="CardMuted.TLabel").pack(anchor="w")
        row = ttk.Frame(c, style="Card.TFrame")
        row.pack(fill="x", pady=8)
        for i in range(3):
            row.columnconfigure(i, weight=1, uniform="live")
        self.btn_left = ttk.Button(row, text="\u25c0 LEWO", style="Big.TButton")
        self.btn_left.grid(row=0, column=0, sticky="ew", padx=(0, 4))
        ttk.Button(row, text="\u25a0 STOP", style="Big.TButton",
                   command=self._stop).grid(row=0, column=1, sticky="ew", padx=4)
        self.btn_right = ttk.Button(row, text="PRAWO \u25b6", style="Big.TButton")
        self.btn_right.grid(row=0, column=2, sticky="ew", padx=(4, 0))
        self.btn_left.bind("<ButtonPress-1>", lambda e: self._live_hold("LEFT"))
        self.btn_left.bind("<ButtonRelease-1>", lambda e: self._live_release())
        self.btn_right.bind("<ButtonPress-1>", lambda e: self._live_hold("RIGHT"))
        self.btn_right.bind("<ButtonRelease-1>", lambda e: self._live_release())

        ttk.Label(c, textvariable=self.var_live_speed,
                  style="Card.TLabel", font=("Segoe UI", 11, "bold")).pack(anchor="w")
        # live label aktualizowany w _on_live_drag
        self.lbl_live_speed = ttk.Label(c, text="400 krokow/s (live)",
                                        style="Card.TLabel",
                                        font=("Segoe UI", 11, "bold"))
        self.lbl_live_speed.pack(anchor="w")
        self.live_slider = ttk.Scale(c, from_=0, to=1000, orient="horizontal",
                                     variable=self.var_live_speed,
                                     command=self._on_live_drag)
        self.live_slider.pack(fill="x", pady=4)
        self.live_slider.bind("<ButtonRelease-1>", lambda e: self._send(f"SPEED:{int(self.var_live_speed.get())}"))
        quick = ttk.Frame(c, style="Card.TFrame")
        quick.pack(fill="x")
        for v in (100, 400, 700, 1000):
            ttk.Button(quick, text=str(v), width=6,
                       command=lambda v=v: self._live_set_speed(v)).pack(side="left", padx=3)

    # ---------- zakladka: log ----------
    def _build_log_tab(self):
        bar = ttk.Frame(self.tab_log)
        bar.pack(fill="x", padx=10, pady=(10, 0))
        ttk.Checkbutton(bar, text="Autoscroll",
                        variable=self.var_autoscroll).pack(side="left")
        ttk.Checkbutton(bar, text="Auto-STATUS co 2 s (uwaga: ruch na linku)",
                        variable=self.var_autostatus).pack(side="left", padx=12)
        ttk.Button(bar, text="Odpytaj STATUS",
                   command=lambda: self._send("STATUS")).pack(side="left", padx=6)
        ttk.Button(bar, text="Wyczysc",
                   command=self._log_clear).pack(side="right")
        self.log_box = tk.Text(self.tab_log, bg="#11141a", fg="#c9d1d9",
                               insertbackground=FG, font=("Consolas", 9))
        self.log_box.pack(fill="both", expand=True, padx=10, pady=6)

    # ================= SERIAL =================
    def _refresh_ports(self):
        if not list_ports:
            self._log("Zainstaluj pyserial: pip install -r requirements.txt")
            return
        try:
            ports = [p.device for p in list_ports.comports()]
        except Exception as ex:
            self._log(f"Blad listy portow: {ex}")
            ports = []
        self.port_box["values"] = ports
        if ports and not self.var_port.get():
            self.var_port.set("COM4" if "COM4" in ports else ports[0])

    def _toggle_conn(self):
        if self.connected:
            self._disconnect()
        else:
            self._connect()

    def _connect(self):
        if serial is None:
            messagebox.showerror("Brak pyserial",
                                 "Uruchom: pip install -r requirements.txt")
            return
        port = self.var_port.get()
        try:
            baud = int(self.var_baud.get())
        except ValueError:
            messagebox.showwarning("Baud", "Wybierz predkosc z listy.")
            return
        if not port:
            messagebox.showwarning("Brak portu", "Wybierz port COM z listy.")
            return
        try:
            self.ser = serial.Serial(port, baud, timeout=0.1)
        except Exception as ex:
            messagebox.showerror("Blad polaczenia", str(ex))
            return
        self.connected = True
        self.link_ready = False  # Uno resetuje sie przy otwarciu portu (~2 s)
        self.ping_tries = 0
        self.next_ping = 0.0
        self.tx_cmds = 0
        self.tx_bytes = 0
        self.rx_lines = 0
        self._update_stats()
        self.btn_conn.config(text="Rozlacz")
        self.var_conn.set(f"Polaczony: {port} @ {baud}")
        self.lbl_conn.config(foreground=GREEN)
        self.reader_stop.clear()
        self.reader_thread = threading.Thread(target=self._reader, daemon=True)
        self.reader_thread.start()
        self._log(f">>> Polaczono {port} @ {baud} (czekam na READY...)")

    def _disconnect(self, silent=False):
        if self.ser:
            try:
                self.ser.write(b"STOP\n")
            except Exception:
                pass
        self.reader_stop.set()
        try:
            if self.ser:
                self.ser.close()
        except Exception:
            pass
        self.ser = None
        self.connected = False
        self.job_busy = False
        self.seq_active = False
        self.test_active = False
        self.test_waiting = False
        self.live_held = None
        self.live_busy = False
        self.job_bar.stop()
        self.var_job_state.set("Bezczynny")
        self.var_seq_state.set("Brak sekwencji")
        self.btn_conn.config(text="Polacz")
        self.var_conn.set("Rozlaczony")
        self.lbl_conn.config(foreground=RED)
        if not silent:
            self._log(">>> Rozlaczono")

    def _reader(self):
        buf = b""
        while not self.reader_stop.is_set():
            try:
                data = self.ser.read(64)
            except Exception:
                break
            if data:
                buf += data
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    self.rx_queue.put(line.decode(errors="ignore").strip())

    def _poll(self):
        while not self.rx_queue.empty():
            self._on_line(self.rx_queue.get())
        now = time.monotonic()
        # handshake po polaczeniu: Uno rebootuje przy otwarciu portu, pytamy o PONG
        if self.connected and not self.link_ready:
            if now >= self.next_ping:
                self.next_ping = now + 0.5
                self.ping_tries += 1
                self._send("PING", quiet=True)
                if self.ping_tries > 20:
                    self._log("BLAD: brak odpowiedzi READY/PONG z plytki")
                    self.next_ping = now + 5.0
                    self.ping_tries = 0
        # timeouty zadan / sekwencji
        if self.job_busy and now > self.job_deadline and not self.seq_active:
            self.job_busy = False
            self.job_bar.stop()
            self.var_job_state.set("BLAD: brak DONE (timeout)")
            self.lbl_job_state.config(foreground=RED)
        if self.seq_active and self.seq_waiting_done and now > self.seq_deadline:
            self._seq_abort(f"BLAD: brak DONE dla linii {self.seq_index + 1} (timeout)")
        if self.test_active and self.test_waiting and now > self.test_deadline:
            self._test_abort("BLAD: brak DONE (timeout)")
            self.lbl_job_state.config(foreground=RED)
        # live: trzymany przycisk = kolejny chunk (łańcuch na DONE, 1 ramka/chunk)
        if self.live_busy and now - self.live_since > LIVE_BUSY_TIMEOUT_S:
            self.live_busy = False  # zablokowane DONE — odblokuj, pompa wyśle od nowa
        if (self.live_held is not None and not self.live_busy
                and not self.job_busy and not self.seq_active
                and self.link_ready and self.connected):
            self._send_live_chunk()
        if self.connected and self.var_autostatus.get():
            if now >= getattr(self, "_next_autostatus", 0):
                self._send("STATUS", quiet=True)
                self._next_autostatus = now + 2.0
        self.poll_id = self.after(100, self._poll)

    def _on_line(self, line):
        if not line:
            return
        self.rx_lines += 1
        self._update_stats()
        self._log(f"RX: {line}")
        if line == "PONG" or line.startswith("READY"):
            if not self.link_ready:
                self.link_ready = True
                self._log(">>> Link gotowy")
                self._send("MODE:STEP")  # bazowy tryb pod zadania
                self._send("STATUS")
            return
        if line.startswith("DONE"):
            pos = line.split("POS=", 1)[1].strip() if "POS=" in line else "?"
            self.var_pos.set(f"POS: {pos}")
            self.live_busy = False  # live: łańcuch jedzie dalej w _poll
            if self.test_active and self.test_waiting:
                self._test_on_done()
            elif self.seq_active and self.seq_waiting_done:
                self.seq_waiting_done = False
                self._seq_next()
            elif self.job_busy:
                self.job_busy = False
                self.job_bar.stop()
                self.var_job_state.set(f"Zakonczono: {line}")
                self.lbl_job_state.config(foreground=GREEN)
        elif line.startswith("OK JOB"):
            self.var_job_state.set(f"Wykonuje: {line}")
            self.lbl_job_state.config(foreground=AMBER)
            self.job_bar.start(12)
        elif line.startswith("OK STOP"):
            self.job_busy = False
            self.live_busy = False
            self.job_bar.stop()
            if self.test_active:
                self._test_abort("Przerwano (STOP)")
            self.var_job_state.set(f"Zatrzymano: {line}")
            self.lbl_job_state.config(foreground=RED)
            if self.seq_active:
                self._seq_abort("Przerwano (STOP)")
        elif line.startswith("ST ") or line.startswith("OK MODE"):
            self._parse_status(line)

    def _parse_status(self, line):
        for tok in line.split():
            if tok.startswith("POS="):
                try:
                    self.var_pos.set(f"POS: {int(tok[4:])}")
                except ValueError:
                    pass

    # ================= WYSYLANIE =================
    def _send(self, cmd, quiet=False):
        if not self.connected or not self.ser:
            if not quiet:
                self.lbl_job_err.config(text="Najpierw sie polacz.")
            return False
        try:
            raw = (cmd + "\n").encode()
            self.ser.write(raw)
            self.tx_cmds += 1
            self.tx_bytes += len(raw)
            self._update_stats()
            if not quiet:
                self._log(f"TX: {cmd}")
            return True
        except Exception as ex:
            self._log(f"BLAD TX: {ex}")
            self._disconnect()
            return False

    def _update_stats(self):
        self.var_stats.set(f"TX: {self.tx_cmds} komend / {self.tx_bytes} B"
                           f"   RX: {self.rx_lines} linii")

    # ================= ZADANIA =================
    def _read_job_fields(self):
        try:
            steps = int(self.var_steps.get().strip())
        except ValueError:
            return None, "Kroki musza byc liczba calkowita."
        try:
            speed = int(self.var_speed.get().strip())
        except ValueError:
            return None, "Predkosc musi byc liczba calkowita."
        try:
            accel = int(self.var_accel.get().strip())
        except ValueError:
            return None, "Accel musi byc liczba calkowita."
        if not STEPS_MIN <= steps <= STEPS_MAX:
            return None, f"Kroki poza zakresem {STEPS_MIN}..{STEPS_MAX}."
        if not SPEED_MIN <= speed <= SPEED_MAX:
            return None, f"Predkosc poza zakresem {SPEED_MIN}..{SPEED_MAX}."
        if not ACCEL_MIN <= accel <= ACCEL_MAX:
            return None, f"Accel poza zakresem {ACCEL_MIN}..{ACCEL_MAX}."
        if self.var_dir.get() == "LEFT":
            steps = -abs(steps)
        else:
            steps = abs(steps)
        return (steps, speed, accel), ""

    def _do_job(self):
        if not self.link_ready:
            self.lbl_job_err.config(text="Link niegotowy — czekam na READY/PONG.")
            return
        if self.job_busy or self.seq_active or self.test_active:
            self.lbl_job_err.config(text="Poczekaj na koniec biezacego ruchu.")
            return
        vals, err = self._read_job_fields()
        if err:
            self.lbl_job_err.config(text=err)
            return
        self.lbl_job_err.config(text="")
        steps, speed, accel = vals
        if self._send(f"MOVE:{steps},{speed},{accel}"):
            self.job_busy = True
            self.job_deadline = time.monotonic() + JOB_TIMEOUT_S
            self.var_job_state.set("Wyslano, czekam na start...")
            self.lbl_job_state.config(foreground=AMBER)

    def _goto_zero(self):
        if self.job_busy or self.seq_active or self.test_active:
            return
        try:
            speed = int(self.var_speed.get().strip())
        except ValueError:
            speed = 400
        speed = max(SPEED_MIN, min(SPEED_MAX, speed))
        if self._send(f"GOTO:0,{speed}"):
            self.job_busy = True
            self.job_deadline = time.monotonic() + JOB_TIMEOUT_S
            self.var_job_state.set("Powrot do zera...")
            self.lbl_job_state.config(foreground=AMBER)

    def _stop(self):
        self.live_held = None
        self.live_busy = False
        if self.test_active:
            self._test_abort("Przerwano (STOP)")
        self._send("STOP")
        if self.seq_active:
            self._seq_abort("Przerwano (STOP)")

    # ================= SEKWENCJA =================
    def _seq_parse(self):
        cmds = []
        for raw in self.seq_box.get("1.0", "end").splitlines():
            line = raw.split("#", 1)[0].strip().upper()
            if line:
                cmds.append(line)
        return cmds

    def _seq_start(self):
        if not self.link_ready:
            self.var_seq_state.set("Link niegotowy — czekam na READY/PONG")
            return
        if self.seq_active or self.job_busy or self.test_active:
            return
        cmds = self._seq_parse()
        if not cmds:
            self.var_seq_state.set("Pusta sekwencja")
            return
        self.seq_lines = cmds
        self.seq_index = 0
        self.seq_active = True
        self.var_seq_state.set(f"Start: {len(cmds)} komend")
        self._seq_next()

    def _seq_next(self):
        if not self.seq_active:
            return
        if self.seq_index >= len(self.seq_lines):
            self.seq_active = False
            self.seq_waiting_done = False
            self.var_seq_state.set("Sekwencja zakonczona OK")
            self.var_job_state.set("Sekwencja zakonczona OK")
            self.lbl_job_state.config(foreground=GREEN)
            self.job_bar.stop()
            return
        cmd = self.seq_lines[self.seq_index]
        self.seq_index += 1
        self.var_seq_state.set(f"Krok {self.seq_index}/{len(self.seq_lines)}: {cmd}")
        if not self._send(cmd):
            self._seq_abort("Blad wysylania")
            return
        waits_done = cmd.startswith(("MOVE:", "GOTO:", "STEP:"))
        if waits_done:
            self.seq_waiting_done = True
            self.seq_deadline = time.monotonic() + SEQ_TIMEOUT_S
            self.job_bar.start(12)
            self.var_job_state.set(f"Sekwencja {self.seq_index}/{len(self.seq_lines)}: {cmd}")
        else:
            # komenda natychmiastowa (STOP/SPEED/STATUS...) - idz dalej po chwili
            self.seq_waiting_done = False
            self.after(250, self._seq_next)

    def _seq_abort(self, msg="Przerwano"):
        self.seq_active = False
        self.seq_waiting_done = False
        self.job_busy = False
        self.job_bar.stop()
        self.var_seq_state.set(msg)
        self.var_job_state.set(msg)
        self.lbl_job_state.config(foreground=RED)

    # ================= TRYB TESTOWY (petla +N/-N) =================
    def _test_start(self):
        if not self.link_ready:
            self.var_test_state.set("Link niegotowy — czekam na READY/PONG")
            return
        if self.test_active or self.job_busy or self.seq_active:
            return
        vals, err = self._read_job_fields()
        if err:
            self.var_test_state.set(err)
            return
        steps, speed, accel = vals
        steps = abs(steps)
        if steps == 0:
            self.var_test_state.set("Kroki musza byc rozne od 0.")
            return
        try:
            cycles = int(self.var_test_cycles.get().strip())
        except ValueError:
            self.var_test_state.set("Cykle musza byc liczba (0 = bez konca).")
            return
        if cycles < 0:
            self.var_test_state.set("Cykle nie moga byc ujemne.")
            return
        self.test_steps = steps
        self.test_speed = speed
        self.test_accel = accel
        self.test_plus = True
        self.test_legs_done = 0
        self.test_legs_left = None if cycles == 0 else cycles * 2
        self.test_active = True
        self.test_waiting = False
        total = "nieskonczonosc" if cycles == 0 else f"{cycles} cykli"
        self._log(f">>> Petla testowa: +-{steps} @ {speed}/{accel}, {total}")
        self._test_next()

    def _test_next(self):
        if not self.test_active:
            return
        if self.test_legs_left is not None and self.test_legs_left <= 0:
            self._test_abort(f"Petla zakonczona ({self.test_legs_done // 2} cykli)")
            self.var_test_state.set(f"Petla zakonczona OK ({self.test_legs_done // 2} cykli)")
            self.lbl_job_state.config(foreground=GREEN)
            return
        steps = self.test_steps if self.test_plus else -self.test_steps
        cmd = f"MOVE:{steps},{self.test_speed},{self.test_accel}"
        if not self._send(cmd):
            self._test_abort("Blad wysylania")
            return
        self.test_waiting = True
        self.test_deadline = time.monotonic() + JOB_TIMEOUT_S
        leg = self.test_legs_done + 1
        left = "inf" if self.test_legs_left is None else str(self.test_legs_left - 1)
        self.var_test_state.set(f"Noga {leg} (zostalo {left}): {cmd}")
        self.job_bar.start(12)

    def _test_on_done(self):
        # Wywolane z _on_line przy DONE — wysyla kolejna noge.
        if not (self.test_active and self.test_waiting):
            return
        self.test_waiting = False
        self.test_legs_done += 1
        if self.test_legs_left is not None:
            self.test_legs_left -= 1
        self.test_plus = not self.test_plus
        self._test_next()

    def _test_abort(self, msg="Petla zatrzymana"):
        was = self.test_active
        self.test_active = False
        self.test_waiting = False
        self.var_test_state.set(f"{msg} (nog: {self.test_legs_done})")
        if was and not self.seq_active and not self.job_busy:
            self.job_bar.stop()

    # ================= NA ZYWO (przytrzymaj = jedzie) =================
    # Dziala na Uno i na moscie ESP32/STM32: trzymany przycisk = lancuch
    # chunkow MOVE wysylanych na DONE (1 ramka ~20 B na chunk, brak spamu).
    # Puszczenie = STOP (tnie chunk w locie). Predkosc z suwaka live.
    def _live_hold(self, side):
        if not self.link_ready or self.job_busy or self.seq_active or self.test_active:
            return
        if self.live_held == side:
            return  # autorepeat klawiatury — ignoruj
        self.live_held = side
        self._send("MODE:STEP", quiet=True)
        self._send_live_chunk()

    def _send_live_chunk(self):
        if self.live_held is None or self.live_busy:
            return
        try:
            speed = int(self.var_live_speed.get())
        except (tk.TclError, ValueError):
            speed = 400
        speed = max(SPEED_MIN, min(SPEED_MAX, speed))
        steps = -LIVE_CHUNK if self.live_held == "LEFT" else LIVE_CHUNK
        if self._send(f"MOVE:{steps},{speed},1000", quiet=True):
            self.live_busy = True
            self.live_since = time.monotonic()

    def _live_release(self):
        was_held = self.live_held is not None
        was_busy = self.live_busy
        self.live_held = None
        self.live_busy = False
        if was_held and was_busy:
            self._send("STOP", quiet=True)  # dotnij chunk w locie

    def _on_live_drag(self, val=None):
        try:
            v = int(float(val))
        except (TypeError, ValueError):
            return
        self.lbl_live_speed.config(text=f"{v} krokow/s (live)")
        # throttle: maks 1 komenda / 150 ms podczas ciagniecia suwaka
        if self.live_throttle_id:
            self.after_cancel(self.live_throttle_id)
        self.live_throttle_id = self.after(
            150, lambda: self._send(f"SPEED:{int(self.var_live_speed.get())}", quiet=True))

    def _live_set_speed(self, v):
        self.var_live_speed.set(v)
        self.lbl_live_speed.config(text=f"{v} krokow/s (live)")
        self._send(f"SPEED:{v}")

    def _key_left(self, _e):
        if self.tabs.index(self.tabs.select()) == 1:
            self._live_hold("LEFT")

    def _key_right(self, _e):
        if self.tabs.index(self.tabs.select()) == 1:
            self._live_hold("RIGHT")

    def _key_release(self, _e):
        if self.tabs.index(self.tabs.select()) == 1:
            self._live_release()

    # ================= LOG =================
    def _log(self, msg):
        box = self.__dict__.get("log_box")
        stamp = time.strftime("%H:%M:%S")
        line = f"[{stamp}] {msg}\n"
        if box is None:
            print(line, end="")
            return
        box.insert("end", line)
        if self.var_autoscroll.get():
            box.see("end")

    def _log_clear(self):
        self.log_box.delete("1.0", "end")

    def _on_close(self):
        if self.poll_id:
            try:
                self.after_cancel(self.poll_id)
            except tk.TclError:
                pass
        self._disconnect(silent=True)
        self.destroy()


if __name__ == "__main__":
    if serial is None:
        raise SystemExit("Brak pyserial. Uruchom: pip install -r requirements.txt")
    App().mainloop()
