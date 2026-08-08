#!/usr/bin/env python3
"""Migrate compatible legacy /docs/*.mov post media without transcoding.

First run --dry-run; then use an explicit durable backup location:
  migrate_mov_media.py --dsn "$DATABASE_URL" --media-root /srv/letovo/media --dry-run
  migrate_mov_media.py --dsn "$DATABASE_URL" --media-root /srv/letovo/media --backup-dir /srv/letovo/mov-backup --apply
"""
import argparse, hashlib, json, os, shutil, subprocess, sys, uuid
from pathlib import Path


def command(args):
    return subprocess.run(args, capture_output=True, text=True, check=False, shell=False)


def probe(path, require_mp4=False):
    result = command(["ffprobe", "-v", "error", "-print_format", "json", "-show_format", "-show_streams", str(path)])
    if result.returncode: raise ValueError("ffprobe failed")
    data = json.loads(result.stdout); streams = data.get("streams", [])
    video = [s for s in streams if s.get("codec_type") == "video"]
    audio = [s for s in streams if s.get("codec_type") == "audio"]
    if len(video) != 1 or video[0].get("codec_name") != "h264" or video[0].get("pix_fmt") not in {"yuv420p", "yuvj420p"}:
        raise ValueError("video must be H.264 8-bit 4:2:0")
    if len(audio) > 1 or (audio and (audio[0].get("codec_name") != "aac" or audio[0].get("profile") != "LC")):
        raise ValueError("audio must be AAC LC")
    if float(data.get("format", {}).get("duration", 0)) <= 0: raise ValueError("invalid duration")
    if require_mp4 and data.get("format", {}).get("tags", {}).get("major_brand") not in {"isom", "iso2", "mp41", "mp42", "avc1"}:
        raise ValueError("remux result is not MP4")


def create_backup_run(backup_root):
    """Create an immutable per-run backup directory under an operator-owned root."""
    run_dir = backup_root / ("run-" + uuid.uuid4().hex)
    run_dir.mkdir(parents=True, exist_ok=False)
    return run_dir


def _contained(path, root):
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False


def safe_source(media_root, media):
    media_root = media_root.resolve()
    docs_root = (media_root / "docs").resolve()
    if not isinstance(media, str) or not media.startswith("/docs/"):
        raise ValueError("media path is outside /docs")
    source = (media_root / media.lstrip("/")).resolve()
    if not _contained(source, docs_root):
        raise ValueError("media path escapes media/docs")
    return source, media_root


def safe_backup_path(backup_run, media_root, source):
    backup_run = backup_run.resolve()
    target = (backup_run / "files" / source.relative_to(media_root)).resolve()
    if not _contained(target, backup_run):
        raise ValueError("backup path escapes backup run")
    return target


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dsn", required=True); parser.add_argument("--media-root", required=True, type=Path)
    parser.add_argument("--backup-dir", type=Path)
    mode = parser.add_mutually_exclusive_group(required=True); mode.add_argument("--dry-run", action="store_true"); mode.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    if args.apply and args.backup_dir is None: parser.error("--apply requires an explicit durable --backup-dir")
    import psycopg2
    report, created = [], []
    try:
        with psycopg2.connect(args.dsn) as db:
            with db.cursor() as cursor:
                cursor.execute("SELECT post_id, media FROM post_media WHERE media LIKE '/docs/%.mov' ORDER BY post_id, media FOR UPDATE")
                rows = cursor.fetchall()
                backup_run = None
                if args.apply:
                    backup_run = create_backup_run(args.backup_dir)
                    (backup_run / "post_media_rows.json").write_text(json.dumps([{"post_id": row[0], "media": row[1]} for row in rows], ensure_ascii=False, indent=2))
                for post_id, media in rows:
                    source, resolved_media_root = safe_source(args.media_root, media)
                    target_rel = "/videos/uploaded/" + hashlib.sha256(media.encode()).hexdigest() + ".mp4"
                    target = args.media_root / target_rel.lstrip("/"); part = target.with_suffix(".mp4.part")
                    entry = {"post_id": post_id, "source": media, "target": target_rel}
                    try:
                        if not source.is_file(): raise ValueError("source file is missing")
                        probe(source)
                        if args.dry_run: entry["status"] = "would-remux"; report.append(entry); continue
                        backup = safe_backup_path(backup_run, resolved_media_root, source)
                        backup.parent.mkdir(parents=True, exist_ok=True)
                        if not backup.exists(): shutil.copy2(source, backup)
                        target.parent.mkdir(parents=True, exist_ok=True)
                        result = command(["ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-y", "-i", str(source), "-map", "0:v:0", "-map", "0:a:0?", "-map_metadata", "0", "-c", "copy", "-movflags", "+faststart", "-f", "mp4", str(part)])
                        if result.returncode or not part.is_file() or not part.stat().st_size: raise ValueError("remux failed")
                        probe(part, require_mp4=True); os.replace(part, target); created.append(target)
                        cursor.execute("UPDATE post_media SET media = %s WHERE post_id = %s AND media = %s", (target_rel, post_id, media))
                        if cursor.rowcount != 1: raise ValueError("post_media row changed during migration")
                        entry["status"] = "remuxed"; entry["backup_run"] = str(backup_run)
                    except (OSError, ValueError, json.JSONDecodeError) as error:
                        entry.update(status="failed", error=str(error)); raise RuntimeError(json.dumps(entry, ensure_ascii=False))
                    finally:
                        part.unlink(missing_ok=True)
                    report.append(entry)
            if args.dry_run: db.rollback()
    except Exception as error:
        for target in created: target.unlink(missing_ok=True)
        print(json.dumps(report + [{"status": "failed", "error": str(error)}], ensure_ascii=False, indent=2)); return 1
    print(json.dumps(report, ensure_ascii=False, indent=2)); return 0

if __name__ == "__main__": sys.exit(main())
