"""画程序图标：邪恶比格的脸（表情包那种：眉毛压低、嘴角一边翘）。

输出 overlay/res/jdog.ico，含 256/64/48/32/16 五个尺寸。程序资源和托盘图标共用它。
只依赖 Pillow。用法：python tools/make_icon.py
"""

import math
import sys

from PIL import Image, ImageDraw

BROWN = (158, 92, 41, 255)
BROWN_DARK = (118, 66, 28, 255)
CREAM = (240, 232, 218, 255)
BLACK = (28, 22, 20, 255)
WHITE = (250, 250, 250, 255)
IRIS = (52, 30, 14, 255)
TONGUE = (222, 96, 110, 255)


def draw_face(size: int) -> Image.Image:
    s = size
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    u = s / 32.0  # 以 32 为基准的单位

    def box(x0, y0, x1, y1):
        return [x0 * u, y0 * u, x1 * u, y1 * u]

    # 两只垂耳（在头后面）
    d.rounded_rectangle(box(1.5, 9, 8.5, 27), radius=3.2 * u, fill=BROWN_DARK)
    d.rounded_rectangle(box(23.5, 9, 30.5, 27), radius=3.2 * u, fill=BROWN_DARK)
    # 头
    d.rounded_rectangle(box(5, 4, 27, 26), radius=7 * u, fill=BROWN)
    # 白色吻部：从眉心一条白线下来，到下巴铺开
    d.rounded_rectangle(box(14.2, 8, 17.8, 18), radius=1.6 * u, fill=CREAM)
    d.ellipse(box(9, 15, 23, 27), fill=CREAM)
    # 鼻头
    d.ellipse(box(13, 16.5, 19, 21.5), fill=BLACK)
    d.ellipse(box(14.2, 17.4, 16.2, 18.8), fill=(70, 62, 60, 255))
    # 坏笑的嘴：左边低右边翘
    d.arc(box(11.5, 19, 21.5, 26), start=200, end=330, fill=BLACK, width=max(1, int(1.3 * u)))
    d.line([13.4 * u, 23.6 * u, 12.6 * u, 24.9 * u], fill=BLACK, width=max(1, int(1.2 * u)))
    # 舌头尖
    d.ellipse(box(17.5, 22.6, 20.5, 25.2), fill=TONGUE)
    # 眼白
    d.ellipse(box(8.2, 9.2, 13.8, 14.8), fill=WHITE)
    d.ellipse(box(18.2, 9.2, 23.8, 14.8), fill=WHITE)
    # 瞳孔往一边瞟
    d.ellipse(box(10.6, 10.8, 13.4, 13.9), fill=IRIS)
    d.ellipse(box(20.6, 10.8, 23.4, 13.9), fill=IRIS)
    d.ellipse(box(12.1, 11.3, 13.0, 12.2), fill=WHITE)
    d.ellipse(box(22.1, 11.3, 23.0, 12.2), fill=WHITE)
    # 压低的眉毛：内侧低、外侧高，「邪恶」全靠这两笔
    w = max(1, int(1.8 * u))
    d.line([7.6 * u, 8.2 * u, 13.9 * u, 10.4 * u], fill=BROWN_DARK, width=w)
    d.line([18.1 * u, 10.4 * u, 24.4 * u, 8.2 * u], fill=BROWN_DARK, width=w)
    # 眉毛把眼白上沿盖掉一点（眯眼）
    d.polygon([(8.0 * u, 8.6 * u), (14.0 * u, 10.9 * u), (14.0 * u, 9.0 * u), (8.0 * u, 7.4 * u)], fill=BROWN)
    d.polygon([(24.0 * u, 8.6 * u), (18.0 * u, 10.9 * u), (18.0 * u, 9.0 * u), (24.0 * u, 7.4 * u)], fill=BROWN)
    return img


def main() -> int:
    sizes = [256, 64, 48, 32, 16]
    # 大图画完缩小到小尺寸，比直接在 16 px 上画干净。
    big = draw_face(256)
    frames = [big] + [big.resize((n, n), Image.LANCZOS) for n in sizes[1:]]
    out = "overlay/res/jdog.ico"
    frames[0].save(out, format="ICO", sizes=[(n, n) for n in sizes], append_images=frames[1:])
    big.save("overlay/res/jdog_preview.png")
    print("wrote", out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
