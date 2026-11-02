"""Windows GUI integration checks using an isolated temporary database."""

import argparse
import ctypes as c
from ctypes import wintypes as w
from pathlib import Path
import subprocess
import tempfile
import time

u = c.WinDLL("user32", use_last_error=True)
k = c.WinDLL("kernel32", use_last_error=True)
u.SetProcessDPIAware()
CALLBACK = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
u.EnumWindows.argtypes = [CALLBACK, w.LPARAM]
u.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
u.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
u.GetDlgItem.argtypes = [w.HWND, c.c_int]
u.GetDlgItem.restype = w.HWND
u.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.SendMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.SendMessageW.restype = w.LPARAM
u.SetWindowTextW.argtypes = [w.HWND, w.LPCWSTR]
u.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
u.GetWindowTextLengthW.argtypes = [w.HWND]
u.IsWindowVisible.argtypes = [w.HWND]
u.IsWindowEnabled.argtypes = [w.HWND]
u.GetWindowRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
u.SetForegroundWindow.argtypes = [w.HWND]
u.PrintWindow.argtypes = [w.HWND, w.HDC, w.UINT]
u.GetWindowDC.argtypes = [w.HWND]
u.GetWindowDC.restype = w.HDC
u.ReleaseDC.argtypes = [w.HWND, w.HDC]
u.MoveWindow.argtypes = [w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.BOOL]
k.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
k.OpenProcess.restype = w.HANDLE
k.VirtualAllocEx.argtypes = [w.HANDLE, c.c_void_p, c.c_size_t, w.DWORD, w.DWORD]
k.VirtualAllocEx.restype = c.c_void_p
k.VirtualFreeEx.argtypes = [w.HANDLE, c.c_void_p, c.c_size_t, w.DWORD]
k.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.c_void_p]
k.WriteProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.c_void_p]
k.CloseHandle.argtypes = [w.HANDLE]


class Item(c.Structure):
    _fields_ = [("mask", w.UINT), ("iItem", c.c_int), ("iSubItem", c.c_int),
                ("state", w.UINT), ("stateMask", w.UINT), ("pszText", c.c_void_p),
                ("cchTextMax", c.c_int), ("iImage", c.c_int), ("lParam", w.LPARAM),
                ("iIndent", c.c_int), ("iGroupId", c.c_int), ("cColumns", w.UINT),
                ("puColumns", c.c_void_p), ("piColFmt", c.c_void_p), ("iGroup", c.c_int)]


def wait(condition, label, timeout=8):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = condition()
        if result:
            return result
        time.sleep(0.05)
    raise AssertionError(f"Timeout: {label}")


class Driver:
    def __init__(self, process, screenshots):
        self.process = process
        self.screenshots = screenshots
        self.root = wait(lambda: self.find("SecondHandMarketWindow"), "main window")
        self.handle = k.OpenProcess(0x438, False, process.pid)
        assert self.handle
        self.remote = k.VirtualAllocEx(self.handle, None, 8192, 0x3000, 0x04)
        assert self.remote

    def find(self, kind):
        matches = []

        @CALLBACK
        def visitor(window, _):
            pid = w.DWORD()
            u.GetWindowThreadProcessId(window, c.byref(pid))
            name = c.create_unicode_buffer(256)
            u.GetClassNameW(window, name, 256)
            if pid.value == self.process.pid and name.value == kind and u.IsWindowVisible(window):
                matches.append(window)
            return True

        u.EnumWindows(visitor, 0)
        return matches[0] if matches else None

    def click(self, control_id, owner=None):
        owner = owner or self.root
        control = u.GetDlgItem(owner, control_id)
        assert control and u.IsWindowEnabled(control), f"Disabled/missing control {control_id}"
        modal_actions = {12, 13, 204, 210, 211, 213, 214, 221, 222,
                         230, 231, 232, 233, 234, 235, 236, 242, 243, 250, 264}
        if owner == self.root and control_id in modal_actions:
            u.PostMessageW(owner, 0x111, control_id, control)
        else:
            u.SendMessageW(owner, 0x111, control_id, control)
        time.sleep(0.15)

    def set_text(self, control_id, text, owner=None):
        buffer = c.create_unicode_buffer(text)
        assert u.SendMessageW(u.GetDlgItem(owner or self.root, control_id),
                              0x000C, 0, c.cast(buffer, c.c_void_p).value)

    def text(self, control_id, owner=None):
        handle = u.GetDlgItem(owner or self.root, control_id)
        buffer = c.create_unicode_buffer(u.GetWindowTextLengthW(handle) + 1)
        u.GetWindowTextW(handle, buffer, len(buffer))
        return buffer.value

    def form(self, values):
        form = wait(lambda: self.find("MarketFormWindow"), "input form")
        for index, value in enumerate(values):
            self.set_text(100 + index, value, form)
        self.click(1, form)
        wait(lambda: not self.find("MarketFormWindow"), "form saved")
        time.sleep(0.2)

    def yes(self):
        dialog = wait(lambda: self.find("#32770"), "confirmation")
        self.click(6, dialog)
        wait(lambda: not self.find("#32770"), "confirmation dismissed")

    def login(self, phone):
        self.click(14)
        self.click(12)
        self.form([phone, "Demo12345"])

    def count(self):
        return u.SendMessageW(u.GetDlgItem(self.root, 400), 0x1004, 0, 0)

    def cell(self, row, column):
        item = Item(iItem=row, iSubItem=column, pszText=self.remote + 256, cchTextMax=1024)
        assert k.WriteProcessMemory(self.handle, self.remote, c.byref(item), c.sizeof(item), None)
        u.SendMessageW(u.GetDlgItem(self.root, 400), 0x1073, row, self.remote)
        text = c.create_unicode_buffer(1024)
        assert k.ReadProcessMemory(self.handle, self.remote + 256, text, c.sizeof(text), None)
        return text.value

    def select(self, text=None, column=1):
        if text is None:
            wait(lambda: self.count() > 0, "non-empty list")
            row = 0
        else:
            def locate():
                for index in range(self.count()):
                    if self.cell(index, column) == text:
                        return index + 1
                return 0
            row = wait(locate, f"row {text}") - 1
        table = u.GetDlgItem(self.root, 400)
        item = Item(state=0, stateMask=3)
        k.WriteProcessMemory(self.handle, self.remote, c.byref(item), c.sizeof(item), None)
        u.SendMessageW(table, 0x102B, -1, self.remote)
        item.state = 3
        k.WriteProcessMemory(self.handle, self.remote, c.byref(item), c.sizeof(item), None)
        u.SendMessageW(table, 0x102B, row, self.remote)
        time.sleep(0.1)
        return row

    def shot(self, filename):
        if not self.screenshots:
            return
        from PIL import Image
        self.screenshots.mkdir(parents=True, exist_ok=True)
        bounds = w.RECT()
        u.GetWindowRect(self.root, c.byref(bounds))
        width, height = bounds.right - bounds.left, bounds.bottom - bounds.top
        g = c.WinDLL("gdi32", use_last_error=True)
        g.CreateCompatibleDC.argtypes = [w.HDC]
        g.CreateCompatibleDC.restype = w.HDC
        g.CreateCompatibleBitmap.argtypes = [w.HDC, c.c_int, c.c_int]
        g.CreateCompatibleBitmap.restype = w.HBITMAP
        g.SelectObject.argtypes = [w.HDC, w.HANDLE]
        g.SelectObject.restype = w.HANDLE
        g.GetDIBits.argtypes = [w.HDC, w.HBITMAP, w.UINT, w.UINT,
                               c.c_void_p, c.c_void_p, w.UINT]
        g.DeleteObject.argtypes = [w.HANDLE]
        g.DeleteDC.argtypes = [w.HDC]
        dc = u.GetWindowDC(self.root)
        memory = g.CreateCompatibleDC(dc)
        bitmap = g.CreateCompatibleBitmap(dc, width, height)
        previous = g.SelectObject(memory, bitmap)
        try:
            assert u.PrintWindow(self.root, memory, 2), "Window capture failed"
            header = c.create_string_buffer(40)
            import struct
            struct.pack_into("<IiiHHIIiiII", header, 0,
                             40, width, -height, 1, 32, 0, 0, 0, 0, 0, 0)
            g.SelectObject(memory, previous)
            pixels = c.create_string_buffer(width * height * 4)
            assert g.GetDIBits(memory, bitmap, 0, height, pixels, header, 0)
            Image.frombytes("RGB", (width, height), pixels.raw,
                            "raw", "BGRX").save(self.screenshots / filename)
        finally:
            g.SelectObject(memory, previous)
            g.DeleteObject(bitmap)
            g.DeleteDC(memory)
            u.ReleaseDC(self.root, dc)

    def close(self):
        k.VirtualFreeEx(self.handle, self.remote, 0, 0x8000)
        k.CloseHandle(self.handle)
        u.PostMessageW(self.root, 0x10, 0, 0)
        self.process.wait(timeout=8)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    parser.add_argument("--screenshots", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="market-gui-") as folder:
        data = str(Path(folder) / "中文数据.db")
        process = subprocess.Popen([str(args.executable.resolve()), "--data", data],
                                   stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.DEVNULL)
        driver = None
        try:
            driver = Driver(process, args.screenshots)
            d = driver
            d.form(["13800000001", "Demo12345", "Demo12345"])
            print("PASS: first-run administrator setup", flush=True)
            d.click(14)
            d.click(13)
            d.form(["13800000002", "Demo12345", "卖家", "Demo12345"])
            d.click(107)
            d.click(250)
            d.yes()
            d.login("13800000001")
            d.click(104)
            d.select("卖家")
            d.click(240)
            d.shot("02_用户管理.png")
            d.login("13800000002")
            d.click(103)
            for title, category, price in [("高等数学教材", "图书", "28.00"),
                                           ("可调节台灯", "数码", "45.00"),
                                           ("无线鼠标", "数码", "39.00")]:
                d.click(210)
                if title == "高等数学教材":
                    form = wait(lambda: d.find("MarketFormWindow"), "product form")
                    for i, value in enumerate([title, category, "2.999", "九成新，无损坏"]):
                        d.set_text(100 + i, value, form)
                    d.click(1, form)
                    assert d.find("MarketFormWindow") == form
                    assert "最多两位小数" in d.text(11, form)
                d.form([title, category, price, "九成新，无损坏"])
                d.select(title)
                d.click(212)
            d.shot("03_商品管理.png")
            print("PASS: registration, seller review, product validation and submission", flush=True)
            d.login("13800000001")
            d.click(103)
            for row in range(3):
                d.select(d.cell(row, 1))
                d.click(215)
            d.click(14)
            d.click(13)
            d.form(["13800000003", "Demo12345", "买家", "Demo12345"])
            wait(lambda: d.count() == 3, "three approved products visible")
            d.set_text(300, "教材")
            d.click(201)
            wait(lambda: d.count() == 1, "search result")
            d.click(202)
            wait(lambda: d.count() == 3, "reset search")
            d.select("高等数学教材")
            d.shot("01_商品市场.png")
            d.click(260)
            d.click(260)
            d.click(108)
            assert d.count() == 1
            d.select("高等数学教材")
            d.click(262)
            d.click(262)
            d.click(109)
            assert d.count() == 1
            d.select("高等数学教材")
            d.click(263)
            assert d.count() == 0
            d.click(108)
            d.select("高等数学教材")
            d.click(262)
            d.click(109)
            d.select("高等数学教材")
            d.shot("08_购物车.png")
            d.click(204)
            d.yes()
            assert d.count() == 0
            d.click(108)
            assert d.count() == 1
            d.shot("09_收藏夹.png")
            d.select("高等数学教材")
            d.click(261)
            assert d.count() == 0
            print("PASS: favorite/cart dedup, remove, reserve from cart and independent favorite", flush=True)
            d.click(101)
            d.select()
            assert not u.IsWindowEnabled(u.GetDlgItem(d.root, 222))
            d.login("13800000002")
            d.click(101)
            d.select()
            d.click(220)
            d.login("13800000003")
            d.click(101)
            d.select()
            d.click(222)
            d.form(["学校 宿舍"])
            d.click(102)
            d.select()
            assert not u.IsWindowEnabled(u.GetDlgItem(d.root, 233))
            d.click(230)
            d.yes()
            d.login("13800000002")
            d.click(102)
            d.select()
            d.click(232)
            d.form(["TRACK-DEMO"])
            d.login("13800000003")
            d.click(102)
            d.select()
            d.click(233)
            d.yes()
            assert d.cell(0, 3) == "已完成"
            assert not u.IsWindowEnabled(u.GetDlgItem(d.root, 230))
            d.shot("04_我的订单.png")
            d.login("13800000002")
            d.click(105)
            d.shot("05_统计汇总.png")
            print("PASS: GUI search, reserve, confirm, order, payment, ship and receive", flush=True)
            d.login("13800000003")
            d.select("无线鼠标")
            d.click(262)
            d.click(109)
            d.select("无线鼠标")
            d.click(264)
            d.yes()
            assert d.count() == 0
            d.click(100)
            lamp_row = d.select("可调节台灯")
            lamp_id = d.cell(lamp_row, 0)
            d.click(204)
            d.yes()
            d.login("13800000002")
            d.click(101)
            d.select(lamp_id, column=1)
            d.click(220)
            d.login("13800000003")
            d.click(101)
            d.select(lamp_id, column=1)
            d.click(222)
            d.form(["学校 宿舍"])
            d.click(102)
            d.select("可调节台灯")
            d.click(230)
            d.yes()
            d.click(234)
            d.yes()
            d.login("13800000002")
            d.click(102)
            refund_row = d.select("可调节台灯")
            d.click(235)
            d.yes()
            assert d.cell(refund_row, 3) == "已退款"
            d.shot("07_售后退款.png")
            print("PASS: full refund through the desktop UI", flush=True)
            d.login("13800000001")
            d.click(104)
            d.select("卖家")
            d.click(242)
            d.yes()
            d.login("13800000002")
            d.click(107)
            assert not u.IsWindowEnabled(u.GetDlgItem(d.root, 250))
            d.shot("06_冻结账号.png")
            d.click(103)
            assert not u.IsWindowEnabled(u.GetDlgItem(d.root, 210))
            print("PASS: frozen account UI restrictions", flush=True)
            d.close()
            driver = None
            cli = args.executable.with_name("market_cli.exe")
            result = subprocess.run([str(cli.resolve()), "--data", data],
                input="login 13800000002\nDemo12345\nstats\nquit\n", capture_output=True,
                encoding="utf-8", check=True, timeout=10)
            assert "completed: 1 | cents: 2800" in result.stdout
            print("PASS: GUI-written data verified after restart with the CLI", flush=True)
        finally:
            if process.poll() is None:
                if driver:
                    try:
                        driver.shot("failure.png")
                    except Exception:
                        pass
                process.terminate()
                process.wait(timeout=5)


if __name__ == "__main__":
    main()
