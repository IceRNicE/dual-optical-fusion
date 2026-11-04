import tkinter as tk
from tkinter import ttk
from tkinter import messagebox
import serial
import serial.tools.list_ports
import math
import time
import threading

class AlignTool:
    def __init__(self, root):
        self.root = root
        self.root.title("双光融合配准工具 v1.3")

        # 宽度1000，高度600，更适合宽屏显示
        self.root.geometry("1000x600")
        
        # 默认参数
        self.tx = 0.0
        self.ty = 0.0
        self.sx = 1.0
        self.sy = 1.0
        self.ang = 0.0
        self.alpha = 128
        
        # 2. 【修改】画布尺寸与缩放逻辑
        self.view_scale = 1.5  # 调整缩放比例，适合宽屏显示
        self.sensor_w = 240    # 热成像原始分辨率
        self.sensor_h = 240

        # 增加基准边距 (Padding)，这样240的框就不会占满画布了
        self.padding = 50
        # 画布总大小 = (传感器尺寸 + 两倍边距) * 缩放
        self.sim_w = int((self.sensor_w + self.padding * 2) * self.view_scale)
        self.sim_h = int((self.sensor_h + self.padding * 2) * self.view_scale)
        
        self.vflip = tk.IntVar(value=0)
        self.hmirror = tk.IntVar(value=0)
        self.selected_port = tk.StringVar()
        
        self._init_ui()
        
        # 启动后延迟执行自动初始化
        self.root.after(200, self.auto_init)

    def _init_ui(self):
        # --- 左侧控制栏 ---
        frm_left = tk.Frame(self.root, width=250)
        frm_left.pack(side=tk.LEFT, fill=tk.Y, padx=10, pady=10)
        frm_left.pack_propagate(False)  # 防止框架大小调整

        # --- 串口连接 ---
        frm_port = tk.LabelFrame(frm_left, text="串口连接", padx=5, pady=5)
        frm_port.pack(fill=tk.X, padx=5, pady=5)
        tk.Label(frm_port, text="端口:").pack(side=tk.LEFT, padx=5, pady=2)
        self.cmb_port = ttk.Combobox(frm_port, textvariable=self.selected_port, width=12, state="readonly")
        self.cmb_port.pack(side=tk.LEFT, padx=5, pady=2)
        self.cmb_port.bind("<<ComboboxSelected>>", self.on_port_selected)
        tk.Button(frm_port, text="刷新", command=self.refresh_ports, width=7).pack(side=tk.LEFT, padx=2, pady=2)

        # --- 透明度调节 ---
        frm_alpha = tk.LabelFrame(frm_left, text="透明度调节", padx=5, pady=5)
        frm_alpha.pack(fill=tk.X, padx=5, pady=5)
        self.scale_alpha = tk.Scale(frm_alpha, from_=0, to=255, orient=tk.HORIZONTAL, command=self.on_alpha_change)
        self.scale_alpha.set(self.alpha)
        self.scale_alpha.pack(fill=tk.X)
        self.scale_alpha.bind("<ButtonRelease-1>", self.on_alpha_release)

        # --- 翻转控制 ---
        frm_flip = tk.LabelFrame(frm_left, text="画面翻转", padx=5, pady=5)
        frm_flip.pack(fill=tk.X, padx=5, pady=5)
        tk.Button(frm_flip, text="上下翻转", command=lambda: self.send_flip_cmd("toggle_vflip")).pack(side=tk.LEFT, expand=True, fill=tk.X, padx=1)
        tk.Button(frm_flip, text="左右翻转", command=lambda: self.send_flip_cmd("toggle_hflip")).pack(side=tk.LEFT, expand=True, fill=tk.X, padx=1)

        # --- 操作栏 ---
        frm_action = tk.Frame(frm_left)
        frm_action.pack(fill=tk.X, padx=5, pady=5)
        self.btn_read = tk.Button(frm_action, text="从设备读取参数", command=self.sync_read_thread, bg="#dddddd", height=2)
        self.btn_read.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=2)

        # --- 右侧绘图区 ---
        frm_right = tk.Frame(self.root)
        frm_right.pack(side=tk.RIGHT, fill=tk.BOTH, expand=True, padx=10, pady=10)

        self.canvas = tk.Canvas(frm_right, width=self.sim_w, height=self.sim_h, bg="#1e1e1e", cursor="crosshair")
        self.canvas.pack(fill=tk.BOTH, expand=True)

        # --- 帮助信息 ---
        lbl_help = tk.Label(frm_right, text="左键拖拽: 平移 | 滚轮: 缩放 | 右键拖拽: 旋转\n橙色框: 热成像(固定) | 绿色框: 摄像头(可调)", fg="gray")
        lbl_help.pack(pady=5)

        # --- 状态栏 ---
        self.lbl_status = tk.Label(self.root, text="就绪", bd=1, relief=tk.SUNKEN, anchor=tk.W, font=("Arial", 10, "bold"))
        self.lbl_status.pack(side=tk.BOTTOM, fill=tk.X)

        # 绑定事件
        self.canvas.bind("<ButtonPress-1>", self.on_mouse_down)
        self.canvas.bind("<B1-Motion>", self.on_drag_move)
        self.canvas.bind("<ButtonRelease-1>", self.on_mouse_release)
        self.canvas.bind("<MouseWheel>", self.on_wheel)
        self.canvas.bind("<ButtonPress-3>", self.on_mouse_down)
        self.canvas.bind("<B3-Motion>", self.on_drag_rotate)
        self.canvas.bind("<ButtonRelease-3>", self.on_mouse_release)

        # 支持 Mac 触控板
        self.canvas.bind("<ButtonPress-2>", self.on_mouse_down)  # 点击触控板
        self.canvas.bind("<B2-Motion>", self.on_drag_rotate)  # 拖拽触控板
        self.canvas.bind("<ButtonRelease-2>", self.on_mouse_release)
        self.canvas.bind("<ButtonPress-4>", self.on_wheel)  # 触控板缩放（放大）
        self.canvas.bind("<ButtonPress-5>", self.on_wheel)  # 触控板缩放（缩小）

        self.last_pos = (0, 0)
        self.draw_wireframe()

    def auto_init(self):
        self.refresh_ports()
        if self.cmb_port.get():
            self.sync_read_thread()

    def refresh_ports(self):
        ports = sorted([p.device for p in serial.tools.list_ports.comports()])
        self.cmb_port['values'] = ports
        if ports:
            current = self.selected_port.get()
            if current in ports:
                self.cmb_port.current(ports.index(current))
            else:
                for i, p in enumerate(ports):
                    if "USB" in p or "COM" in p:
                        self.cmb_port.current(i)
                        return
                self.cmb_port.current(0)
    
    def on_port_selected(self, event):
        self.sync_read_thread()

    def set_status(self, msg, level="info"):
        self.lbl_status.config(text=msg)
        if level == "success":
            self.lbl_status.config(fg="green")
        elif level == "error":
            self.lbl_status.config(fg="red")
        elif level == "busy":
            self.lbl_status.config(fg="blue")
        else:
            self.lbl_status.config(fg="black")
        self.root.update_idletasks()

    # ================= 通信逻辑 (保持不变) =================
    
    def open_serial(self):
        port = self.selected_port.get()
        if not port:
            messagebox.showwarning("警告", "请先选择一个串口")
            return None
        try:
            ser = serial.Serial(port, 115200, timeout=0.5)
            return ser
        except Exception as e:
            self.set_status("连接失败", "error")
            messagebox.showerror("连接错误", str(e))
            return None

    def sync_read_thread(self):
        threading.Thread(target=self.sync_read, daemon=True).start()

    def sync_read(self):
        ser = self.open_serial()
        if not ser: return
        try:
            self.set_status("正在读取...", "busy")
            ser.reset_input_buffer()
            ser.write(b"get_align\n")
            
            start_t = time.time()
            found = False
            while time.time() - start_t < 2.0:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if line.startswith("ALIGN"):
                    parts = line.split()
                    if len(parts) >= 6:
                        self.tx = float(parts[1])
                        self.ty = float(parts[2])
                        self.sx = float(parts[3])
                        self.sy = float(parts[4])
                        self.ang = float(parts[5])
                        if len(parts) >= 7:
                            self.alpha = int(parts[6])
                            self.scale_alpha.set(self.alpha)
                        self.draw_wireframe()
                        self.set_status("读取成功", "success")
                        found = True
                        break
            if not found:
                self.set_status("读取超时", "error")
        except Exception as e:
            self.set_status(f"错误: {e}", "error")
        finally:
            if ser.is_open: ser.close()

    def sync_write_align(self):
        ser = self.open_serial()
        if not ser: return
        try:
            cmd = f"set_align {self.tx:.2f} {self.ty:.2f} {self.sx:.3f} {self.sy:.3f} {self.ang:.2f}\n"
            ser.write(cmd.encode())
            time.sleep(0.02)
            self.set_status(f"参数已发送", "success")
        except Exception as e:
            self.set_status(f"发送错误: {e}", "error")
        finally:
            if ser.is_open: ser.close()

    def sync_write_alpha(self):
        ser = self.open_serial()
        if not ser: return
        try:
            val = int(self.scale_alpha.get())
            ser.write(f"set_alpha {val}\n".encode())
            self.set_status(f"透明度已设为 {val}", "success")
        except Exception as e:
            self.set_status(f"错误: {e}", "error")
        finally:
            if ser.is_open: ser.close()

    def send_flip_cmd(self, cmd_str):
        ser = self.open_serial()
        if not ser: return
        try:
            ser.write(f"{cmd_str}\n".encode())
            self.set_status("指令已发送", "success")
        except Exception as e:
            self.set_status(f"错误: {e}", "error")
        finally:
            if ser.is_open: ser.close()

    # ================= 交互与绘图 (修改部分) =================
    
    def on_alpha_change(self, val): pass
    def on_alpha_release(self, event): self.sync_write_alpha()
    def on_mouse_down(self, event): self.last_pos = (event.x, event.y)
    def on_mouse_release(self, event): self.sync_write_align()

    def on_drag_move(self, event):
        dx = (event.x - self.last_pos[0]) / self.view_scale
        dy = (event.y - self.last_pos[1]) / self.view_scale
        self.tx += dx
        self.ty += dy
        self.last_pos = (event.x, event.y)
        self.draw_wireframe()

    def on_drag_rotate(self, event):
        dx = (event.x - self.last_pos[0])
        dy = (event.y - self.last_pos[1])
        self.ang += (dx + dy) * 0.5
        self.last_pos = (event.x, event.y)
        self.draw_wireframe()

    def on_wheel(self, event):
        # 处理鼠标滚轮和触控板缩放事件
        # Windows/Linux: event.delta (滚轮), Mac: event.num (按钮)
        if hasattr(event, 'delta'):
            # 传统鼠标滚轮事件
            factor = 1.05 if event.delta > 0 else 0.95
        elif hasattr(event, 'num'):
            # Mac 触控板缩放事件
            factor = 1.05 if event.num == 4 else 0.95
        else:
            return

        self.sx *= factor
        self.sy *= factor
        self.draw_wireframe()
        # 减少串口发送频率，只在操作结束后发送
        # self.sync_write_align()

    def draw_wireframe(self):
        self.canvas.delete("all")
        cw, ch = self.sim_w, self.sim_h
        
        # 1. 绘制辅助网格 (十字线)
        self.canvas.create_line(cw/2, 0, cw/2, ch, fill="#444", dash=(2,4))
        self.canvas.create_line(0, ch/2, cw, ch/2, fill="#444", dash=(2,4))
        
        # 2. 【修改】绘制热成像基准框 (橙色)
        # 热成像的物理尺寸 (240) 乘以 视图缩放比例
        scaled_sensor_size = self.sensor_w * self.view_scale
        
        # 始终将框绘制在画布正中心
        bx1 = (cw - scaled_sensor_size) / 2
        by1 = (ch - scaled_sensor_size) / 2
        bx2 = bx1 + scaled_sensor_size
        by2 = by1 + scaled_sensor_size
        
        self.canvas.create_rectangle(bx1, by1, bx2, by2, 
                                     outline="orange", width=3, dash=(5, 2), tags="thermal")
        self.canvas.create_text(bx1 + 5, by1 + 5, text="热成像 (基准 240x240)", fill="orange", anchor=tk.NW)

        # 3. 绘制摄像头调节框 (绿色)
        half = self.sensor_w / 2
        pts = [(-half, -half), (half, -half), (half, half), (-half, half)]
        
        final_pts = []
        rad = math.radians(self.ang)
        cos_a = math.cos(rad)
        sin_a = math.sin(rad)
        
        # 屏幕中心点
        screen_cx = cw / 2
        screen_cy = ch / 2
        
        for x, y in pts:
            x *= self.sx
            y *= self.sy
            rx = x * cos_a - y * sin_a
            ry = x * sin_a + y * cos_a
            fx = rx + self.tx
            fy = ry + self.ty
            
            # 坐标转换：屏幕中心 + 偏移量 * 缩放
            screen_x = screen_cx + (fx * self.view_scale)
            screen_y = screen_cy + (fy * self.view_scale)
            final_pts.append(screen_x)
            final_pts.append(screen_y)
            
        final_pts += final_pts[:2]
        
        self.canvas.create_line(final_pts, fill="#00FF00", width=2, tags="cam")
        
        # 摄像头中心点
        center_x = screen_cx + self.tx * self.view_scale
        center_y = screen_cy + self.ty * self.view_scale
        self.canvas.create_oval(center_x-4, center_y-4, center_x+4, center_y+4, outline="#00FF00", width=2)
        
        # 参数文字
        info = f"平移: ({self.tx:.1f}, {self.ty:.1f})\n缩放: {self.sx:.3f}\n旋转: {self.ang:.1f}°"
        self.canvas.create_text(10, ch-10, text=info, fill="#00FF00", anchor=tk.SW, font=("Consolas", 10))
        self.canvas.create_text(center_x + 10, center_y, text="摄像头", fill="#00FF00", anchor=tk.W)

if __name__ == "__main__":
    root = tk.Tk()
    app = AlignTool(root)
    root.mainloop()