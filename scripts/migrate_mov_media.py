#!/usr/bin/env python3
"""Safely remux legacy /docs/*.mov post media to /videos/uploaded/*.mp4.

Run inside the uploader image/host with ffmpeg, ffprobe and psycopg installed:
  migrate_mov_media.py --dsn "$DATABASE_URL" --media-root /srv/letovo/media --dry-run
  migrate_mov_media.py --dsn "$DATABASE_URL" --media-root /srv/letovo/media --apply
"""
import argparse, hashlib, json, os, shutil, subprocess, sys
from pathlib import Path


def run(command):
    return subprocess.run(command, capture_output=True, text=True, check=False, shell=False)


def probe(path):
    result = run(["ffprobe", "-v", "error", "-print_format", "json", "-show_format", "-show_streams", str(path)])
    if result.returncode: raise ValueError("ffprobe failed")
    data = json.loads(result.stdout)
    streams = data.get("streams", [])
    video = [s for s in streams if s.get("codec_type") == "video"]
    audio = [s for s in streams if s.get("codec_type") == "audio"]
    if len(video) != 1 or video[0].get("codec_name") != "h264" or video[0].get("pix_fmt") not in {"yuv420p", "yuvj420p"}:
        raise ValueError("video must be H.264 8-bit 4:2:0")
    if len(audio) > 1 or (audio and (audio[0].get("codec_name") != "aac" or audio[0].get("profile") != "LC")):
        raise ValueError("audio must be AAC LC")
    if float(data.get("format", {}).get("duration", 0)) <= 0: raise ValueError("invalid duration")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dsn", required=True)
    parser.add_argument("--media-root", required=True, type=Path)
    parser.add_argument("--backup-dir", type=Path, default=Path("mov-migration-backup"))
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--dry-run", action="store_true")
    mode.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    try:
        import psycopg
    except ImportError:
        sys.exit("psycopg is required; run this from an environment with psycopg installed")
    args.backup_dir.mkdir(parents=True, exist_ok=True)
    report = []
    with psycopg.connect(args.dsn) as db:
        rows = db.execute("SELECT post_id, media FROM post_media WHERE media LIKE '/docs/%.mov' ORDER BY post_id, media").fetchall()
        for post_id, media in rows:
            source = args.media_root / media.lstrip("/")
            stable_id = hashlib.sha256(media.encode()).hexdigest()
            target_rel = f"/videos/uploaded/{stable_id}.mp4"
            target = args.media_root / target_rel.lstrip("/")
            entry = {"post_id": post_id, "source": media, "target": target_rel}
            try:
                if not source.is_file(): raise ValueError("source file is missing")
                probe(source)
                entry["status"] = "would-remux" if args.dry_run else "remuxed"
                if args.apply:
                    backup = args.backup_dir / source.name
                    if not backup.exists(): shutil.copy2(source, backup)
                    target.parent.mkdir(parents=True, exist_ok=True)
                    part = target.with_suffix(".mp4.part")
                    result = run(["ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-y", "-i", str(source), "-map", "0:v:0", "-map", "0:a:0?", "-map_metadata", "0", "-c", "copy", "-movflags", "+faststart", "-f", "mp4", str(part)])
                    if result.returncode or not part.is_file() or not part.stat().st_size: raise ValueError("remux failed")
                    probe(part); os.replace(part, target)
                    db.execute("UPDATE post_media SET media = %s WHERE post_id = %s AND media = %s", (target_rel, post_id, media))
            except (OSError, ValueError, json.JSONDecodeError) as error:
                entry.update(status="skipped", error=str(error))
            report.append(entry)
        if args.dry_run: db.rollback()
    print(json.dumps(report, ensure_ascii=False, indent=2))

if __name__ == "__main__": main()
