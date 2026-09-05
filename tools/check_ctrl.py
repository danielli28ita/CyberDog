"""扫描文件里不该出现的控制字符。

只读，不改文件。规则见技能包 engineering/SKILL.md 第 6 节「多层转义」：
内容经过 shell 和 Python 字符串两层转义之后，反斜杠序列会变成退格、换行等
控制字符写进文件，肉眼看不出来。写完文件用这个脚本扫一遍。

用法：
    python tools/check_ctrl.py 文件1 文件2 ...
退出码 0 表示全部干净，1 表示至少一个文件有控制字符。
"""

import sys

# 制表符(9)、换行(10)、回车(13) 是正常的，其余 0x00–0x1F 都不该出现。
BAD = set(range(0x00, 0x09)) | {0x0B, 0x0C} | set(range(0x0E, 0x20))

NAMES = {0x08: "退格 \\b", 0x07: "响铃 \\a", 0x0B: "纵向制表 \\v", 0x0C: "换页 \\f", 0x1B: "ESC"}


def scan(path: str) -> int:
    data = open(path, "rb").read()
    hits = []
    line = 1
    for i, c in enumerate(data):
        if c == 0x0A:
            line += 1
        elif c in BAD:
            hits.append((line, i, c))
    if not hits:
        print(f"OK    {path}")
        return 0
    print(f"脏    {path}  共 {len(hits)} 处")
    for line_no, offset, c in hits[:10]:
        print(f"      行 {line_no}  字节偏移 {offset}  0x{c:02X} {NAMES.get(c, '')}")
    return 1


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    sys.stdout.reconfigure(encoding="utf-8")
    return 1 if any(scan(p) for p in sys.argv[1:]) else 0


if __name__ == "__main__":
    sys.exit(main())
