# -*- coding: utf-8 -*-
"""One-path picture guide: first Pelish write on a factory Avalon Nano 3s."""

import io
import math
import os

from PIL import Image
from reportlab.lib.colors import Color, white
from reportlab.lib.utils import ImageReader
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.pdfgen import canvas

OUT = r"G:\cyber tools\3s\Nano3s-pelish-install.pdf"
STAGING = r"G:\cyber tools\3s\_pelish_install_staging.pdf"
PHOTO = r"G:\cyber tools\3s\Canaan-Avalon-Nano-3s-6TH.png"
PELISH = r"G:\cyber tools\3s\apps\miner-web\assets\pelish-logo.png"

W, H = 612, 792
PAGES = 7

pdfmetrics.registerFont(TTFont("S", r"C:\Windows\Fonts\segoeui.ttf"))
pdfmetrics.registerFont(TTFont("B", r"C:\Windows\Fonts\segoeuib.ttf"))
pdfmetrics.registerFont(TTFont("M", r"C:\Windows\Fonts\consola.ttf"))

def C(h):
    h = h.lstrip("#")
    return Color(int(h[0:2], 16) / 255, int(h[2:4], 16) / 255, int(h[4:6], 16) / 255)

INK = C("1c1917")
MUTED = C("5c564c")
GOLD = C("b8860b")
GOLD_D = C("7a5a10")
GOLD_S = C("fff6e0")
BLUE = C("1e4d8c")
BLUE_S = C("e7f0fb")
RED = C("9f2d2d")
RED_S = C("fdecea")
GREEN = C("176b40")
GREEN_S = C("e7f6ee")
LINE = C("e4dccf")
CREAM = C("fbf8f3")
SOFT = C("f4f0e8")
CHAR = C("1a1a1a")

KEEP = []


def reader_from(im):
    bio = io.BytesIO()
    im.save(bio, format="PNG")
    bio.seek(0)
    KEEP.append(bio)
    return ImageReader(bio)


def key_near_black(path, thresh=28):
    im = Image.open(path).convert("RGBA")
    px = im.load()
    w, h = im.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if r <= thresh and g <= thresh and b <= thresh:
                px[x, y] = (r, g, b, 0)
    return im


def load_assets():
    photo = Image.open(PHOTO).convert("RGBA").crop((36, 104, 538, 396))
    return {
        "photo": reader_from(photo),
        "photo_size": photo.size,
        "pelish": reader_from(key_near_black(PELISH)),
    }


def wrap(c, text, font, size, width):
    words = text.split()
    lines, cur = [], ""
    for word in words:
        trial = word if not cur else cur + " " + word
        if c.stringWidth(trial, font, size) <= width:
            cur = trial
        else:
            if cur:
                lines.append(cur)
            cur = word
    if cur:
        lines.append(cur)
    return lines


def text(c, s, x, y, font="S", size=11, color=INK):
    c.setFont(font, size)
    c.setFillColor(color)
    c.drawString(x, y, s)


def center(c, s, x, y, font="S", size=11, color=INK):
    c.setFont(font, size)
    c.setFillColor(color)
    c.drawCentredString(x, y, s)


def para(c, s, x, y, width, font="S", size=13, color=INK, leading=17):
    lines = wrap(c, s, font, size, width)
    c.setFont(font, size)
    c.setFillColor(color)
    for i, line in enumerate(lines):
        c.drawString(x, y - i * leading, line)
    return y - (len(lines) - 1) * leading - leading


def sentences(c, lines, x, y, width, size=13.5, leading=17, gap=7):
    for line in lines:
        y = para(c, line, x, y, width, "S", size, INK, leading)
        y -= gap
    return y


def rr(c, x, y, w, h, r, fill=None, stroke=None, sw=1):
    c.saveState()
    c.setLineWidth(sw)
    if fill:
        c.setFillColor(fill)
    if stroke:
        c.setStrokeColor(stroke)
    c.roundRect(x, y, w, h, r, fill=1 if fill else 0, stroke=1 if stroke else 0)
    c.restoreState()


def arrow(c, x1, y1, x2, y2, color=INK, weight=2.2, head=9):
    c.saveState()
    c.setStrokeColor(color)
    c.setFillColor(color)
    c.setLineWidth(weight)
    c.setLineCap(1)
    c.line(x1, y1, x2, y2)
    ang = math.atan2(y2 - y1, x2 - x1)
    p = c.beginPath()
    p.moveTo(x2, y2)
    p.lineTo(x2 + head * math.cos(ang + 2.6), y2 + head * math.sin(ang + 2.6))
    p.lineTo(x2 + head * math.cos(ang - 2.6), y2 + head * math.sin(ang - 2.6))
    p.close()
    c.drawPath(p, fill=1, stroke=0)
    c.restoreState()


def cable(c, x1, y1, x2, y2, color=INK, weight=7):
    c.saveState()
    c.setStrokeColor(color)
    c.setLineWidth(weight)
    c.setLineCap(1)
    path = c.beginPath()
    path.moveTo(x1, y1)
    mid = (x1 + x2) / 2.0
    path.curveTo(mid, y1 - 10, mid, y2 - 10, x2, y2)
    c.drawPath(path, stroke=1, fill=0)
    c.restoreState()


def card(c, x, y, w, h, r=14, fill=white, stroke=LINE):
    c.setFillColor(C("eadfcb"))
    c.roundRect(x + 3, y - 3, w, h, r, fill=1, stroke=0)
    rr(c, x, y, w, h, r, fill=fill, stroke=stroke, sw=1.1)


def footer(c, page):
    c.setStrokeColor(LINE)
    c.setLineWidth(0.7)
    c.line(32, 34, W - 32, 34)
    c.setFillColor(MUTED)
    c.setFont("S", 8)
    c.drawString(32, 18, "Pelish  ·  Avalon Nano 3s")
    c.drawRightString(W - 32, 18, "%d / %d" % (page, PAGES))


def background(c):
    c.setFillColor(CREAM)
    c.rect(0, 0, W, H, fill=1, stroke=0)
    c.setFillColor(GOLD)
    c.rect(0, H - 8, W, 8, fill=1, stroke=0)


def bookmark(c, page, title):
    key = "p%d" % page
    c.bookmarkPage(key)
    c.addOutlineEntry("%d  %s" % (page, title), key, level=0, closed=False)


def step_header(c, n, title, page):
    background(c)
    c.setFillColor(GOLD)
    c.setFont("B", 68)
    c.drawString(30, 708, str(n))
    nw = c.stringWidth(str(n), "B", 68)
    c.setFillColor(INK)
    c.setFont("B", 22)
    c.drawString(30 + nw + 14, 728, title)
    footer(c, page)
    bookmark(c, page, title)


def label_stack(c, x, y, name, note, color):
    center(c, name, x, y, "B", 11, color)
    if note:
        center(c, note, x, y - 14, "S", 9.5, MUTED)


# --- pictograms -----------------------------------------------------------

def draw_slot(c, x, y, s=1):
    """The one data hole. Wide, dark, where the Wi-Fi stick sits."""
    w, h = 78 * s, 32 * s
    c.setFillColor(C("2c2c2c"))
    c.roundRect(x - w / 2, y - h / 2, w, h, 5 * s, fill=1, stroke=0)
    c.setFillColor(C("111111"))
    c.roundRect(x - w / 2 + 5 * s, y - h / 2 + 5 * s, w - 10 * s, h - 10 * s, 3 * s, fill=1, stroke=0)
    c.setFillColor(C("9a9186"))
    c.rect(x - 16 * s, y - 5 * s, 32 * s, 10 * s, fill=1, stroke=0)
    c.setFillColor(C("3a3a3a"))
    c.rect(x - 10 * s, y - 3 * s, 4 * s, 6 * s, fill=1, stroke=0)
    c.rect(x - 2 * s, y - 3 * s, 4 * s, 6 * s, fill=1, stroke=0)
    c.rect(x + 6 * s, y - 3 * s, 4 * s, 6 * s, fill=1, stroke=0)


def draw_stick(c, x, y, s=1):
    """Wi-Fi stick: short body and a thin antenna."""
    w, h = 22 * s, 14 * s
    c.setFillColor(C("2f3840"))
    c.roundRect(x - w / 2, y - h / 2, w, h, 2 * s, fill=1, stroke=0)
    c.setFillColor(C("c5bfb4"))
    c.rect(x - 6 * s, y - h / 2 - 7 * s, 12 * s, 8 * s, fill=1, stroke=0)
    c.setStrokeColor(C("2f3840"))
    c.setFillColor(C("2f3840"))
    c.setLineWidth(2 * s)
    c.line(x, y + h / 2, x, y + h / 2 + 22 * s)
    c.circle(x, y + h / 2 + 26 * s, 3.2 * s, fill=1, stroke=0)


def draw_usbc(c, x, y, s=1):
    w, h = 46 * s, 20 * s
    c.setFillColor(white)
    c.setStrokeColor(BLUE)
    c.setLineWidth(2)
    c.roundRect(x - w / 2, y - h / 2, w, h, 8 * s, fill=1, stroke=1)
    c.setFillColor(BLUE)
    c.roundRect(x - 10 * s, y - 4 * s, 20 * s, 8 * s, 2.5 * s, fill=1, stroke=0)


def draw_pin(c, x, y, s=1):
    """Tiny hole with a pin held in it."""
    c.setFillColor(CHAR)
    c.circle(x, y, 7 * s, fill=1, stroke=0)
    c.setStrokeColor(C("8d8d8d"))
    c.setLineWidth(1)
    c.circle(x, y, 7 * s, fill=0, stroke=1)
    c.setStrokeColor(GOLD_D)
    c.setFillColor(GOLD)
    c.setLineWidth(2.4 * s)
    c.setLineCap(1)
    c.line(x + 3 * s, y + 3 * s, x + 28 * s, y + 36 * s)
    c.circle(x + 31 * s, y + 40 * s, 4 * s, fill=1, stroke=0)


def draw_pc(c, x, y, w=108, h=78):
    rr(c, x, y + 16, w, h - 16, 6, fill=CHAR)
    rr(c, x + 6, y + 24, w - 12, h - 32, 3, fill=C("2e6b4f"))
    center(c, "PC", x + w / 2, y + 40, "B", 14, white)
    c.setFillColor(C("4a4a4a"))
    c.rect(x + w / 2 - 16, y, 32, 16, fill=1, stroke=0)
    c.rect(x + w / 2 - 28, y, 56, 6, fill=1, stroke=0)


def draw_brick(c, x, y):
    rr(c, x, y, 70, 40, 6, fill=white, stroke=BLUE, sw=1.6)
    center(c, "power", x + 35, y + 16, "B", 11, BLUE)


def draw_router(c, x, y):
    rr(c, x, y, 78, 44, 6, fill=white, stroke=GREEN, sw=1.6)
    center(c, "router", x + 39, y + 16, "B", 11, GREEN)


def draw_rj45(c, x, y, s=1):
    """The clear wide network plug, so the ethernet dongle is obvious."""
    c.setFillColor(C("d9efe4"))
    c.setStrokeColor(GREEN)
    c.setLineWidth(1.4)
    p = c.beginPath()
    p.moveTo(x - 16 * s, y - 10 * s)
    p.lineTo(x + 10 * s, y - 10 * s)
    p.lineTo(x + 18 * s, y + 10 * s)
    p.lineTo(x - 16 * s, y + 10 * s)
    p.close()
    c.drawPath(p, fill=1, stroke=1)
    c.setFillColor(GOLD)
    for i in range(4):
        c.rect(x - 8 * s + i * 5 * s, y - 6 * s, 2.2 * s, 7 * s, fill=1, stroke=0)


def draw_file(c, x, y, w, h, title, color):
    c.setFillColor(white)
    c.setStrokeColor(color)
    c.setLineWidth(1.6)
    p = c.beginPath()
    p.moveTo(x, y)
    p.lineTo(x + w - 16, y)
    p.lineTo(x + w, y + 16)
    p.lineTo(x + w, y + h)
    p.lineTo(x, y + h)
    p.close()
    c.drawPath(p, fill=1, stroke=1)
    c.setFillColor(color)
    tri = c.beginPath()
    tri.moveTo(x + w - 16, y)
    tri.lineTo(x + w - 16, y + 16)
    tri.lineTo(x + w, y + 16)
    tri.close()
    c.drawPath(tri, fill=1, stroke=0)
    center(c, title, x + w / 2, y + h / 2 - 4, "B", 13, color)


def mini_num(c, x, y, n):
    c.setFillColor(GOLD)
    c.circle(x, y, 11, fill=1, stroke=0)
    center(c, str(n), x, y - 4, "B", 12, white)


# --- cover ----------------------------------------------------------------

def page_cover(c, a):
    background(c)
    footer(c, 1)
    bookmark(c, 1, "What you will do")
    c.drawImage(a["pelish"], W - 78, H - 78, 46, 46, mask="auto")
    text(c, "WHAT YOU WILL DO", 36, 742, "B", 11, GOLD_D)
    sentence = (
        "Unplug the Wi-Fi dongle, hold BOOT, write your .kdimg, "
        "then plug the ethernet dongle into the same hole."
    )
    para(c, sentence, 36, 712, 500, "B", 18, INK, 24)

    panels = [
        (1, "Unplug the dongle", _cover_unplug),
        (2, "Hold BOOT", _cover_boot),
        (3, "Write your .kdimg", _cover_write),
        (4, "Ethernet dongle", _cover_lan),
    ]
    pw, ph = 258, 216
    gap = 14
    x0, y0 = 36, 156
    for i, (n, cap, fn) in enumerate(panels):
        col, row = i % 2, 1 - (i // 2)
        x = x0 + col * (pw + gap)
        y = y0 + row * (ph + gap)
        card(c, x, y, pw, ph, 12, fill=white)
        mini_num(c, x + 22, y + ph - 22, n)
        fn(c, x, y, pw, ph)
        center(c, cap, x + pw / 2, y + 16, "B", 13, INK)


def _cover_unplug(c, x, y, w, h):
    cx, cy = x + w / 2, y + 108
    draw_slot(c, cx, cy - 10, 1.05)
    draw_stick(c, cx + 8, cy + 48, 1.15)
    arrow(c, cx + 8, cy + 18, cx + 8, cy + 36, GREEN, 2.4, 9)
    center(c, "out", cx + 36, cy + 24, "B", 10, GREEN)


def _cover_boot(c, x, y, w, h):
    draw_pin(c, x + 78, y + 78, 1.05)
    center(c, "pinhole BOOT", x + 92, y + 52, "B", 9, GOLD_D)
    arrow(c, x + 150, y + 118, x + 176, y + 118, GOLD_D, 2, 8)
    center(c, "then", x + 163, y + 128, "B", 8, GOLD_D)
    draw_usbc(c, x + 206, y + 118, 0.95)
    center(c, "power only", x + 206, y + 78, "B", 9, BLUE)


def _cover_write(c, x, y, w, h):
    draw_pc(c, x + 22, y + 78, 86, 64)
    cable(c, x + 112, y + 118, x + 168, y + 118, INK, 6)
    draw_slot(c, x + 196, y + 122, 0.85)
    center(c, ".kdimg", x + 140, y + 132, "B", 9, GREEN)


def _cover_lan(c, x, y, w, h):
    draw_slot(c, x + 62, y + 124, 0.8)
    rr(c, x + 88, y + 110, 62, 28, 4, fill=white, stroke=GREEN, sw=1.5)
    center(c, "ethernet", x + 119, y + 119, "B", 8, GREEN)
    c.saveState()
    c.setStrokeColor(GREEN)
    c.setLineWidth(5)
    c.setLineCap(1)
    c.line(x + 152, y + 124, x + 168, y + 124)
    c.restoreState()
    draw_router(c, x + 168, y + 102)
    center(c, "same hole", x + 110, y + 78, "B", 10, GREEN)


# --- step 1 ---------------------------------------------------------------

def page_holes(c, a):
    step_header(c, 1, "Find the three holes", 2)
    x, y, w, h = 32, 176, 548, 490
    card(c, x, y, w, h, 16, fill=white)

    pw = 268
    ph = pw * a["photo_size"][1] / a["photo_size"][0]
    px, py = x + 18, y + h - 16 - ph
    c.setFillColor(CHAR)
    c.roundRect(px - 4, py - 4, pw + 8, ph + 8, 8, fill=1, stroke=0)
    c.drawImage(a["photo"], px, py, pw, ph, mask="auto")

    def spot(fx, fy):
        return px + fx * pw, py + ph - fy * ph

    sx, sy = spot(358 / 502, 145 / 292)
    bx, by = spot(431 / 502, 147 / 292)
    c.setStrokeColor(GOLD)
    c.setLineWidth(1.8)
    c.circle(sx, sy, 18, fill=0, stroke=1)
    c.circle(bx, by, 8, fill=0, stroke=1)
    c.setStrokeColor(GOLD_D)
    c.setLineWidth(1.1)
    c.line(sx, sy + 20, sx, sy + 36)
    center(c, "screen", sx, sy + 42, "B", 11, INK)
    c.line(bx + 10, by, px + pw + 18, by)
    text(c, "not BOOT", px + pw + 22, by - 4, "B", 12, GOLD_D)
    text(c, "THE EDGE  —  three different holes", x + 20, py - 28, "B", 11, GOLD_D)
    bar_x = x + 16
    bar_w = w - 32
    bar_top = py - 44
    bar_y = y + 52
    bar_h = bar_top - bar_y
    rr(c, bar_x, bar_y, bar_w, bar_h, 12, fill=SOFT, stroke=LINE, sw=1)
    cols = [bar_x + 92, bar_x + bar_w / 2, bar_x + bar_w - 96]
    mid = bar_y + bar_h * 0.46
    draw_stick(c, cols[0], mid + 28, 1.25)
    draw_slot(c, cols[0], mid - 6, 1.3)
    draw_usbc(c, cols[1], mid + 6, 1.45)
    draw_pin(c, cols[2] - 18, mid - 16, 1.15)
    label_stack(c, cols[0], y + 30, "DONGLE HOLE", "(data)", GREEN)
    label_stack(c, cols[1], y + 30, "ROUND USB-C", "(power only)", BLUE)
    label_stack(c, cols[2], y + 30, "pinhole BOOT", "", GOLD_D)

    sentences(c, [
        "The photo is the top. The black circle is the screen.",
        "The wide hole on the edge holds the Wi-Fi stick. Data uses that hole only.",
        "The round plug is USB-C. It only brings power.",
        "BOOT is the tiny pinhole. The pale dot beside the screen is not BOOT.",
    ], 36, 156, 540, 13, 16, 5)


# --- step 2 ---------------------------------------------------------------

def page_file(c, a):
    step_header(c, 2, "Get your .kdimg", 3)
    x, y, w, h = 32, 188, 548, 478
    card(c, x, y, w, h, 16, fill=white)

    draw_file(c, x + 196, y + 300, 150, 130, "", GREEN)
    center(c, ".kdimg", x + 271, y + 368, "B", 22, GREEN)
    center(c, "write this", x + 271, y + 342, "S", 11, GREEN)

    # the other two, smaller, so the right file is obvious
    draw_file(c, x + 36, y + 318, 110, 86, "", C("b9a56a"))
    center(c, ".aup", x + 91, y + 364, "B", 16, GOLD_D)
    center(c, "later", x + 91, y + 344, "S", 10, MUTED)

    rr(c, x + 390, y + 328, 124, 78, 8, fill=RED_S, stroke=RED, sw=1.2)
    center(c, "Mujina", x + 452, y + 372, "B", 14, RED)
    center(c, "do not write", x + 452, y + 352, "S", 10, RED)

    text(c, "WHERE THE FILE IS", x + 24, y + 268, "B", 8.5, MUTED)
    box_y = y + 16
    rr(c, x + 16, box_y, w - 32, 236, 8, fill=SOFT, stroke=LINE, sw=0.8)
    rows = [
        ("Windows", r"G:\cyber tools\3s\build\images"),
        ("WSL", "/home/owner/canaan-sdk-nano3s/build/images/"),
        ("File", "AvalonHome_nano3s_nmm1v1x1_mm319_s1.1.2_m0.3.59_dbg_"),
        ("", "<date>_<time>_<rev>.kdimg"),
        ("Unpack", "gunzip -k build/images/AvalonHome_nano3s_….kdimg.gz"),
    ]
    yy = box_y + 210
    for lab, val in rows:
        if lab:
            text(c, lab, x + 30, yy, "B", 8, BLUE)
            yy -= 14
        text(c, val, x + 30, yy, "M", 8, INK)
        yy -= 22

    sentences(c, [
        "Use the file that ends in .kdimg.",
        "If you only see .kdimg.gz, unpack it and keep the .kdimg.",
        "A factory miner will not take the .aup update yet.",
        "Do not write a Mujina file.",
    ], 36, 166, 540, 13, 16, 6)


# --- step 3 ---------------------------------------------------------------

def page_cable(c, a):
    step_header(c, 3, "Plug the PC into that hole", 4)
    x, y, w, h = 32, 214, 548, 452
    card(c, x, y, w, h, 16, fill=white)

    # data row: PC --- cable --- the hole the stick left
    draw_pc(c, x + 28, y + 250, 112, 86)
    cable(c, x + 148, y + 310, x + 292, y + 310, INK, 8)
    center(c, "cable", x + 220, y + 328, "B", 11, INK)
    draw_slot(c, x + 348, y + 314, 1.35)
    draw_stick(c, x + 470, y + 392, 1.15)
    arrow(c, x + 400, y + 348, x + 448, y + 378, GREEN, 2.4, 9)
    center(c, "OUT", x + 492, y + 368, "B", 13, GREEN)
    label_stack(c, x + 348, y + 258, "DONGLE HOLE", "(data)", GREEN)

    # then: pin, then round power
    c.setStrokeColor(LINE)
    c.setLineWidth(1)
    c.line(x + 24, y + 228, x + w - 24, y + 228)
    center(c, "THEN", x + w / 2, y + 206, "B", 10, GOLD_D)

    draw_pin(c, x + 70, y + 78, 1.15)
    label_stack(c, x + 110, y + 52, "pinhole BOOT", "hold the pin", GOLD_D)
    arrow(c, x + 210, y + 130, x + 250, y + 130, GOLD_D, 2.4, 9)
    center(c, "then power", x + 230, y + 142, "B", 9, GOLD_D)
    draw_brick(c, x + 268, y + 108)
    cable(c, x + 342, y + 128, x + 400, y + 128, BLUE, 6)
    draw_usbc(c, x + 440, y + 128, 1.25)
    label_stack(c, x + 440, y + 78, "ROUND USB-C", "(power only)", BLUE)

    sentences(c, [
        "Unplug the round power, and pull the Wi-Fi stick out.",
        "Plug a data cable from the PC into that same hole. On the PC, a wide plug or USB-C is fine.",
        "Hold a pin in BOOT, then plug the round power back in.",
        "Let go of the pin when the PC sees the miner.",
    ], 36, 196, 540, 12.5, 15, 5)
    para(
        c,
        "If the PC does not see it: VID 29f1 PID 0230, often named K230 USB Boot Device. "
        "On Windows, set only that device to WinUSB. Do not change a keyboard, mouse, disk, or hub.",
        36, 78, 540, "S", 7.5, MUTED, 10,
    )


# --- step 4 ---------------------------------------------------------------

def page_write(c, a):
    step_header(c, 4, "Write the file", 5)
    x, y, w, h = 32, 348, 548, 318
    card(c, x, y, w, h, 16, fill=white)

    # Windows tool
    tw = 308
    tx, ty = x + 16, y + 16
    th = h - 32
    rr(c, tx, ty, tw, th, 10, fill=SOFT, stroke=LINE, sw=1)
    c.setFillColor(BLUE)
    c.rect(tx, ty + th - 28, tw, 28, fill=1, stroke=0)
    text(c, "K230 Burning Tool", tx + 12, ty + th - 19, "B", 12, white)
    fields = [
        ("File", "your .kdimg"),
        ("Medium", "SPI NAND"),
        ("Image part", "all"),
    ]
    fy = ty + th - 56
    for lab, val in fields:
        text(c, lab, tx + 16, fy, "S", 11, MUTED)
        rr(c, tx + 108, fy - 7, tw - 128, 26, 6, fill=white, stroke=LINE, sw=0.8)
        text(c, val, tx + 118, fy, "B", 12, INK)
        fy -= 46
    rr(c, tx + 64, fy + 6, tw - 128, 36, 8, fill=GREEN)
    center(c, "Start", tx + tw / 2, fy + 18, "B", 15, white)
    center(c, "Wait until it says finished.", tx + tw / 2, fy - 16, "S", 10, MUTED)
    center(c, "Downloading Completed", tx + tw / 2, fy - 34, "B", 12, GREEN)

    # same job, one command
    rx = x + 340
    rw = w - 358
    rr(c, rx, ty, rw, th, 10, fill=CHAR)
    text(c, "OR THIS COMMAND", rx + 14, ty + th - 28, "B", 9, GOLD)
    cmd = [
        "cd /home/owner/",
        "  canaan-sdk-nano3s",
        "",
        "kburn -m nand \\",
        "  build/images/",
        "  AvalonHome_nano3s_",
        "  ….kdimg",
    ]
    yy = ty + th - 58
    for line in cmd:
        text(c, line, rx + 14, yy, "M", 9, C("f4efe4"))
        yy -= 18
    text(c, "-m nand is SPI NAND.", rx + 14, ty + 52, "S", 9, C("f4efe4"))
    text(c, "Same write.", rx + 14, ty + 28, "B", 14, GOLD)

    sentences(c, [
        "Pick your .kdimg, not the .gz.",
        "Set Medium to SPI NAND and Image part to all.",
        "Start, and wait until the tool says it is finished. The command does the same write.",
        "Leave the power and the cable plugged in until then.",
    ], 36, 326, 540, 13, 16, 6)


# --- step 5 ---------------------------------------------------------------

def page_lan(c, a):
    step_header(c, 5, "Plug in the ethernet dongle", 6)
    x, y, w, h = 32, 168, 548, 498
    card(c, x, y, w, h, 16, fill=white)

    center(c, "SAME HOLE", x + 210, y + h - 28, "B", 12, GREEN)
    draw_slot(c, x + 150, y + 372, 1.35)
    rr(c, x + 196, y + 354, 96, 36, 5, fill=white, stroke=GREEN, sw=1.6)
    center(c, "ethernet", x + 244, y + 366, "B", 11, GREEN)
    c.saveState()
    c.setStrokeColor(GREEN)
    c.setLineWidth(6)
    c.setLineCap(1)
    c.line(x + 296, y + 372, x + 360, y + 372)
    c.restoreState()
    draw_router(c, x + 360, y + 350)
    center(c, "USB-to-RJ45", x + 244, y + 334, "S", 9, MUTED)
    label_stack(c, x + 150, y + 322, "DONGLE HOLE", "(data)", GREEN)

    # screen
    c.setFillColor(CHAR)
    c.circle(x + 78, y + 250, 36, fill=1, stroke=0)
    center(c, "your", x + 78, y + 256, "B", 11, white)
    center(c, "IP", x + 78, y + 240, "B", 11, white)
    center(c, "round screen", x + 78, y + 200, "S", 9, MUTED)

    c.setStrokeColor(LINE)
    c.line(x + 24, y + 176, x + w - 24, y + 176)

    draw_brick(c, x + 36, y + 78)
    cable(c, x + 112, y + 98, x + 180, y + 98, BLUE, 6)
    draw_usbc(c, x + 214, y + 98, 1.15)
    label_stack(c, x + 214, y + 58, "ROUND USB-C", "(power only)", BLUE)

    # empty boot hole, pin set aside
    c.setFillColor(CHAR)
    c.circle(x + 360, y + 100, 7, fill=1, stroke=0)
    c.setStrokeColor(GOLD_D)
    c.setFillColor(GOLD)
    c.setLineWidth(2)
    c.line(x + 400, y + 70, x + 430, y + 96)
    c.circle(x + 396, y + 66, 4, fill=1, stroke=0)
    center(c, "pinhole BOOT", x + 430, y + 118, "B", 10, GOLD_D)
    center(c, "do not hold", x + 430, y + 104, "S", 9, MUTED)

    sentences(c, [
        "When the tool is finished, unplug the round power and the PC cable.",
        "Do not hold BOOT.",
        "Plug a USB ethernet dongle into the same hole, and connect it to your router.",
        "Plug the round power back in. Read the IP on the round screen.",
        "Wi-Fi stays off while that ethernet link is up.",
    ], 36, 148, 540, 12.5, 15, 4)


# --- step 6 ---------------------------------------------------------------

def page_open(c, a):
    step_header(c, 6, "Open the page", 7)
    x, y, w, h = 32, 168, 548, 498
    card(c, x, y, w, h, 16, fill=SOFT)

    # browser chrome
    rr(c, x + 28, y + 250, w - 56, 210, 12, fill=white, stroke=LINE, sw=1.1)
    c.setFillColor(C("e7e0d4"))
    c.circle(x + 52, y + 434, 5, fill=1, stroke=0)
    c.circle(x + 68, y + 434, 5, fill=1, stroke=0)
    c.circle(x + 84, y + 434, 5, fill=1, stroke=0)
    rr(c, x + 104, y + 418, w - 160, 32, 16, fill=CREAM, stroke=LINE, sw=0.8)
    text(c, "https://", x + 118, y + 428, "B", 13, GREEN)
    text(c, "IP from the screen", x + 176, y + 428, "B", 13, INK)

    text(c, "user", x + 64, y + 384, "S", 10, MUTED)
    rr(c, x + 64, y + 348, 200, 28, 6, fill=white, stroke=INK, sw=1)
    text(c, "admin", x + 76, y + 356, "B", 14, INK)
    text(c, "password", x + 290, y + 384, "S", 10, MUTED)
    rr(c, x + 290, y + 348, 200, 28, 6, fill=white, stroke=INK, sw=1)
    text(c, "admin", x + 302, y + 356, "B", 14, INK)
    center(c, "admin  /  admin", x + w / 2, y + 312, "B", 16, INK)
    center(c, "The browser may warn you. Continue.", x + w / 2, y + 286, "S", 10, MUTED)

    # later, one quiet strip inside the same picture
    rr(c, x + 28, y + 28, w - 56, 150, 12, fill=white, stroke=LINE, sw=1)
    text(c, "LATER", x + 48, y + 148, "B", 11, GOLD_D)
    text(c, "Upload a .aup", x + 48, y + 118, "B", 16, INK)
    para(c, "On the Upgrade page. Keep the .kdimg you just wrote.", x + 48, y + 96, 280, "S", 11, MUTED, 14)
    rr(c, x + 350, y + 78, 150, 64, 8, fill=RED_S, stroke=RED, sw=1)
    center(c, "Not Mujina", x + 425, y + 112, "B", 13, RED)
    center(c, "if it will not start,", x + 425, y + 94, "S", 8, MUTED)
    center(c, "write the .kdimg again", x + 425, y + 82, "S", 8, MUTED)

    sentences(c, [
        "From a computer on the same network, open https:// and the IP on the screen.",
        "Log in with admin and admin.",
        "Next time, upload a .aup on the Upgrade page.",
        "If the miner will not start, write this same .kdimg again. Not Mujina.",
    ], 36, 148, 540, 12.5, 15, 4)


def main():
    assets = load_assets()
    c = canvas.Canvas(STAGING, pagesize=(W, H))
    c.setTitle("Pelish on your Nano 3s")
    c.setAuthor("Pelish")
    c.setCreator("Pelish")
    c.setSubject(
        "Unplug the Wi-Fi dongle, hold BOOT, write your .kdimg, "
        "then plug the ethernet dongle into the same hole."
    )
    for fn in (page_cover, page_holes, page_file, page_cable, page_write, page_lan, page_open):
        fn(c, assets)
        c.showPage()
    c.save()
    os.replace(STAGING, OUT)
    print(OUT, os.path.getsize(OUT))


if __name__ == "__main__":
    main()
