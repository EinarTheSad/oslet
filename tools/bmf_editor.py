#!/usr/bin/env python3
"""
BMF Font Editor - Professional bitmap font editor for osLET
Creates pixel-perfect fonts by manual editing or smart TTF conversion
"""

import struct
import tkinter as tk
from tkinter import ttk, filedialog, messagebox, scrolledtext, simpledialog
from PIL import Image, ImageDraw, ImageFont, ImageTk
import os
import sys

class BMFFont:
    def __init__(self):
        self.name = "Untitled"
        self.version = 1
        self.flags = 0
        self.sequences = []
    
    def add_sequence(self, height, baseline, point_size):
        seq = {
            'height': height,
            'baseline': baseline,
            'point_size': point_size,
            'glyphs': {}
        }
        for i in range(256):
            seq['glyphs'][i] = self.create_empty_glyph(i, height)
        self.sequences.append(seq)
        return seq
    
    def create_empty_glyph(self, ascii_code, height):
        width = 8 if ascii_code == 32 else 6
        return {
            'ascii': ascii_code,
            'width': width,
            'bitmap': [[0] * width for _ in range(height)]
        }
    
    def save(self, path):
        with open(path, 'wb') as f:
            f.write(b'BMF\0')
            name_bytes = self.name[:28].encode('ascii', errors='ignore').ljust(28, b'\x00')
            f.write(name_bytes)
            f.write(struct.pack('<B', self.version))
            f.write(struct.pack('<B', self.flags))
            f.write(struct.pack('<H', len(self.sequences)))
            
            for seq in self.sequences:
                f.write(struct.pack('<B', seq['height']))
                f.write(struct.pack('<B', seq['baseline']))
                f.write(struct.pack('<B', seq['point_size']))
                f.write(struct.pack('<H', 256))
                
                for i in range(256):
                    glyph = seq['glyphs'][i]
                    width = glyph['width']
                    pitch = (width + 7) // 8
                    
                    bitmap_bytes = []
                    for row in glyph['bitmap']:
                        byte_val = 0
                        bit_count = 0
                        for x in range(width):
                            pixel = row[x] if x < len(row) else 0
                            if pixel:
                                byte_val |= (1 << (7 - bit_count))
                            bit_count += 1
                            if bit_count == 8:
                                bitmap_bytes.append(byte_val)
                                byte_val = 0
                                bit_count = 0
                        if bit_count > 0:
                            bitmap_bytes.append(byte_val)
                    
                    f.write(struct.pack('<B', i))
                    f.write(struct.pack('<B', width))
                    f.write(struct.pack('<B', pitch))
                    f.write(bytes(bitmap_bytes))
    
    def load(self, path):
        with open(path, 'rb') as f:
            magic = f.read(4)
            if magic != b'BMF\0':
                raise ValueError("Invalid BMF file")
            
            self.name = f.read(28).rstrip(b'\x00').decode('ascii', errors='ignore')
            self.version = struct.unpack('<B', f.read(1))[0]
            self.flags = struct.unpack('<B', f.read(1))[0]
            seq_count = struct.unpack('<H', f.read(2))[0]
            
            self.sequences = []
            for _ in range(seq_count):
                height = struct.unpack('<B', f.read(1))[0]
                baseline = struct.unpack('<B', f.read(1))[0]
                point_size = struct.unpack('<B', f.read(1))[0]
                glyph_count = struct.unpack('<H', f.read(2))[0]
                
                seq = {
                    'height': height,
                    'baseline': baseline,
                    'point_size': point_size,
                    'glyphs': {}
                }
                
                for _ in range(glyph_count):
                    ascii_code = struct.unpack('<B', f.read(1))[0]
                    width = struct.unpack('<B', f.read(1))[0]
                    pitch = struct.unpack('<B', f.read(1))[0]
                    
                    bitmap = [[0] * width for _ in range(height)]
                    
                    for y in range(height):
                        bits_read = 0
                        for _ in range(pitch):
                            byte_val = struct.unpack('<B', f.read(1))[0]
                            for bit in range(8):
                                if bits_read < width:
                                    if byte_val & (1 << (7 - bit)):
                                        bitmap[y][bits_read] = 1
                                    bits_read += 1
                    
                    seq['glyphs'][ascii_code] = {
                        'ascii': ascii_code,
                        'width': width,
                        'bitmap': bitmap
                    }
                
                self.sequences.append(seq)

class BMFEditor(tk.Tk):
    def __init__(self):
        super().__init__()
        
        self.title("BMF Font Editor - osLET")
        self.geometry("1200x800")
        
        self.font = BMFFont()
        self.current_seq_idx = 0
        self.current_file = None
        
        self.undo_stack = []
        self.redo_stack = []
        
        self.create_menu()
        self.create_toolbar()
        self.create_main_area()
        self.create_statusbar()
        
        self.new_font()
    
    def create_menu(self):
        menubar = tk.Menu(self)
        self.config(menu=menubar)
        
        file_menu = tk.Menu(menubar, tearoff=0)
        menubar.add_cascade(label="File", menu=file_menu)
        file_menu.add_command(label="New", command=self.new_font, accelerator="Ctrl+N")
        file_menu.add_command(label="Open...", command=self.open_font, accelerator="Ctrl+O")
        file_menu.add_command(label="Save", command=self.save_font, accelerator="Ctrl+S")
        file_menu.add_command(label="Save As...", command=self.save_font_as)
        file_menu.add_separator()
        file_menu.add_command(label="Exit", command=self.quit)
        
        edit_menu = tk.Menu(menubar, tearoff=0)
        menubar.add_cascade(label="Edit", menu=edit_menu)
        edit_menu.add_command(label="Undo", command=self.undo, accelerator="Ctrl+Z")
        edit_menu.add_command(label="Redo", command=self.redo, accelerator="Ctrl+Y")
        
        import_menu = tk.Menu(menubar, tearoff=0)
        menubar.add_cascade(label="Import", menu=import_menu)
        import_menu.add_command(label="From TTF (Single Size)...", command=self.import_ttf)
        import_menu.add_command(label="From TTF (osLET Standard Sizes)...", command=self.import_ttf_oslet)
        
        seq_menu = tk.Menu(menubar, tearoff=0)
        menubar.add_cascade(label="Sequence", menu=seq_menu)
        seq_menu.add_command(label="Add Size...", command=self.add_sequence)
        seq_menu.add_command(label="Remove Current", command=self.remove_sequence)
        
        tools_menu = tk.Menu(menubar, tearoff=0)
        menubar.add_cascade(label="Tools", menu=tools_menu)
        tools_menu.add_command(label="Batch Operations...", command=self.batch_operations)
        tools_menu.add_separator()
        tools_menu.add_command(label="Clone Glyph...", command=self.clone_glyph)
        tools_menu.add_command(label="Copy Sequence to All Sizes...", command=self.copy_sequence_to_all)
        
        self.bind('<Control-n>', lambda e: self.new_font())
        self.bind('<Control-o>', lambda e: self.open_font())
        self.bind('<Control-s>', lambda e: self.save_font())
        self.bind('<Control-z>', lambda e: self.undo())
        self.bind('<Control-y>', lambda e: self.redo())
    
    def create_toolbar(self):
        toolbar = tk.Frame(self, relief=tk.RAISED, bd=2)
        toolbar.pack(side=tk.TOP, fill=tk.X)
        
        tk.Label(toolbar, text="Font Name:").pack(side=tk.LEFT, padx=5)
        self.name_var = tk.StringVar(value="Untitled")
        tk.Entry(toolbar, textvariable=self.name_var, width=20).pack(side=tk.LEFT)
        
        tk.Label(toolbar, text="  Size:").pack(side=tk.LEFT, padx=(20,5))
        self.size_combo = ttk.Combobox(toolbar, width=10, state='readonly')
        self.size_combo.pack(side=tk.LEFT)
        self.size_combo.bind('<<ComboboxSelected>>', self.on_size_changed)
        
        tk.Button(toolbar, text="Preview Text", command=self.show_preview).pack(side=tk.LEFT, padx=10)
    
    def create_main_area(self):
        main = tk.Frame(self)
        main.pack(fill=tk.BOTH, expand=True)
        
        left_panel = tk.Frame(main, width=200)
        left_panel.pack(side=tk.LEFT, fill=tk.Y)
        left_panel.pack_propagate(False)
        
        tk.Label(left_panel, text="Character Set", font=('Arial', 10, 'bold')).pack(pady=5)
        
        self.char_list = tk.Listbox(left_panel, font=('Courier', 10))
        self.char_list.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        self.char_list.bind('<<ListboxSelect>>', self.on_char_selected)
        
        for i in range(256):
            if 32 <= i < 127:
                self.char_list.insert(tk.END, f"{i:3d} '{chr(i)}'")
            else:
                self.char_list.insert(tk.END, f"{i:3d} 0x{i:02X}")
        
        right_panel = tk.Frame(main)
        right_panel.pack(side=tk.RIGHT, fill=tk.BOTH, expand=True)
        
        toolbar = tk.Frame(right_panel)
        toolbar.pack(side=tk.TOP, fill=tk.X, padx=5, pady=5)
        
        self.info_label = tk.Label(toolbar, text="No character selected", font=('Arial', 10))
        self.info_label.pack(side=tk.LEFT)
        
        tk.Frame(toolbar, width=20).pack(side=tk.LEFT)
        
        tk.Button(toolbar, text="Clear", command=self.clear_glyph).pack(side=tk.LEFT, padx=2)
        tk.Button(toolbar, text="Invert", command=self.invert_glyph).pack(side=tk.LEFT, padx=2)
        tk.Button(toolbar, text="Shift ←", command=lambda: self.shift_glyph(-1, 0)).pack(side=tk.LEFT, padx=2)
        tk.Button(toolbar, text="Shift →", command=lambda: self.shift_glyph(1, 0)).pack(side=tk.LEFT, padx=2)
        tk.Button(toolbar, text="Shift ↑", command=lambda: self.shift_glyph(0, -1)).pack(side=tk.LEFT, padx=2)
        tk.Button(toolbar, text="Shift ↓", command=lambda: self.shift_glyph(0, 1)).pack(side=tk.LEFT, padx=2)
        
        tk.Label(toolbar, text="Width:").pack(side=tk.LEFT, padx=(10,2))
        self.width_var = tk.IntVar(value=8)
        self.width_spinbox = tk.Spinbox(toolbar, from_=1, to=64, textvariable=self.width_var, 
                                        width=5, command=self.resize_glyph_width)
        self.width_spinbox.pack(side=tk.LEFT)
        
        self.editor_canvas = tk.Canvas(right_panel, bg='white')
        self.editor_canvas.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        self.editor_canvas.bind('<Button-1>', self.on_canvas_click)
        self.editor_canvas.bind('<B1-Motion>', self.on_canvas_drag)
        self.editor_canvas.bind('<Button-3>', self.on_canvas_right_click)
        self.editor_canvas.bind('<B3-Motion>', self.on_canvas_right_drag)
        self.editor_canvas.bind('<Configure>', self.on_canvas_resize)
        
        self.current_glyph = None
        self.pixel_size = 20
    
    def create_statusbar(self):
        self.statusbar = tk.Label(self, text="Ready", bd=1, relief=tk.SUNKEN, anchor=tk.W)
        self.statusbar.pack(side=tk.BOTTOM, fill=tk.X)
    
    def new_font(self):
        self.font = BMFFont()
        self.font.name = "Untitled"
        self.current_seq_idx = 0
        self.current_file = None
        self.name_var.set("Untitled")
        self.update_size_combo()
        self.statusbar.config(text="New font created - add a size to begin")
    
    def open_font(self):
        path = filedialog.askopenfilename(
            title="Open BMF Font",
            filetypes=[("BMF Files", "*.bmf"), ("All Files", "*.*")]
        )
        if path:
            try:
                self.font.load(path)
                self.current_file = path
                self.current_seq_idx = 0
                self.name_var.set(self.font.name)
                self.update_size_combo()
                self.statusbar.config(text=f"Opened: {os.path.basename(path)}")
            except Exception as e:
                messagebox.showerror("Error", f"Failed to open file:\n{e}")
    
    def save_font(self):
        if self.current_file:
            self.save_to_file(self.current_file)
        else:
            self.save_font_as()
    
    def save_font_as(self):
        path = filedialog.asksaveasfilename(
            title="Save BMF Font",
            defaultextension=".bmf",
            filetypes=[("BMF Files", "*.bmf"), ("All Files", "*.*")]
        )
        if path:
            self.save_to_file(path)
    
    def save_to_file(self, path):
        try:
            self.font.name = self.name_var.get()
            self.font.save(path)
            self.current_file = path
            self.statusbar.config(text=f"Saved: {os.path.basename(path)}")
        except Exception as e:
            messagebox.showerror("Error", f"Failed to save file:\n{e}")
    
    def add_sequence(self):
        dialog = tk.Toplevel(self)
        dialog.title("Add Size")
        dialog.geometry("300x150")
        
        tk.Label(dialog, text="Font Height (pixels):").pack(pady=5)
        height_var = tk.IntVar(value=16)
        tk.Spinbox(dialog, from_=6, to=64, textvariable=height_var, width=10).pack()
        
        tk.Label(dialog, text="Baseline (pixels):").pack(pady=5)
        baseline_var = tk.IntVar(value=12)
        tk.Spinbox(dialog, from_=1, to=64, textvariable=baseline_var, width=10).pack()
        
        def ok():
            height = height_var.get()
            baseline = baseline_var.get()
            self.font.add_sequence(height, baseline, height)
            self.update_size_combo()
            dialog.destroy()
        
        tk.Button(dialog, text="OK", command=ok).pack(pady=10)
    
    def remove_sequence(self):
        if len(self.font.sequences) <= 1:
            messagebox.showwarning("Warning", "Cannot remove the last sequence")
            return
        
        if messagebox.askyesno("Confirm", "Remove current size?"):
            del self.font.sequences[self.current_seq_idx]
            self.current_seq_idx = 0
            self.update_size_combo()
    
    def update_size_combo(self):
        sizes = [f"{seq['point_size']}pt ({seq['height']}px)" 
                for seq in self.font.sequences]
        self.size_combo['values'] = sizes
        if sizes:
            if self.current_seq_idx >= len(sizes):
                self.current_seq_idx = 0
            self.size_combo.current(self.current_seq_idx)
        else:
            self.size_combo.set("No sizes - add one")
    
    def on_size_changed(self, event):
        self.current_seq_idx = self.size_combo.current()
        self.on_char_selected(None)
    
    def on_char_selected(self, event):
        selection = self.char_list.curselection()
        if not selection or not self.font.sequences:
            return
        
        char_idx = selection[0]
        seq = self.font.sequences[self.current_seq_idx]
        self.current_glyph = seq['glyphs'][char_idx]
        
        if 32 <= char_idx < 127:
            char_str = chr(char_idx)
        else:
            char_str = f"0x{char_idx:02X}"
        
        self.info_label.config(
            text=f"Character: {char_str}  Width: {self.current_glyph['width']}px  Height: {seq['height']}px"
        )
        
        self.width_var.set(self.current_glyph['width'])
        self.draw_editor()
    
    def draw_editor(self):
        self.editor_canvas.delete('all')
        
        if not self.current_glyph or not self.font.sequences:
            return
        
        seq = self.font.sequences[self.current_seq_idx]
        bitmap = self.current_glyph['bitmap']
        
        width = self.current_glyph['width']
        height = len(bitmap)
        
        canvas_width = self.editor_canvas.winfo_width()
        canvas_height = self.editor_canvas.winfo_height()
        
        if canvas_width < 10:
            canvas_width = 800
        if canvas_height < 10:
            canvas_height = 600
        
        self.pixel_size = min(canvas_width // (width + 2), canvas_height // (height + 2), 30)
        self.pixel_size = max(self.pixel_size, 5)
        
        offset_x = (canvas_width - width * self.pixel_size) // 2
        offset_y = (canvas_height - height * self.pixel_size) // 2
        
        for y in range(height):
            for x in range(width):
                x1 = offset_x + x * self.pixel_size
                y1 = offset_y + y * self.pixel_size
                x2 = x1 + self.pixel_size
                y2 = y1 + self.pixel_size
                
                color = 'black' if bitmap[y][x] else 'white'
                self.editor_canvas.create_rectangle(x1, y1, x2, y2, 
                                                    fill=color, outline='gray')
        
        baseline_y = offset_y + seq['baseline'] * self.pixel_size
        self.editor_canvas.create_line(offset_x - 10, baseline_y,
                                       offset_x + width * self.pixel_size + 10, baseline_y,
                                       fill='red', dash=(5, 5), width=2)
    
    def get_pixel_coords(self, event):
        if not self.current_glyph:
            return None, None
        
        width = self.current_glyph['width']
        height = len(self.current_glyph['bitmap'])
        
        canvas_width = self.editor_canvas.winfo_width()
        canvas_height = self.editor_canvas.winfo_height()
        
        offset_x = (canvas_width - width * self.pixel_size) // 2
        offset_y = (canvas_height - height * self.pixel_size) // 2
        
        x = (event.x - offset_x) // self.pixel_size
        y = (event.y - offset_y) // self.pixel_size
        
        if 0 <= x < width and 0 <= y < height:
            return x, y
        return None, None
    
    def on_canvas_click(self, event):
        x, y = self.get_pixel_coords(event)
        if x is not None:
            self.current_glyph['bitmap'][y][x] = 1
            self.draw_editor()
    
    def on_canvas_drag(self, event):
        x, y = self.get_pixel_coords(event)
        if x is not None:
            self.current_glyph['bitmap'][y][x] = 1
            self.draw_editor()
    
    def on_canvas_right_click(self, event):
        x, y = self.get_pixel_coords(event)
        if x is not None:
            self.current_glyph['bitmap'][y][x] = 0
            self.draw_editor()
    
    def on_canvas_right_drag(self, event):
        x, y = self.get_pixel_coords(event)
        if x is not None:
            self.current_glyph['bitmap'][y][x] = 0
            self.draw_editor()
    
    def on_canvas_resize(self, event):
        self.draw_editor()
    
    def clear_glyph(self):
        if not self.current_glyph:
            return
        self.save_state()
        for row in self.current_glyph['bitmap']:
            for i in range(len(row)):
                row[i] = 0
        self.draw_editor()
    
    def invert_glyph(self):
        if not self.current_glyph:
            return
        self.save_state()
        for row in self.current_glyph['bitmap']:
            for i in range(len(row)):
                row[i] = 1 - row[i]
        self.draw_editor()
    
    def shift_glyph(self, dx, dy):
        if not self.current_glyph:
            return
        self.save_state()
        
        bitmap = self.current_glyph['bitmap']
        height = len(bitmap)
        width = len(bitmap[0]) if bitmap else 0
        
        new_bitmap = [[0] * width for _ in range(height)]
        
        for y in range(height):
            for x in range(width):
                new_y = (y + dy) % height
                new_x = (x + dx) % width
                new_bitmap[new_y][new_x] = bitmap[y][x]
        
        self.current_glyph['bitmap'] = new_bitmap
        self.draw_editor()
    
    def resize_glyph_width(self):
        if not self.current_glyph:
            return
        
        new_width = self.width_var.get()
        old_width = self.current_glyph['width']
        
        if new_width == old_width:
            return
        
        self.save_state()
        bitmap = self.current_glyph['bitmap']
        
        if new_width > old_width:
            for row in bitmap:
                row.extend([0] * (new_width - old_width))
        elif new_width < old_width:
            for i in range(len(bitmap)):
                bitmap[i] = bitmap[i][:new_width]
        
        self.current_glyph['width'] = new_width
        
        seq = self.font.sequences[self.current_seq_idx]
        selection = self.char_list.curselection()
        if selection:
            char_idx = selection[0]
            if 32 <= char_idx < 127:
                char_str = chr(char_idx)
            else:
                char_str = f"0x{char_idx:02X}"
            
            self.info_label.config(
                text=f"Character: {char_str}  Width: {self.current_glyph['width']}px  Height: {seq['height']}px"
            )
        
        self.draw_editor()
    
    def import_ttf(self):
        path = filedialog.askopenfilename(
            title="Import TTF Font",
            filetypes=[("TrueType Fonts", "*.ttf"), ("OpenType Fonts", "*.otf"), 
                      ("All Files", "*.*")]
        )
        if not path:
            return
        
        dialog = tk.Toplevel(self)
        dialog.title("TTF Import Settings")
        dialog.geometry("400x250")
        
        tk.Label(dialog, text="Font Size (pixels):").pack(pady=5)
        size_var = tk.IntVar(value=16)
        tk.Spinbox(dialog, from_=6, to=64, textvariable=size_var, width=10).pack()
        
        tk.Label(dialog, text="Rendering Method:").pack(pady=5)
        method_var = tk.StringVar(value="1bit")
        tk.Radiobutton(dialog, text="1-bit (no antialiasing)", 
                      variable=method_var, value="1bit").pack()
        tk.Radiobutton(dialog, text="Threshold antialiasing", 
                      variable=method_var, value="threshold").pack()
        
        tk.Label(dialog, text="Threshold (0-255):").pack(pady=5)
        threshold_var = tk.IntVar(value=128)
        tk.Spinbox(dialog, from_=0, to=255, textvariable=threshold_var, width=10).pack()
        
        def do_import():
            size = size_var.get()
            method = method_var.get()
            threshold = threshold_var.get()
            
            try:
                self.import_ttf_rasterized(path, size, method, threshold)
                dialog.destroy()
                self.statusbar.config(text=f"Imported TTF: {os.path.basename(path)}")
            except Exception as e:
                messagebox.showerror("Error", f"Failed to import:\n{e}")
        
        tk.Button(dialog, text="Import", command=do_import, 
                 bg='green', fg='white').pack(pady=10)
    
    def import_ttf_oslet(self):
        path = filedialog.askopenfilename(
            title="Import TTF Font (osLET Standard Sizes)",
            filetypes=[("TrueType Fonts", "*.ttf"), ("OpenType Fonts", "*.otf"), 
                      ("All Files", "*.*")]
        )
        if not path:
            return
        
        dialog = tk.Toplevel(self)
        dialog.title("osLET Standard Import")
        dialog.geometry("400x300")
        
        tk.Label(dialog, text="Importing sizes: 10, 12, 14, 16, 18, 24, 32 pt", 
                font=('Arial', 10, 'bold')).pack(pady=10)
        
        tk.Label(dialog, text="Rendering Method:").pack(pady=5)
        method_var = tk.StringVar(value="1bit")
        tk.Radiobutton(dialog, text="1-bit (no antialiasing)", 
                      variable=method_var, value="1bit").pack()
        tk.Radiobutton(dialog, text="Threshold antialiasing", 
                      variable=method_var, value="threshold").pack()
        
        tk.Label(dialog, text="Threshold (0-255):").pack(pady=5)
        threshold_var = tk.IntVar(value=128)
        tk.Spinbox(dialog, from_=0, to=255, textvariable=threshold_var, width=10).pack()
        
        auto_fix_var = tk.BooleanVar(value=False)
        tk.Checkbutton(dialog, text="Auto-fix space width after import", 
                      variable=auto_fix_var).pack(pady=5)
        
        progress_label = tk.Label(dialog, text="")
        progress_label.pack(pady=5)
        
        def do_import():
            method = method_var.get()
            threshold = threshold_var.get()
            auto_fix = auto_fix_var.get()
            
            sizes = [10, 12, 14, 16, 18, 24, 32]
            
            try:
                for i, size in enumerate(sizes):
                    progress_label.config(text=f"Importing {size}pt... ({i+1}/{len(sizes)})")
                    dialog.update()
                    
                    self.import_ttf_rasterized(path, size, method, threshold)
                
                if auto_fix:
                    progress_label.config(text="Fixing space width...")
                    dialog.update()
                    for seq in self.font.sequences:
                        glyph = seq['glyphs'][32]
                        if glyph['width'] < 4:
                            glyph['width'] = 4
                            glyph['bitmap'] = [[0] * 4 for _ in range(len(glyph['bitmap']))]
                
                dialog.destroy()
                self.statusbar.config(text=f"Imported {len(sizes)} sizes from {os.path.basename(path)}")
                messagebox.showinfo("Success", f"Imported {len(sizes)} font sizes successfully!")
            except Exception as e:
                messagebox.showerror("Error", f"Failed to import:\n{e}")
        
        tk.Button(dialog, text="Import All", command=do_import, 
                 bg='green', fg='white', padx=20).pack(pady=10)
    
    def import_ttf_rasterized(self, ttf_path, size, method, threshold):
        font = ImageFont.truetype(ttf_path, size)
        
        test_img = Image.new('L', (200, 200), 0)
        test_draw = ImageDraw.Draw(test_img)
        
        try:
            ascent, descent = font.getmetrics()
            height = ascent + descent
            baseline = ascent
        except:
            bbox = test_draw.textbbox((0, 0), 'Agyjpq|', font=font)
            height = bbox[3] - bbox[1]
            baseline = int(height * 0.75)
        
        height = max(height, size)
        baseline = min(baseline, height - 2)
        
        seq = {
            'height': height,
            'baseline': baseline,
            'point_size': size,
            'glyphs': {}
        }
        
        for ascii_code in range(256):
            char = chr(ascii_code)
            
            bbox = test_draw.textbbox((0, 0), char, font=font)
            left = bbox[0]
            right = bbox[2]
            
            char_width = max(1, right - left)
            
            if ascii_code == 32:
                char_width = max(char_width, size // 3)
            
            # Dodaj padding jeśli left jest dodatni (znak zaczyna się z offsetem)
            padding_left = max(0, left) + 1
            img_width = char_width + padding_left
            
            if method == '1bit':
                img = Image.new('1', (img_width, height), 0)
                draw = ImageDraw.Draw(img)
                draw.text((padding_left - left, 0), char, font=font, fill=1)
            else:
                img = Image.new('L', (img_width, height), 0)
                draw = ImageDraw.Draw(img)
                draw.text((padding_left - left, 0), char, font=font, fill=255)
            
            bitmap = []
            for y in range(height):
                row = []
                for x in range(img_width):
                    pixel = img.getpixel((x, y))
                    if method == '1bit':
                        row.append(1 if pixel else 0)
                    else:
                        row.append(1 if pixel >= threshold else 0)
                bitmap.append(row)
            
            seq['glyphs'][ascii_code] = {
                'ascii': ascii_code,
                'width': img_width,
                'bitmap': bitmap
            }
        
        self.font.sequences.append(seq)
        self.current_seq_idx = len(self.font.sequences) - 1
        self.update_size_combo()
    
    def import_png(self):
        path = filedialog.askopenfilename(
            title="Import PNG Strip",
            filetypes=[("PNG Images", "*.png"), ("All Files", "*.*")]
        )
        if not path:
            return
        
        dialog = tk.Toplevel(self)
        dialog.title("PNG Import Settings")
        dialog.geometry("300x200")
        
        tk.Label(dialog, text="Glyph Width (pixels):").pack(pady=5)
        width_var = tk.IntVar(value=8)
        tk.Spinbox(dialog, from_=1, to=32, textvariable=width_var, width=10).pack()
        
        tk.Label(dialog, text="Glyph Height (pixels):").pack(pady=5)
        height_var = tk.IntVar(value=16)
        tk.Spinbox(dialog, from_=1, to=64, textvariable=height_var, width=10).pack()
        
        tk.Label(dialog, text="Layout: 16x16 grid (256 chars)").pack(pady=10)
        
        def do_import():
            width = width_var.get()
            height = height_var.get()
            
            try:
                self.import_png_strip(path, width, height)
                dialog.destroy()
                self.statusbar.config(text=f"Imported PNG: {os.path.basename(path)}")
            except Exception as e:
                messagebox.showerror("Error", f"Failed to import:\n{e}")
        
        tk.Button(dialog, text="Import", command=do_import,
                 bg='green', fg='white').pack(pady=10)
    
    def import_png_strip(self, png_path, glyph_width, glyph_height):
        img = Image.open(png_path).convert('1')
        
        baseline = glyph_height - 2
        
        seq = {
            'height': glyph_height,
            'baseline': baseline,
            'point_size': glyph_height,
            'glyphs': {}
        }
        
        for ascii_code in range(256):
            x_offset = (ascii_code % 16) * glyph_width
            y_offset = (ascii_code // 16) * glyph_height
            
            bitmap = []
            for y in range(glyph_height):
                row = []
                for x in range(glyph_width):
                    pixel = img.getpixel((x_offset + x, y_offset + y))
                    row.append(1 if pixel else 0)
                bitmap.append(row)
            
            seq['glyphs'][ascii_code] = {
                'ascii': ascii_code,
                'width': glyph_width,
                'bitmap': bitmap
            }
        
        self.font.sequences.append(seq)
        self.current_seq_idx = len(self.font.sequences) - 1
        self.update_size_combo()
    
    def show_preview(self):
        if not self.font.sequences:
            messagebox.showinfo("Info", "No font loaded")
            return
        
        preview_win = tk.Toplevel(self)
        preview_win.title("Text Preview")
        preview_win.geometry("800x600")
        
        text_frame = tk.Frame(preview_win)
        text_frame.pack(side=tk.TOP, fill=tk.X, padx=5, pady=5)
        
        tk.Label(text_frame, text="Enter text:").pack(side=tk.LEFT)
        text_var = tk.StringVar(value="The quick brown fox jumps over the lazy dog")
        text_entry = tk.Entry(text_frame, textvariable=text_var, width=50)
        text_entry.pack(side=tk.LEFT, padx=5)
        
        canvas = tk.Canvas(preview_win, bg='white')
        canvas.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        def update_preview():
            canvas.delete('all')
            text = text_var.get()
            seq = self.font.sequences[self.current_seq_idx]
            
            scale = 2
            x = 10
            y = 10
            
            for char in text:
                ascii_code = ord(char)
                if ascii_code not in seq['glyphs']:
                    continue
                
                glyph = seq['glyphs'][ascii_code]
                
                for gy in range(len(glyph['bitmap'])):
                    for gx in range(len(glyph['bitmap'][gy])):
                        if glyph['bitmap'][gy][gx]:
                            canvas.create_rectangle(
                                x + gx * scale, y + gy * scale,
                                x + gx * scale + scale, y + gy * scale + scale,
                                fill='black', outline='')
                
                x += glyph['width'] * scale
                
                if x > 700:
                    x = 10
                    y += seq['height'] * scale + 5
        
        tk.Button(text_frame, text="Refresh", command=update_preview).pack(side=tk.LEFT)
        update_preview()
    
    def auto_fix_all(self):
        if not self.font.sequences:
            messagebox.showinfo("Info", "No font loaded")
            return
        
        dialog = tk.Toplevel(self)
        dialog.title("Auto-fix Glyphs")
        dialog.geometry("400x250")
        
        tk.Label(dialog, text="Basic cleanup for imported fonts", 
                font=('Arial', 10, 'bold')).pack(pady=10)
        
        min_width_var = tk.BooleanVar(value=True)
        tk.Checkbutton(dialog, text="Enforce minimum width for space", 
                      variable=min_width_var).pack(anchor=tk.W, padx=20)
        
        tk.Label(dialog, text="Space character min width:").pack(pady=5)
        space_width_var = tk.IntVar(value=4)
        tk.Spinbox(dialog, from_=2, to=16, textvariable=space_width_var, width=10).pack()
        
        tk.Label(dialog, text="\nNote: Trimming operations removed - they damage\n"
                              "baseline alignment and glyph spacing.",
                justify=tk.LEFT, fg='red').pack(pady=10)
        
        progress_label = tk.Label(dialog, text="")
        progress_label.pack(pady=10)
        
        def do_fix():
            seq = self.font.sequences[self.current_seq_idx]
            fixed_count = 0
            
            if min_width_var.get():
                glyph = seq['glyphs'][32]
                if glyph['width'] < space_width_var.get():
                    glyph['width'] = space_width_var.get()
                    glyph['bitmap'] = [[0] * glyph['width'] for _ in range(len(glyph['bitmap']))]
                    fixed_count = 1
            
            dialog.destroy()
            self.statusbar.config(text=f"Fixed {fixed_count} glyphs")
            if fixed_count > 0:
                messagebox.showinfo("Done", f"Fixed space character width")
            self.on_char_selected(None)
        
        tk.Button(dialog, text="Apply", command=do_fix, 
                 bg='green', fg='white', padx=20).pack(pady=10)
    
    def auto_fix_all_silent(self):
        for seq_idx in range(len(self.font.sequences)):
            seq = self.font.sequences[seq_idx]
            glyph = seq['glyphs'][32]
            if glyph['width'] < 4:
                glyph['width'] = 4
                glyph['bitmap'] = [[0] * 4 for _ in range(len(glyph['bitmap']))]
    
    def trim_vertical(self, glyph):
        bitmap = glyph['bitmap']
        if not bitmap:
            return False
        
        modified = False
        
        while len(bitmap) > 1:
            if all(pixel == 0 for pixel in bitmap[0]):
                bitmap.pop(0)
                modified = True
            else:
                break
        
        while len(bitmap) > 1:
            if all(pixel == 0 for pixel in bitmap[-1]):
                bitmap.pop()
                modified = True
            else:
                break
        
        glyph['bitmap'] = bitmap
        return modified
    
    def trim_horizontal(self, glyph):
        bitmap = glyph['bitmap']
        if not bitmap or not bitmap[0]:
            return False
        
        width = len(bitmap[0])
        height = len(bitmap)
        
        left_trim = 0
        for x in range(width):
            if all(bitmap[y][x] == 0 for y in range(height)):
                left_trim += 1
            else:
                break
        
        right_trim = 0
        for x in range(width - 1, -1, -1):
            if all(bitmap[y][x] == 0 for y in range(height)):
                right_trim += 1
            else:
                break
        
        if left_trim > 0 or right_trim > 0:
            new_width = width - left_trim - right_trim
            if new_width < 1:
                new_width = 1
                left_trim = width - 1
                right_trim = 0
            
            new_bitmap = []
            for row in bitmap:
                new_row = row[left_trim:left_trim + new_width]
                new_bitmap.append(new_row)
            
            glyph['bitmap'] = new_bitmap
            glyph['width'] = new_width
            return True
        
        return False
    
    def center_glyph(self, glyph, target_height):
        bitmap = glyph['bitmap']
        if not bitmap:
            return False
        
        current_height = len(bitmap)
        if current_height >= target_height:
            return False
        
        pad_top = (target_height - current_height) // 2
        pad_bottom = target_height - current_height - pad_top
        
        width = glyph['width']
        new_bitmap = [[0] * width for _ in range(pad_top)]
        new_bitmap.extend(bitmap)
        new_bitmap.extend([[0] * width for _ in range(pad_bottom)])
        
        glyph['bitmap'] = new_bitmap
        return True
    
    def batch_operations(self):
        if not self.font.sequences:
            messagebox.showinfo("Info", "No font loaded")
            return
        
        dialog = tk.Toplevel(self)
        dialog.title("Batch Operations")
        dialog.geometry("500x450")
        
        tk.Label(dialog, text="Batch operations on current sequence", 
                font=('Arial', 10, 'bold')).pack(pady=10)
        
        notebook = ttk.Notebook(dialog)
        notebook.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)
        
        # Tab 1: Width operations
        width_frame = tk.Frame(notebook)
        notebook.add(width_frame, text="Width")
        
        tk.Label(width_frame, text="Character range:").pack(pady=5)
        range_frame = tk.Frame(width_frame)
        range_frame.pack()
        tk.Label(range_frame, text="From:").pack(side=tk.LEFT)
        from_var = tk.IntVar(value=33)
        tk.Spinbox(range_frame, from_=0, to=255, textvariable=from_var, width=5).pack(side=tk.LEFT, padx=2)
        tk.Label(range_frame, text="To:").pack(side=tk.LEFT, padx=(10,0))
        to_var = tk.IntVar(value=126)
        tk.Spinbox(range_frame, from_=0, to=255, textvariable=to_var, width=5).pack(side=tk.LEFT, padx=2)
        
        tk.Label(width_frame, text="\nWidth operations:").pack()
        
        def set_fixed_width():
            w = tk.simpledialog.askinteger("Fixed Width", "Enter width (pixels):", 
                                          initialvalue=8, minvalue=1, maxvalue=32)
            if w:
                self.apply_fixed_width(from_var.get(), to_var.get(), w)
        
        def enforce_min():
            w = tk.simpledialog.askinteger("Minimum Width", "Enter min width (pixels):", 
                                          initialvalue=3, minvalue=1, maxvalue=32)
            if w:
                self.apply_min_width(from_var.get(), to_var.get(), w)
        
        def enforce_max():
            w = tk.simpledialog.askinteger("Maximum Width", "Enter max width (pixels):", 
                                          initialvalue=16, minvalue=1, maxvalue=32)
            if w:
                self.apply_max_width(from_var.get(), to_var.get(), w)
        
        def add_margins():
            left = tk.simpledialog.askinteger("Left Margin", "Left margin (pixels):", 
                                             initialvalue=1, minvalue=0, maxvalue=8)
            if left is not None:
                right = tk.simpledialog.askinteger("Right Margin", "Right margin (pixels):", 
                                                  initialvalue=1, minvalue=0, maxvalue=8)
                if right is not None:
                    self.apply_margins(from_var.get(), to_var.get(), left, right)
        
        tk.Button(width_frame, text="Set Fixed Width", command=set_fixed_width).pack(pady=5)
        tk.Button(width_frame, text="Enforce Minimum Width", command=enforce_min).pack(pady=5)
        tk.Button(width_frame, text="Enforce Maximum Width", command=enforce_max).pack(pady=5)
        tk.Button(width_frame, text="Add Margins (padding)", command=add_margins).pack(pady=5)
        tk.Button(width_frame, text="Optimize (Trim Sides)", 
                 command=lambda: self.trim_range(from_var.get(), to_var.get())).pack(pady=5)
        
        # Tab 2: Transform operations
        transform_frame = tk.Frame(notebook)
        notebook.add(transform_frame, text="Transform")
        
        tk.Label(transform_frame, text="Character range:").pack(pady=5)
        t_range_frame = tk.Frame(transform_frame)
        t_range_frame.pack()
        tk.Label(t_range_frame, text="From:").pack(side=tk.LEFT)
        t_from_var = tk.IntVar(value=0)
        tk.Spinbox(t_range_frame, from_=0, to=255, textvariable=t_from_var, width=5).pack(side=tk.LEFT, padx=2)
        tk.Label(t_range_frame, text="To:").pack(side=tk.LEFT, padx=(10,0))
        t_to_var = tk.IntVar(value=255)
        tk.Spinbox(t_range_frame, from_=0, to=255, textvariable=t_to_var, width=5).pack(side=tk.LEFT, padx=2)
        
        tk.Label(transform_frame, text="\nTransform operations:").pack(pady=10)
        
        def bold_range():
            self.apply_bold(t_from_var.get(), t_to_var.get())
        
        def italic_range():
            slant = tk.simpledialog.askinteger("Italic Slant", 
                                              "Slant amount (pixels, 1-4 recommended):",
                                              initialvalue=2, minvalue=1, maxvalue=8)
            if slant:
                self.apply_italic(t_from_var.get(), t_to_var.get(), slant)
        
        def invert_range():
            self.apply_invert(t_from_var.get(), t_to_var.get())
        
        def shift_all():
            dx = tk.simpledialog.askinteger("Shift X", "Horizontal shift (pixels, negative = left):", 
                                           initialvalue=0, minvalue=-32, maxvalue=32)
            if dx is not None:
                dy = tk.simpledialog.askinteger("Shift Y", "Vertical shift (pixels, negative = up):", 
                                               initialvalue=0, minvalue=-32, maxvalue=32)
                if dy is not None:
                    self.apply_shift(t_from_var.get(), t_to_var.get(), dx, dy)
        
        tk.Button(transform_frame, text="Make Bold (add right pixel)", command=bold_range).pack(pady=5)
        tk.Button(transform_frame, text="Make Italic (slant)", command=italic_range).pack(pady=5)
        tk.Button(transform_frame, text="Invert Colors", command=invert_range).pack(pady=5)
        tk.Button(transform_frame, text="Shift All...", command=shift_all).pack(pady=5)
    
    def apply_fixed_width(self, start, end, width):
        self.save_state()
        seq = self.font.sequences[self.current_seq_idx]
        for i in range(start, end + 1):
            if i in seq['glyphs']:
                glyph = seq['glyphs'][i]
                old_width = glyph['width']
                if old_width != width:
                    for row in glyph['bitmap']:
                        if len(row) < width:
                            row.extend([0] * (width - len(row)))
                        elif len(row) > width:
                            del row[width:]
                    glyph['width'] = width
        self.statusbar.config(text=f"Set fixed width {width}px for chars {start}-{end}")
        self.on_char_selected(None)
    
    def apply_min_width(self, start, end, min_width):
        self.save_state()
        seq = self.font.sequences[self.current_seq_idx]
        modified = 0
        for i in range(start, end + 1):
            if i in seq['glyphs']:
                glyph = seq['glyphs'][i]
                if glyph['width'] < min_width:
                    for row in glyph['bitmap']:
                        row.extend([0] * (min_width - len(row)))
                    glyph['width'] = min_width
                    modified += 1
        self.statusbar.config(text=f"Enforced min width on {modified} glyphs")
        self.on_char_selected(None)
    
    def apply_max_width(self, start, end, max_width):
        self.save_state()
        seq = self.font.sequences[self.current_seq_idx]
        modified = 0
        for i in range(start, end + 1):
            if i in seq['glyphs']:
                glyph = seq['glyphs'][i]
                if glyph['width'] > max_width:
                    for row in glyph['bitmap']:
                        del row[max_width:]
                    glyph['width'] = max_width
                    modified += 1
        self.statusbar.config(text=f"Enforced max width on {modified} glyphs")
        self.on_char_selected(None)
    
    def trim_range(self, start, end):
        self.save_state()
        seq = self.font.sequences[self.current_seq_idx]
        modified = 0
        for i in range(start, end + 1):
            if i in seq['glyphs']:
                if self.trim_horizontal(seq['glyphs'][i]):
                    modified += 1
        self.statusbar.config(text=f"Trimmed {modified} glyphs")
        self.on_char_selected(None)
    
    def apply_margins(self, start, end, left_margin, right_margin):
        self.save_state()
        seq = self.font.sequences[self.current_seq_idx]
        modified = 0
        for i in range(start, end + 1):
            if i in seq['glyphs']:
                glyph = seq['glyphs'][i]
                
                # Dodaj marginesy
                new_bitmap = []
                for row in glyph['bitmap']:
                    new_row = [0] * left_margin + row + [0] * right_margin
                    new_bitmap.append(new_row)
                
                glyph['bitmap'] = new_bitmap
                glyph['width'] = len(new_bitmap[0]) if new_bitmap else 0
                modified += 1
        
        self.statusbar.config(text=f"Added margins to {modified} glyphs")
        self.on_char_selected(None)
    
    def save_state(self):
        if not self.font.sequences:
            return
        
        state = []
        for seq in self.font.sequences:
            seq_copy = {
                'height': seq['height'],
                'baseline': seq['baseline'],
                'point_size': seq['point_size'],
                'glyphs': {}
            }
            for i in range(256):
                glyph = seq['glyphs'][i]
                seq_copy['glyphs'][i] = {
                    'ascii': glyph['ascii'],
                    'width': glyph['width'],
                    'bitmap': [row[:] for row in glyph['bitmap']]
                }
            state.append(seq_copy)
        
        self.undo_stack.append(state)
        self.redo_stack.clear()
        
        if len(self.undo_stack) > 50:
            self.undo_stack.pop(0)
    
    def undo(self):
        if not self.undo_stack:
            self.statusbar.config(text="Nothing to undo")
            return
        
        current_state = []
        for seq in self.font.sequences:
            seq_copy = {
                'height': seq['height'],
                'baseline': seq['baseline'],
                'point_size': seq['point_size'],
                'glyphs': {}
            }
            for i in range(256):
                glyph = seq['glyphs'][i]
                seq_copy['glyphs'][i] = {
                    'ascii': glyph['ascii'],
                    'width': glyph['width'],
                    'bitmap': [row[:] for row in glyph['bitmap']]
                }
            current_state.append(seq_copy)
        
        self.redo_stack.append(current_state)
        
        self.font.sequences = self.undo_stack.pop()
        
        self.statusbar.config(text="Undo")
        self.on_char_selected(None)
    
    def redo(self):
        if not self.redo_stack:
            self.statusbar.config(text="Nothing to redo")
            return
        
        current_state = []
        for seq in self.font.sequences:
            seq_copy = {
                'height': seq['height'],
                'baseline': seq['baseline'],
                'point_size': seq['point_size'],
                'glyphs': {}
            }
            for i in range(256):
                glyph = seq['glyphs'][i]
                seq_copy['glyphs'][i] = {
                    'ascii': glyph['ascii'],
                    'width': glyph['width'],
                    'bitmap': [row[:] for row in glyph['bitmap']]
                }
            current_state.append(seq_copy)
        
        self.undo_stack.append(current_state)
        
        self.font.sequences = self.redo_stack.pop()
        
        self.statusbar.config(text="Redo")
        self.on_char_selected(None)
    
    def apply_bold(self, start, end):
        self.save_state()
        seq = self.font.sequences[self.current_seq_idx]
        for i in range(start, end + 1):
            if i in seq['glyphs']:
                glyph = seq['glyphs'][i]
                bitmap = glyph['bitmap']
                for y in range(len(bitmap)):
                    row = bitmap[y]
                    new_row = row[:]
                    for x in range(len(row) - 1):
                        if row[x] == 1:
                            new_row[x + 1] = 1
                    new_row.append(0)
                    bitmap[y] = new_row
                glyph['width'] += 1
        self.statusbar.config(text=f"Applied bold to chars {start}-{end}")
        self.on_char_selected(None)
    
    def apply_italic(self, start, end, slant):
        self.save_state()
        seq = self.font.sequences[self.current_seq_idx]
        height = seq['height']
        
        for i in range(start, end + 1):
            if i in seq['glyphs']:
                glyph = seq['glyphs'][i]
                bitmap = glyph['bitmap']
                old_width = glyph['width']
                
                new_width = old_width + slant
                new_bitmap = [[0] * new_width for _ in range(height)]
                
                for y in range(height):
                    offset = (height - 1 - y) * slant // height
                    for x in range(old_width):
                        if x < len(bitmap[y]) and bitmap[y][x]:
                            new_x = x + offset
                            if 0 <= new_x < new_width:
                                new_bitmap[y][new_x] = 1
                
                glyph['bitmap'] = new_bitmap
                glyph['width'] = new_width
        
        self.statusbar.config(text=f"Applied italic (slant {slant}) to chars {start}-{end}")
        self.on_char_selected(None)
    
    def apply_invert(self, start, end):
        self.save_state()
        seq = self.font.sequences[self.current_seq_idx]
        for i in range(start, end + 1):
            if i in seq['glyphs']:
                glyph = seq['glyphs'][i]
                for row in glyph['bitmap']:
                    for x in range(len(row)):
                        row[x] = 1 - row[x]
        self.statusbar.config(text=f"Inverted chars {start}-{end}")
        self.on_char_selected(None)
    
    def apply_shift(self, start, end, dx, dy):
        self.save_state()
        seq = self.font.sequences[self.current_seq_idx]
        for i in range(start, end + 1):
            if i in seq['glyphs']:
                glyph = seq['glyphs'][i]
                bitmap = glyph['bitmap']
                height = len(bitmap)
                width = glyph['width']
                
                new_bitmap = [[0] * width for _ in range(height)]
                
                for y in range(height):
                    for x in range(width):
                        new_y = (y + dy) % height
                        new_x = (x + dx) % width
                        new_bitmap[new_y][new_x] = bitmap[y][x]
                
                glyph['bitmap'] = new_bitmap
        self.statusbar.config(text=f"Shifted chars {start}-{end} by ({dx},{dy})")
        self.on_char_selected(None)
    
    def clone_glyph(self):
        selection = self.char_list.curselection()
        if not selection:
            messagebox.showinfo("Info", "Please select source glyph first")
            return
        
        source_idx = selection[0]
        
        target = tk.simpledialog.askinteger("Clone Glyph", 
                                           f"Clone glyph {source_idx} to which character (0-255)?",
                                           initialvalue=source_idx + 1, minvalue=0, maxvalue=255)
        if target is not None:
            seq = self.font.sequences[self.current_seq_idx]
            source_glyph = seq['glyphs'][source_idx]
            
            seq['glyphs'][target] = {
                'ascii': target,
                'width': source_glyph['width'],
                'bitmap': [row[:] for row in source_glyph['bitmap']]
            }
            
            self.statusbar.config(text=f"Cloned glyph {source_idx} → {target}")
            messagebox.showinfo("Done", f"Cloned glyph {source_idx} to {target}")
    
    def copy_sequence_to_all(self):
        if len(self.font.sequences) < 2:
            messagebox.showinfo("Info", "Need at least 2 sequences to copy")
            return
        
        if not messagebox.askyesno("Confirm", 
                                   f"Copy current sequence ({self.font.sequences[self.current_seq_idx]['point_size']}pt) to ALL other sequences?\n\n"
                                   "This will OVERWRITE all glyphs in other sequences!"):
            return
        
        source_seq = self.font.sequences[self.current_seq_idx]
        copied = 0
        
        for i, seq in enumerate(self.font.sequences):
            if i == self.current_seq_idx:
                continue
            
            for ascii_code in range(256):
                source_glyph = source_seq['glyphs'][ascii_code]
                seq['glyphs'][ascii_code] = {
                    'ascii': ascii_code,
                    'width': source_glyph['width'],
                    'bitmap': [row[:] for row in source_glyph['bitmap']]
                }
            copied += 1
        
        self.statusbar.config(text=f"Copied to {copied} sequences")
        messagebox.showinfo("Done", f"Copied glyphs to {copied} sequences")
    
    def generate_ps_template(self):
        dialog = tk.Toplevel(self)
        dialog.title("Generate Photoshop Template")
        dialog.geometry("550x550")
        
        tk.Label(dialog, text="Photoshop Template Generator", 
                font=('Arial', 12, 'bold')).pack(pady=10)
        
        tk.Label(dialog, text="This will generate a PSD template with proper guides\n"
                              "for rendering glyphs in Photoshop with perfect quality.",
                justify=tk.LEFT).pack(pady=5, padx=20)
        
        tk.Label(dialog, text="\nFont to render:").pack()
        font_var = tk.StringVar()
        font_entry = tk.Entry(dialog, textvariable=font_var, width=40)
        font_entry.pack()
        tk.Label(dialog, text="(Leave empty to use text tool in Photoshop)").pack()
        
        tk.Label(dialog, text="\nFont sizes (comma separated):").pack()
        sizes_var = tk.StringVar(value="10,12,14,16,18,24,32")
        tk.Entry(dialog, textvariable=sizes_var, width=40).pack()
        
        tk.Label(dialog, text="\nOutput directory:").pack()
        output_frame = tk.Frame(dialog)
        output_frame.pack()
        
        output_var = tk.StringVar(value=os.path.expanduser("~"))
        tk.Entry(output_frame, textvariable=output_var, width=35).pack(side=tk.LEFT)
        
        def browse():
            d = filedialog.askdirectory(initialdir=output_var.get())
            if d:
                output_var.set(d)
        
        tk.Button(output_frame, text="Browse...", command=browse).pack(side=tk.LEFT, padx=5)
        
        info_text = scrolledtext.ScrolledText(dialog, height=8, width=60, wrap=tk.WORD)
        info_text.pack(pady=10, padx=10)
        info_text.insert('1.0', """WORKFLOW:

1. Click 'Generate' - creates template PNGs + instruction file
2. Open template PNG in Photoshop
3. Use Text Tool with your TTF font (NO antialiasing!)
4. Type characters in each cell (guides show where)
5. Flatten layers, save as PNG
6. Import → From PNG Strip in BMF Editor
7. Perfect 1-bit glyphs with Photoshop quality!

The template has:
- 16x16 grid for 256 characters
- Red guides showing cell boundaries
- ASCII reference numbers
- Proper spacing for baseline""")
        info_text.config(state=tk.DISABLED)
        
        def generate():
            try:
                sizes_str = sizes_var.get()
                sizes = [int(s.strip()) for s in sizes_str.split(',')]
                output_dir = output_var.get()
                
                self.create_ps_templates(sizes, output_dir, font_var.get())
                dialog.destroy()
                messagebox.showinfo("Success", 
                    f"Generated {len(sizes)} templates in:\n{output_dir}\n\n"
                    "Open the PNG files in Photoshop and start typing!")
            except Exception as e:
                messagebox.showerror("Error", f"Failed to generate templates:\n{e}")
        
        tk.Button(dialog, text="Generate Templates", command=generate,
                 bg='green', fg='white', padx=20, pady=5).pack(pady=10)
    
    def create_ps_templates(self, sizes, output_dir, font_name):
        instruction_file = os.path.join(output_dir, "BMF_PHOTOSHOP_INSTRUCTIONS.txt")
        
        with open(instruction_file, 'w', encoding='utf-8') as f:
            f.write("BMF PHOTOSHOP TEMPLATE - INSTRUCTIONS\n")
            f.write("=" * 50 + "\n\n")
            f.write("GOAL: Create pixel-perfect bitmap fonts using Photoshop's superior rendering\n\n")
            f.write("FILES GENERATED:\n")
            for size in sizes:
                f.write(f"  - template_{size}pt.png\n")
            f.write("\nSTEPS:\n\n")
            f.write("1. OPEN TEMPLATE\n")
            f.write("   - Open template_XXpt.png in Photoshop\n")
            f.write("   - You'll see a 16x16 grid with guides\n\n")
            f.write("2. SETUP TEXT TOOL\n")
            f.write("   - Select Text Tool (T)\n")
            f.write(f"   - Font: {font_name if font_name else '[YOUR FONT HERE]'}\n")
            f.write("   - Size: Match template size (XXpt)\n")
            f.write("   - CRITICAL: Set anti-aliasing to 'None' !!!\n")
            f.write("   - Color: Black (#000000)\n\n")
            f.write("3. TYPE CHARACTERS\n")
            f.write("   - Grid layout is row-major (left to right, top to bottom)\n")
            f.write("   - Row 0: ASCII 0-15 (control chars - can skip or use replacements)\n")
            f.write("   - Row 1: ASCII 16-31 (control chars - can skip)\n")
            f.write("   - Row 2: ASCII 32-47 (space ! \" # $ % & ' ( ) * + , - . /)\n")
            f.write("   - Row 3: ASCII 48-63 (0-9 : ; < = > ? @)\n")
            f.write("   - Row 4: ASCII 64-79 (A-O)\n")
            f.write("   - Row 5: ASCII 80-95 (P-Z [ \\ ] ^ _ `)\n")
            f.write("   - Row 6: ASCII 96-111 (a-o)\n")
            f.write("   - Row 7: ASCII 112-127 (p-z { | } ~)\n")
            f.write("   - Row 8-15: Extended ASCII (128-255) - optional\n\n")
            f.write("   TIP: Type each character in its cell, center it manually\n")
            f.write("   Use guides to align baseline properly\n\n")
            f.write("4. SAVE\n")
            f.write("   - Flatten all layers (Layer > Flatten Image)\n")
            f.write("   - Save As > PNG\n")
            f.write(f"   - Name it: rendered_{sizes[0] if sizes else 'XX'}pt.png\n\n")
            f.write("5. IMPORT TO BMF EDITOR\n")
            f.write("   - Import > From PNG Strip\n")
            f.write("   - Select your rendered_XXpt.png\n")
            f.write("   - Enter glyph width and height (from template specs)\n")
            f.write("   - Done! Perfect glyphs!\n\n")
            f.write("TIPS:\n")
            f.write("- Keep template file, you can always re-render\n")
            f.write("- For monospace fonts, use fixed cell width\n")
            f.write("- Test with 'The quick brown fox' before full render\n")
            f.write("- Control characters (0-31): use placeholder symbols or leave blank\n\n")
            f.write("TROUBLESHOOTING:\n")
            f.write("- Blurry text? Check anti-aliasing is 'None'\n")
            f.write("- Wrong size? Verify font point size matches template\n")
            f.write("- Misaligned? Use Photoshop guides to center glyphs\n")
        
        for size in sizes:
            glyph_width = size
            glyph_height = size + 4
            
            cell_w = glyph_width + 4
            cell_h = glyph_height + 4
            
            img_w = cell_w * 16
            img_h = cell_h * 16
            
            img = Image.new('RGB', (img_w, img_h), (255, 255, 255))
            draw = ImageDraw.Draw(img)
            
            for i in range(17):
                y = i * cell_h
                color = (255, 0, 0) if i % 4 == 0 else (220, 220, 220)
                draw.line([(0, y), (img_w, y)], fill=color, width=1)
            
            for i in range(17):
                x = i * cell_w
                color = (255, 0, 0) if i % 4 == 0 else (220, 220, 220)
                draw.line([(x, 0), (x, img_h)], fill=color, width=1)
            
            baseline_offset = int(glyph_height * 0.75)
            for row in range(16):
                y = row * cell_h + baseline_offset + 2
                draw.line([(0, y), (img_w, y)], fill=(0, 0, 255), width=1)
            
            try:
                from PIL import ImageFont
                small_font = ImageFont.load_default()
                
                for ascii_code in range(256):
                    grid_x = ascii_code % 16
                    grid_y = ascii_code // 16
                    
                    x = grid_x * cell_w + 2
                    y = grid_y * cell_h + 2
                    
                    draw.text((x, y), str(ascii_code), fill=(180, 180, 180), font=small_font)
            except:
                pass
            
            template_path = os.path.join(output_dir, f"template_{size}pt.png")
            img.save(template_path)
        
        self.statusbar.config(text=f"Generated {len(sizes)} Photoshop templates")
    
    def export_glyph_sheet(self):
        if not self.font.sequences:
            messagebox.showinfo("Info", "No font loaded")
            return
        
        path = filedialog.asksaveasfilename(
            title="Export Glyph Sheet",
            defaultextension=".png",
            filetypes=[("PNG Images", "*.png"), ("All Files", "*.*")]
        )
        
        if not path:
            return
        
        seq = self.font.sequences[self.current_seq_idx]
        
        max_width = max(seq['glyphs'][i]['width'] for i in range(256))
        height = seq['height']
        
        cell_w = max_width + 2
        cell_h = height + 2
        
        img_w = cell_w * 16
        img_h = cell_h * 16
        
        img = Image.new('RGB', (img_w, img_h), (255, 255, 255))
        draw = ImageDraw.Draw(img)
        
        for i in range(17):
            y = i * cell_h
            draw.line([(0, y), (img_w, y)], fill=(200, 200, 200))
        for i in range(17):
            x = i * cell_w
            draw.line([(x, 0), (x, img_h)], fill=(200, 200, 200))
        
        pixels = img.load()
        
        for ascii_code in range(256):
            glyph = seq['glyphs'][ascii_code]
            
            grid_x = ascii_code % 16
            grid_y = ascii_code // 16
            
            base_x = grid_x * cell_w + 1
            base_y = grid_y * cell_h + 1
            
            for y in range(len(glyph['bitmap'])):
                for x in range(len(glyph['bitmap'][y])):
                    if glyph['bitmap'][y][x]:
                        px = base_x + x
                        py = base_y + y
                        if 0 <= px < img_w and 0 <= py < img_h:
                            pixels[px, py] = (0, 0, 0)
        
        img.save(path)
        self.statusbar.config(text=f"Exported glyph sheet: {os.path.basename(path)}")
        messagebox.showinfo("Done", f"Glyph sheet exported to:\n{path}")

if __name__ == '__main__':
    app = BMFEditor()
    app.mainloop()
