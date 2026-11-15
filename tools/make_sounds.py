# -*- coding: utf-8 -*-
"""
sounds フォルダの音声ファイル（mp3 / wav / ogg / flac）を、スケッチに組み込める sounds.h に変換する。

使い方:  make_sounds.bat をダブルクリック（または  py tools\\make_sounds.py ）
準備:    py -m pip install miniaudio

ファイル名（拡張子の前）で、どの場面の音かが決まる。下の EVENTS を参照。
ファイルを置かなかった場面は、スケッチが作る音がそのまま鳴る。
"""
import os
import sys

# (ファイル名, 説明, 最大の長さ[秒])  ※ 並び順は ArrowDefense.ino の SampleId と同じにすること
EVENTS = [
    ("start", "ゲーム開始", 1.5),
    ("fire", "ミサイル発射／ポンのサーブ", 0.4),
    ("hit", "矢印を破壊（コンボで音が高くなる）", 0.5),
    ("armor", "アーマーに当たった", 0.5),
    ("miss", "色ちがいのミス", 0.6),
    ("damage", "基地にダメージ", 0.8),
    ("heal", "HP回復", 1.0),
    ("levelup", "レベルアップ", 1.5),
    ("bomb", "レインボーで全部破壊", 1.5),
    ("gameover", "ゲームオーバー", 2.5),
    ("record", "ハイスコア更新／ポンの勝利", 2.5),
    ("warning", "ボス警告", 2.5),
    ("boss_crash", "ボスが基地に衝突", 1.5),
    ("boss_clear", "ボス撃破", 2.5),
    ("rhythm_miss", "リズムモードのミス", 0.4),
    ("pong_point", "ポンの得点", 1.0),
]
RATE = 22050  # 1秒あたりのサンプル数（1秒 = 約22KB）
EXTS = (".mp3", ".wav", ".ogg", ".flac")
BUDGET = 800 * 1024  # 合計がこれを超えるとボードに入らないおそれ

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_DIR = os.path.join(ROOT, "sounds")
OUT = os.path.join(ROOT, "sounds.h")


def mulaw(s):
    """16ビットの値を1バイトに圧縮（小さい音ほど細かく記録する方式）"""
    sign = 0x80 if s < 0 else 0
    s = min(abs(s), 32635) + 0x84
    exp, mask = 7, 0x4000
    while exp > 0 and not (s & mask):
        exp -= 1
        mask >>= 1
    return ~(sign | (exp << 4) | ((s >> (exp + 3)) & 0x0F)) & 0xFF


def convert(path, max_sec):
    import miniaudio

    dec = miniaudio.decode_file(path, output_format=miniaudio.SampleFormat.SIGNED16, nchannels=1, sample_rate=RATE)
    s = list(dec.samples)
    peak = max((abs(x) for x in s), default=0)
    if peak < 50:
        return None  # ほぼ無音
    # 前後の無音を切る
    th = peak * 0.01
    a = next(i for i, x in enumerate(s) if abs(x) > th)
    b = len(s) - next(i for i, x in enumerate(reversed(s)) if abs(x) > th)
    s = s[a:b]
    cut = len(s) > int(max_sec * RATE)
    s = s[: int(max_sec * RATE)]
    # 音量をそろえる（最大を 90% に）
    peak = max(abs(x) for x in s)
    g = 0.9 * 32767 / peak
    s = [x * g for x in s]
    # プチッという雑音を防ぐため、出だしと終わりをなめらかに
    fin = min(int(0.003 * RATE), len(s))
    fout = min(int((0.08 if cut else 0.01) * RATE), len(s))
    for i in range(fin):
        s[i] *= i / fin
    for i in range(fout):
        s[-1 - i] *= i / fout
    return bytes(mulaw(int(x)) for x in s), cut


def main():
    try:
        import miniaudio  # noqa: F401
    except ImportError:
        print("miniaudio が入っていません。次のコマンドを実行してから、もう一度ためしてください:")
        print("    py -m pip install miniaudio")
        return 1

    os.makedirs(SRC_DIR, exist_ok=True)
    out = [
        "// 自動で作られたファイル（tools/make_sounds.py）。手で書きかえないこと。",
        "// 音声素材そのものを含むので、このファイルをインターネットに公開・配布しないこと。",
        "#pragma once",
        "#include <Arduino.h>",
        f"#define SOUNDS_COUNT {len(EVENTS)}",
        f"#define SOUNDS_RATE {RATE}",
        "struct SampleInfo {",
        "  const uint8_t* data;",
        "  uint32_t len;",
        "};",
    ]
    table, total, found = [], 0, 0
    print(f"音声フォルダ: {SRC_DIR}\n")
    for i, (name, desc, max_sec) in enumerate(EVENTS):
        path = next((os.path.join(SRC_DIR, name + e) for e in EXTS if os.path.exists(os.path.join(SRC_DIR, name + e))), None)
        if not path:
            print(f"  --  {name:<12} {desc}  … ファイルなし（スケッチの音）")
            table.append("  { nullptr, 0 },")
            continue
        try:
            res = convert(path, max_sec)
        except Exception as e:  # 壊れたファイルなど
            print(f"  NG  {name:<12} 読めませんでした: {e}")
            table.append("  { nullptr, 0 },")
            continue
        if res is None:
            print(f"  NG  {name:<12} 無音のファイルです")
            table.append("  { nullptr, 0 },")
            continue
        data, cut = res
        found += 1
        total += len(data)
        note = f"（長いので {max_sec} 秒で切りました）" if cut else ""
        print(f"  OK  {name:<12} {desc}  … {len(data) / RATE:.2f} 秒 / {len(data) // 1024 + 1} KB {note}")
        out.append(f"static const uint8_t SMPD_{i}[] PROGMEM = {{  // {os.path.basename(path)}")
        for k in range(0, len(data), 24):
            out.append("  " + ",".join(str(v) for v in data[k : k + 24]) + ",")
        out.append("};")
        table.append(f"  {{ SMPD_{i}, sizeof(SMPD_{i}) }},")

    out.append("static const SampleInfo SAMPLES[SOUNDS_COUNT] = {")
    out += table
    out.append("};")
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")

    print(f"\n{found} 個の音を sounds.h に書き出しました（合計 {total // 1024 + 1} KB）")
    if total > BUDGET:
        print("⚠ 合計が大きすぎて、ボードに入らないかもしれません。長い音を短いものに替えてください。")
    if found == 0:
        print("sounds フォルダに、上の名前（例: fire.mp3）で音声ファイルを入れてください。")
    print("このあと Arduino IDE でスケッチを書き込み直すと、音が変わります。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
