#!/usr/bin/env python3
"""Generates the panel and component SVGs in res/ for the Noiseminded plugin.

Rack's SVG renderer (nanosvg) cannot draw text, patterns or filters, so every label is
converted to a path here, and halftones are drawn as individual dots.

Usage:  python3 tools/make_panels.py      (needs: pip install fonttools)
"""

import math
import os
import random

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(ROOT, "res")
FONTS = os.path.join(RES, "fonts")

INK = "#111111"
PAPER = "#efede7"

HEIGHT = 128.5
HP = 5.08


# ---------------------------------------------------------------- text as paths

class Font:
	def __init__(self, filename):
		self.tt = TTFont(os.path.join(FONTS, filename))
		self.glyphs = self.tt.getGlyphSet()
		self.cmap = self.tt.getBestCmap()
		self.upem = self.tt["head"].unitsPerEm
		self.hmtx = self.tt["hmtx"]

	def width(self, text, size, spacing=0.0):
		s = size / self.upem
		w = 0.0
		for ch in text:
			name = self.cmap.get(ord(ch))
			if name:
				w += self.hmtx[name][0] * s + spacing
		return w - spacing if text else 0.0

	def path(self, text, x, y, size, anchor="middle", spacing=0.0):
		"""SVG path data for `text` with its baseline at y. size = em size in mm."""
		s = size / self.upem
		w = self.width(text, size, spacing)
		if anchor == "middle":
			x -= w / 2
		elif anchor == "end":
			x -= w
		d = []
		for ch in text:
			name = self.cmap.get(ord(ch))
			if not name:
				continue
			pen = SVGPathPen(self.glyphs)
			self.glyphs[name].draw(TransformPen(pen, (s, 0, 0, -s, x, y)))
			d.append(pen.getCommands())
			x += self.hmtx[name][0] * s + spacing
		return " ".join(d)


ANTON = Font("Anton-Regular.ttf")
TYPE = Font("SpecialElite-Regular.ttf")
STENCIL = Font("BlackOpsOne-Regular.ttf")


def text(font, txt, x, y, size, fill=INK, anchor="middle", spacing=0.0, transform=None):
	t = f' transform="{transform}"' if transform else ""
	return f'<path d="{font.path(txt, x, y, size, anchor, spacing)}" fill="{fill}"{t}/>'


# ---------------------------------------------------------------- punk elements

def dymo(txt, x, y, size=2.6, rotate=0.0, inverse=False):
	"""Embossed label-maker tape: black strip, white typewriter letters, centred on (x, y)."""
	bg, fg = (INK, PAPER) if not inverse else (PAPER, INK)
	w = TYPE.width(txt, size, 0.25) + 2.4
	h = size * 1.15
	out = [f'<g transform="rotate({rotate} {x} {y})">']
	out.append(f'<rect x="{x - w / 2}" y="{y - h / 2}" width="{w}" height="{h}" fill="{bg}"/>')
	out.append(text(TYPE, txt, x, y + size * 0.33, size, fg, spacing=0.25))
	out.append("</g>")
	return "".join(out)


def ransom(chars, x, y, size, rotations):
	"""Cut-out letters of mismatched styles, like a ransom note. Each char gets its own box."""
	out = []
	styles = [(PAPER, INK, ANTON), (INK, PAPER, STENCIL), (PAPER, INK, STENCIL), (INK, PAPER, ANTON)]
	cx = x
	for i, ch in enumerate(chars):
		bg, fg, font = styles[i % len(styles)]
		cw = max(font.width(ch, size), size * 0.28) + size * 0.32
		ch_h = size * 0.95
		rot = rotations[i % len(rotations)]
		mx, my = cx + cw / 2, y
		out.append(f'<g transform="rotate({rot} {mx} {my})">')
		edge = PAPER if bg == INK else INK
		out.append(f'<rect x="{cx}" y="{y - ch_h / 2}" width="{cw}" height="{ch_h}" fill="{bg}" stroke="{edge}" stroke-width="0.45"/>')
		out.append(text(font, ch, mx, y + size * 0.36, size, fg))
		out.append("</g>")
		cx += cw + 0.5
	return "".join(out)


def tape(x, y, length, width, angle):
	"""A strip of masking tape with torn zigzag ends, centred on (x, y)."""
	rnd = random.Random(int(x * 100 + y))
	pts = []
	half = length / 2
	n = 5
	for i in range(n + 1):  # left torn edge, top to bottom
		pts.append((-half + rnd.uniform(-0.5, 0.5), -width / 2 + width * i / n))
	for i in range(n + 1):  # right torn edge, bottom to top
		pts.append((half + rnd.uniform(-0.5, 0.5), width / 2 - width * i / n))
	p = " ".join(f"{px:.2f},{py:.2f}" for px, py in pts)
	lines = "".join(
		f'<line x1="{-half + 1.5}" y1="{-width / 2 + width * k / 4}" x2="{half - 1.5}" y2="{-width / 2 + width * k / 4}" stroke="{INK}" stroke-width="0.12" stroke-opacity="0.5"/>'
		for k in (1, 2, 3))
	return (f'<g transform="translate({x} {y}) rotate({angle})">'
		f'<polygon points="{p}" fill="{PAPER}" stroke="{INK}" stroke-width="0.35"/>{lines}</g>')


def halftone(x0, y0, x1, y1, step, rmax, fade="up", fill=INK):
	"""A field of dots that shrink in one direction, like a photocopied gradient."""
	out = []
	rows = int((y1 - y0) / step) + 1
	cols = int((x1 - x0) / step) + 1
	for r in range(rows):
		for c in range(cols):
			x = x0 + c * step + (step / 2 if r % 2 else 0)
			y = y0 + r * step
			if x > x1:
				continue
			if fade == "up":
				t = (y - y0) / max(y1 - y0, 1e-6)
			elif fade == "down":
				t = 1 - (y - y0) / max(y1 - y0, 1e-6)
			elif fade == "left":
				t = (x - x0) / max(x1 - x0, 1e-6)
			else:
				t = 1 - (x - x0) / max(x1 - x0, 1e-6)
			rad = rmax * t
			if rad > 0.08:
				out.append(f'<circle cx="{x:.2f}" cy="{y:.2f}" r="{rad:.2f}" fill="{fill}"/>')
	return "".join(out)


def halftone_vignette(x0, y0, x1, y1, step, rmax, fill=INK):
	"""Dots that grow towards the edges of a box, leaving its centre clear."""
	out = []
	cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
	rows = int((y1 - y0) / step) + 1
	cols = int((x1 - x0) / step) + 1
	for r in range(rows):
		for c in range(cols):
			x = x0 + c * step + (step / 2 if r % 2 else 0)
			y = y0 + r * step
			if x > x1:
				continue
			d = math.hypot((x - cx) / ((x1 - x0) / 2), (y - cy) / ((y1 - y0) / 2))
			t = min(max((d - 0.55) / 0.45, 0.0), 1.0)
			rad = rmax * t
			if rad > 0.08:
				out.append(f'<circle cx="{x:.2f}" cy="{y:.2f}" r="{rad:.2f}" fill="{fill}"/>')
	return "".join(out)


def knob_ticks(x, y, radius, count=11, length=1.1):
	"""Tick marks around a knob over the usual 300° range."""
	out = []
	for i in range(count):
		a = math.radians(-150 + 300 * i / (count - 1))
		x0, y0 = x + radius * math.sin(a), y - radius * math.cos(a)
		x1, y1 = x + (radius + length) * math.sin(a), y - (radius + length) * math.cos(a)
		out.append(f'<line x1="{x0:.2f}" y1="{y0:.2f}" x2="{x1:.2f}" y2="{y1:.2f}" stroke="{INK}" stroke-width="0.35"/>')
	return "".join(out)


def scribble_underline(x0, x1, y, seed=1):
	rnd = random.Random(seed)
	pts = []
	n = 14
	for i in range(n + 1):
		pts.append(f"{x0 + (x1 - x0) * i / n:.2f},{y + rnd.uniform(-0.45, 0.45):.2f}")
	return f'<polyline points="{" ".join(pts)}" fill="none" stroke="{INK}" stroke-width="0.5" stroke-linejoin="round"/>'


def brand_strip(width):
	"""Halftone fading up from the bottom edge, with the brand on a cut-out label."""
	out = [halftone(0, 119.5, width, HEIGHT + 0.5, 1.15, 0.55, fade="up")]
	w = ANTON.width("NOISEMINDED", 3.2, 0.3) + 3
	out.append(f'<rect x="{width / 2 - w / 2}" y="121.1" width="{w}" height="4.6" fill="{PAPER}" stroke="{INK}" stroke-width="0.4"/>')
	out.append(text(ANTON, "NOISEMINDED", width / 2, 124.65, 3.2, INK, spacing=0.3))
	return "".join(out)


def svg(width, body):
	return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}mm" height="{HEIGHT}mm" '
		f'viewBox="0 0 {width} {HEIGHT}">{body}</svg>\n')


# ---------------------------------------------------------------- Surround Panner 7.1 (16 HP)

def surround_panner():
	W = 16 * HP  # 81.28
	b = [f'<rect width="{W}" height="{HEIGHT}" fill="{PAPER}"/>']

	# Title band
	b.append(f'<polygon points="0,0 {W},0 {W},14.6 {W * 0.62},15.6 {W * 0.3},14.2 0,15.2" fill="{INK}"/>')
	b.append(text(ANTON, "SURROUND PANNER", 8.0, 11.6, 6.6, PAPER, anchor="start", spacing=0.15))
	b.append(ransom("7.1", 61.2, 10.4, 6.0, [-7, 5, -3]))

	# Room display window (the widget draws inside it) with tape on the corners
	b.append(f'<rect x="5" y="18.5" width="{W - 10}" height="38" fill="{INK}"/>')
	b.append(tape(7.2, 19.6, 11, 3.6, -38))
	b.append(tape(W - 7.2, 19.6, 11, 3.6, 38))
	b.append(text(TYPE, "top view", W - 7, 55.2, 2.3, PAPER, anchor="end"))

	# Knobs
	for x, label, r, rot in [(16, "AZIMUTH", 6.6, -2), (40.64, "SPREAD", 5.3, 1.5), (65.28, "LFE SEND", 5.3, -1)]:
		b.append(knob_ticks(x, 69, r))
		b.append(dymo(label, x, 60.6, rotate=rot))

	# CV row
	for x, label in [(9.5, "cv"), (21.5, "amt"), (34, "cv"), (46, "amt")]:
		b.append(text(TYPE, label, x, 78.4, 2.6, INK))
	b.append(dymo("IN", 66.04, 78.0, size=2.8, rotate=-3))

	# Output block
	b.append(f'<polygon points="3.6,90.2 {W - 3.4},89.6 {W - 3.8},119 3.9,119.4" fill="{INK}"/>')
	jx = [13.97, 31.75, 49.53, 67.31]
	for i, (top, bottom) in enumerate(zip(["L", "R", "C", "LFE"], ["BL", "BR", "SL", "SR"])):
		b.append(text(ANTON, top, jx[i], 96.4, 3.6, PAPER))
		b.append(text(TYPE, str(i + 1), jx[i] + 5.3, 96.2, 2.2, PAPER))
		b.append(text(ANTON, bottom, jx[i], 109.5, 3.6, PAPER))
		b.append(text(TYPE, str(i + 5), jx[i] + 5.3, 109.3, 2.2, PAPER))

	b.append(brand_strip(W))
	return svg(W, "".join(b))


# ---------------------------------------------------------------- Headphone Monitor 7.1 (10 HP)

def headphones(cx, cy, s=1.0):
	"""Headphones drawn with thick strokes."""
	return (
		f'<path d="M {cx - 9 * s} {cy + 3 * s} A {9 * s} {9.5 * s} 0 0 1 {cx + 9 * s} {cy + 3 * s}" fill="none" stroke="{INK}" stroke-width="{1.6 * s}" stroke-linecap="round"/>'
		f'<rect x="{cx - 11.2 * s}" y="{cy + 1.5 * s}" width="{4.6 * s}" height="{8 * s}" rx="{1.2 * s}" fill="{INK}"/>'
		f'<rect x="{cx + 6.6 * s}" y="{cy + 1.5 * s}" width="{4.6 * s}" height="{8 * s}" rx="{1.2 * s}" fill="{INK}"/>'
		f'<rect x="{cx - 7.4 * s}" y="{cy + 2.6 * s}" width="{1.6 * s}" height="{5.8 * s}" fill="{INK}"/>'
		f'<rect x="{cx + 5.8 * s}" y="{cy + 2.6 * s}" width="{1.6 * s}" height="{5.8 * s}" fill="{INK}"/>'
	)


def headphone_monitor():
	W = 10 * HP  # 50.8
	b = [f'<rect width="{W}" height="{HEIGHT}" fill="{PAPER}"/>']

	# Title band, two lines
	b.append(f'<polygon points="0,0 {W},0 {W},20.4 {W * 0.55},21.4 0,20.2" fill="{INK}"/>')
	b.append(text(ANTON, "HEADPHONE", 5.6, 11.4, 6.0, PAPER, anchor="start", spacing=0.12))
	b.append(text(ANTON, "MONITOR", 5.6, 18.4, 6.0, PAPER, anchor="start", spacing=0.12))
	b.append(ransom("7.1", 31.5, 19.4, 5.6, [6, -5, 4]))

	# Headphones on a halftone burst
	b.append(halftone_vignette(3.2, 23.2, W - 3.2, 44.6, 1.05, 0.5))
	b.append(headphones(W / 2, 30.4, 1.05))
	b.append(text(TYPE, "preview the room", W / 2, 46.8, 2.4, INK))
	b.append(scribble_underline(W / 2 - 13, W / 2 + 13, 47.9, seed=7))

	# Inputs, in 7.1 order
	b.append(dymo("FROM YOUR PATCH", W / 2, 52.4, size=2.4, rotate=-1.5))
	cols = [14.0, 36.8]
	pairs = [("L", "1", "R", "2"), ("C", "3", "LFE", "4"), ("BL", "5", "BR", "6"), ("SL", "7", "SR", "8")]
	for r, (a, na, c, nc) in enumerate(pairs):
		ly = 59.0 + r * 11.6
		b.append(text(ANTON, a, cols[0] - 6.4, ly + 1.3, 3.4, INK, anchor="end"))
		b.append(text(TYPE, na, cols[0] - 6.4, ly + 4.0, 1.9, INK, anchor="end"))
		b.append(text(ANTON, c, cols[1] + 6.4, ly + 1.3, 3.4, INK, anchor="start"))
		b.append(text(TYPE, nc, cols[1] + 6.4, ly + 4.0, 1.9, INK, anchor="start"))
	b.append(f'<line x1="{W / 2}" y1="55.2" x2="{W / 2}" y2="100.4" stroke="{INK}" stroke-width="0.35" stroke-dasharray="1.2 0.8"/>')

	# Output block
	b.append(f'<polygon points="3.4,102.6 {W - 3.2},102.2 {W - 3.6},119 3.7,119.3" fill="{INK}"/>')
	b.append(text(ANTON, "L", 10.6, 107.4, 3.2, PAPER))
	b.append(text(ANTON, "R", W - 10.6, 107.4, 3.2, PAPER))
	b.append(text(TYPE, "level", W / 2, 107.2, 2.3, PAPER))

	b.append(brand_strip(W))
	return svg(W, "".join(b))


# ---------------------------------------------------------------- components

def component(size_mm, body):
	return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{size_mm}mm" height="{size_mm}mm" '
		f'viewBox="0 0 {size_mm} {size_mm}">{body}</svg>\n')


def knob(size, stroke):
	c = size / 2
	r = c - stroke / 2 - 0.05
	return component(size,
		f'<circle cx="{c}" cy="{c}" r="{r}" fill="{PAPER}" stroke="{INK}" stroke-width="{stroke}"/>'
		f'<circle cx="{c}" cy="{c}" r="{r * 0.55}" fill="none" stroke="{INK}" stroke-width="{stroke * 0.45}"/>'
		f'<line x1="{c}" y1="{c - r * 0.15}" x2="{c}" y2="{c - r + stroke * 0.2}" stroke="{INK}" stroke-width="{stroke * 1.1}" stroke-linecap="round"/>')


def jack(dark):
	size = 8.0
	c = size / 2
	if dark:
		body = (f'<circle cx="{c}" cy="{c}" r="3.6" fill="{INK}" stroke="{PAPER}" stroke-width="0.7"/>'
			f'<circle cx="{c}" cy="{c}" r="2.45" fill="{PAPER}"/>'
			f'<circle cx="{c}" cy="{c}" r="1.55" fill="{INK}"/>')
	else:
		body = (f'<circle cx="{c}" cy="{c}" r="3.6" fill="{PAPER}" stroke="{INK}" stroke-width="0.7"/>'
			f'<circle cx="{c}" cy="{c}" r="2.45" fill="{INK}"/>'
			f'<circle cx="{c}" cy="{c}" r="1.55" fill="{PAPER}"/>'
			f'<circle cx="{c}" cy="{c}" r="1.05" fill="{INK}"/>')
	return component(size, body)


def screw():
	size = 5.08
	c = size / 2
	return component(size,
		f'<circle cx="{c}" cy="{c}" r="2.1" fill="{PAPER}" stroke="{INK}" stroke-width="0.4"/>'
		f'<line x1="{c - 1.1}" y1="{c - 1.1}" x2="{c + 1.1}" y2="{c + 1.1}" stroke="{INK}" stroke-width="0.5"/>'
		f'<line x1="{c - 1.1}" y1="{c + 1.1}" x2="{c + 1.1}" y2="{c - 1.1}" stroke="{INK}" stroke-width="0.5"/>')


def main():
	os.makedirs(os.path.join(RES, "components"), exist_ok=True)
	files = {
		"SurroundPanner71.svg": surround_panner(),
		"HeadphoneMonitor71.svg": headphone_monitor(),
		"components/KnobLarge.svg": knob(12.2, 0.9),
		"components/Knob.svg": knob(9.6, 0.8),
		"components/Trimpot.svg": knob(6.0, 0.6),
		"components/Jack.svg": jack(dark=False),
		"components/JackDark.svg": jack(dark=True),
		"components/Screw.svg": screw(),
	}
	for name, content in files.items():
		with open(os.path.join(RES, name), "w") as f:
			f.write(content)
		print("wrote res/" + name)


if __name__ == "__main__":
	main()
