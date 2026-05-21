#!/usr/bin/env python3
"""
Unity 解包资源 → 标准格式转换工具

将 APK 解包后的资源标准化为项目约定的 assets/ 目录结构。
直接保留原始的 DynaMaker XML 谱面（不转换为二进制格式）。
"""

import argparse
import json
import os
import shutil
import sys
from pathlib import Path


def normalize_song(src_dir: Path, dst_dir: Path, song_id: str):
    \"\"\"将单个歌曲目录标准化\"\"\"
    dst = dst_dir / song_id
    dst.mkdir(parents=True, exist_ok=True)

    # 查找源文件
    xml_files = list(src_dir.glob(""*.xml""))
    cover_files = list(src_dir.glob(""cover.*"")) + list(src_dir.glob(""*.png"")) + list(src_dir.glob(""*.jpg""))
    music_files = list(src_dir.glob(""music.*"")) + list(src_dir.glob(""*.mp3"")) + list(src_dir.glob(""*.ogg""))
    preview_files = list(src_dir.glob(""preview.*""))
    info_file = src_dir / ""info.json""

    # 复制封面
    if cover_files:
        shutil.copy2(cover_files[0], dst / ""cover.png"")

    # 复制音频
    if music_files:
        ext = music_files[0].suffix
        shutil.copy2(music_files[0], dst / f""bgm{ext}"")

    if preview_files:
        ext = preview_files[0].suffix
        shutil.copy2(preview_files[0], dst / f""preview{ext}"")

    # 读取元数据
    meta = {
        ""id"": song_id,
        ""title"": song_id,
        ""artist"": ""Unknown"",
        ""bpm"": 120.0,
        ""duration_sec"": 120,
        ""difficulties"": {}
    }
    if info_file.exists():
        try:
            info = json.loads(info_file.read_text(encoding='utf-8'))
            meta[""title""] = info.get(""musicName"", song_id)
            meta[""artist""] = info.get(""musicComposer"", ""Unknown"")
            meta[""noter""] = info.get(""noterName"", ""Unknown"")
            diffs = info.get(""difficulties"", {})
            meta[""difficulties""] = {k: {""level"": int(v), ""constant"": float(v)} for k, v in diffs.items()}
        except Exception as e:
            print(f""[warn] Failed to parse {info_file}: {e}"")

    # 直接复制 XML 谱面（不转换二进制，ChartParser 原生支持 XML）
    for xml in xml_files:
        diff_name = xml.stem  # e.g., ""1"", ""HARD"", ""GIGA""
        shutil.copy2(xml, dst / f""{diff_name}.xml"")
        print(f""[xml] {xml.name} -> {dst.name}/{diff_name}.xml"")

    (dst / ""metadata.json"").write_text(
        json.dumps(meta, indent=2, ensure_ascii=False), encoding='utf-8')
    print(f""[normalize] {song_id} -> {dst}"")


def main():
    parser = argparse.ArgumentParser(description=""Normalize Unity unpacked assets to standard format"")
    parser.add_argument(""--input"", required=True, help=""Input directory (unpacked assets)"")
    parser.add_argument(""--output"", required=True, help=""Output directory (standard assets/)"")
    args = parser.parse_args()

    src = Path(args.input)
    dst = Path(args.output)
    dst.mkdir(parents=True, exist_ok=True)

    songs_dst = dst / ""songs""
    songs_dst.mkdir(parents=True, exist_ok=True)

    # 查找 BuiltInSets 目录（APK解包结构）
    builtin_sets = src / ""BuiltInSets""
    if builtin_sets.exists():
        for song_dir in builtin_sets.iterdir():
            if song_dir.is_dir():
                normalize_song(song_dir, songs_dst, song_dir.name)
    else:
        # 直接处理输入目录
        xml_files = list(src.glob(""*.xml""))
        info_file = src / ""info.json""
        if xml_files or info_file.exists():
            normalize_song(src, songs_dst, src.name)
        else:
            for subdir in src.iterdir():
                if subdir.is_dir() and (list(subdir.glob(""*.xml"")) or (subdir / ""info.json"").exists()):
                    normalize_song(subdir, songs_dst, subdir.name)

    print(""[done] Normalization complete"")


if __name__ == ""__main__"":
    main()