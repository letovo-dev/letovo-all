import flask
from werkzeug.exceptions import RequestEntityTooLarge
import json, os
import logging
import subprocess
import threading
import requests
from datetime import datetime
import hashlib
import uuid

# docker build -f dockerfile.uploader -t flask-uploader:latest .

app = flask.Flask(__name__)
ROOT_PATH = "/app/pages"
current_path = os.path.dirname(os.path.abspath(__file__))
with open(os.path.join(current_path, 'UploaderConfig.json'), 'r') as f:
    config = json.load(f)
MAX_AVATAR_SIZE = int(config.get("max_avatar_size", 5 * 1024 * 1024))



def _video_setting(name, default):
    value = os.environ.get("UPLOADER_VIDEO_" + name.upper(), config.get("video", {}).get(name, default))
    if isinstance(default, bool):
        return str(value).lower() in {"1", "true", "yes", "on"}
    return int(value)


VIDEO_MAX_BYTES = _video_setting("max_bytes", 524288000)
VIDEO_MAX_DURATION = _video_setting("max_duration_seconds", 900)
VIDEO_MAX_WIDTH = _video_setting("max_width", 3840)
VIDEO_MAX_HEIGHT = _video_setting("max_height", 2160)
VIDEO_PROBE_TIMEOUT = _video_setting("probe_timeout_seconds", 30)
VIDEO_REMUX_TIMEOUT = _video_setting("remux_timeout_seconds", 300)
VIDEO_MOV_ENABLED = _video_setting("mov_input_enabled", True)
VIDEO_JOBS = threading.BoundedSemaphore(_video_setting("max_concurrent_jobs", 2))
# Allow only the small multipart envelope beyond the configured media limit.
app.config['MAX_CONTENT_LENGTH'] = VIDEO_MAX_BYTES + 1024 * 1024


def _video_error(status, code, message):
    return flask.jsonify(code=code, message=message), status


def _probe(path, timeout):
    completed = subprocess.run(
        ["ffprobe", "-v", "error", "-print_format", "json", "-show_format", "-show_streams", path],
        capture_output=True, text=True, timeout=timeout, check=False, shell=False)
    if completed.returncode != 0:
        raise ValueError("ffprobe failed")
    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise ValueError("invalid ffprobe output") from error


def _validate_video_probe(probe, require_mp4=False):
    formats = set(str(probe.get("format", {}).get("format_name", "")).split(","))
    if not ({"mov", "mp4"} & formats):
        return 415, "unsupported_video_container", "Видео должно быть в контейнере MOV/MP4."
    if require_mp4 and probe.get("format", {}).get("tags", {}).get("major_brand") not in {"isom", "iso2", "mp41", "mp42", "avc1"}:
        return 415, "invalid_remuxed_video", "Обработанное видео не является MP4."
    streams = probe.get("streams")
    if not isinstance(streams, list):
        return 415, "invalid_video", "Видео не удалось распознать."
    videos = [stream for stream in streams if stream.get("codec_type") == "video"]
    audios = [stream for stream in streams if stream.get("codec_type") == "audio"]
    if len(videos) != 1:
        return 415, "unsupported_video_streams", "Видео должно содержать один видеопоток."
    video = videos[0]
    if video.get("codec_name") != "h264":
        return 415, "unsupported_video_codec", "Видео использует неподдерживаемый кодек. Экспортируйте его как MP4: H.264, звук AAC."
    if video.get("pix_fmt") not in {"yuv420p", "yuvj420p"}:
        return 415, "unsupported_video_pixel_format", "Поддерживается только 8-битное видео H.264 4:2:0."
    if len(audios) > 1 or (audios and (audios[0].get("codec_name") != "aac" or audios[0].get("profile") != "LC")):
        return 415, "unsupported_audio_codec", "Поддерживается только звук AAC LC."
    try:
        width, height = int(video["width"]), int(video["height"])
        duration = float(probe.get("format", {}).get("duration", video.get("duration", 0)))
    except (KeyError, TypeError, ValueError):
        return 415, "invalid_video", "Видео не удалось распознать."
    if duration <= 0:
        return 415, "invalid_video", "Видео не удалось распознать."
    if duration > VIDEO_MAX_DURATION or width > VIDEO_MAX_WIDTH or height > VIDEO_MAX_HEIGHT:
        return 422, "video_limits_exceeded", "Видео превышает допустимую длительность или разрешение."
    return None


def _save_normalized_mov(file):
    if not VIDEO_JOBS.acquire(blocking=False):
        response, status = _video_error(429, "video_queue_busy", "Очередь обработки видео занята. Попробуйте позже.")
        response.headers["Retry-After"] = "30"
        return response, status
    incoming = output_part = None
    try:
        incoming_dir = os.path.join(ROOT_PATH, config["paths"]["videos"], ".incoming")
        os.makedirs(incoming_dir, exist_ok=True)
        identifier = uuid.uuid4().hex
        incoming = os.path.join(incoming_dir, identifier + ".mov")
        output_part = os.path.join(incoming_dir, identifier + ".mp4.part")
        relative = os.path.join(config["paths"]["videos"], identifier + ".mp4").replace(os.sep, "/")
        final = os.path.join(ROOT_PATH, relative)
        os.makedirs(os.path.dirname(final), exist_ok=True)
        file.save(incoming)
        if os.path.getsize(incoming) > VIDEO_MAX_BYTES:
            return _video_error(413, "video_too_large", "Размер видео превышает допустимый лимит.")
        try:
            error = _validate_video_probe(_probe(incoming, VIDEO_PROBE_TIMEOUT))
        except (OSError, subprocess.TimeoutExpired, ValueError) as exc:
            logging.warning("MOV probe failed: %s", exc)
            return _video_error(415, "invalid_video", "Видео не удалось распознать. Экспортируйте его как MP4: H.264, звук AAC.")
        if error:
            return _video_error(*error)
        try:
            completed = subprocess.run(
                ["ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-y", "-i", incoming,
                 "-map", "0:v:0", "-map", "0:a:0?", "-map_metadata", "0", "-c", "copy",
                 "-movflags", "+faststart", "-f", "mp4", output_part],
                capture_output=True, text=True, timeout=VIDEO_REMUX_TIMEOUT, check=False, shell=False)
            if completed.returncode != 0 or not os.path.isfile(output_part) or os.path.getsize(output_part) == 0:
                raise ValueError("ffmpeg failed")
            error = _validate_video_probe(_probe(output_part, VIDEO_PROBE_TIMEOUT), require_mp4=True)
            if error:
                return _video_error(*error)
        except (OSError, subprocess.TimeoutExpired, ValueError) as exc:
            logging.exception("MOV remux failed: %s", exc)
            return _video_error(500, "video_processing_failed", "Не удалось обработать видео. Попробуйте снова.")
        os.replace(output_part, final)
        output_part = None
        return flask.jsonify(file="/" + relative, normalized=True)
    finally:
        for path in (incoming, output_part):
            if path:
                try:
                    os.remove(path)
                except FileNotFoundError:
                    pass
        VIDEO_JOBS.release()

def api_get_upload_capabilities(token: str, cookie: str = ""):
    if not config["check_admin"]:
        return {"status": "t", "avatar_status": "t", "username": "local"}
    if not token and not cookie:
        return None
    auth_url = os.environ.get("UPLOADER_CAPABILITIES_URL") or config.get(
        "auth_check_url", "https://letovocorp.ru/letovo-api/auth/amiuploader")
    try:
        headers = {}
        if token:
            headers["Bearer"] = token
        if cookie:
            headers["Cookie"] = cookie
        r = requests.get(auth_url, headers=headers, timeout=5)
        if r.status_code != 200:
            return None
        result = r.json()
        if not isinstance(result, dict):
            return None
        if result.get("status") not in {"t", "f"}:
            return None
        return result
    except (requests.RequestException, ValueError, TypeError, KeyError):
        return None

def api_check_admin(token: str, cookie: str = ""):
    capabilities = api_get_upload_capabilities(token, cookie)
    return capabilities is not None and capabilities.get("status") == "t"

def _detect_image_extension(data: bytes):
    if data.startswith(b"\x89PNG\r\n\x1a\n"):
        return "png"
    if data.startswith(b"\xff\xd8\xff"):
        return "jpg"
    if len(data) >= 12 and data[:4] == b"RIFF" and data[8:12] == b"WEBP":
        return "webp"
    return None

@app.errorhandler(RequestEntityTooLarge)
def request_too_large(_error):
    return _video_error(413, "upload_too_large", "Размер файла превышает допустимый лимит.")


@app.route('/', methods=['POST'])
def upload_file():
    # Do not trigger Werkzeug multipart parsing/spooling before authorization and
    # the declared-size limit have been evaluated.
    token = flask.request.headers.get('Bearer', None)
    if not api_check_admin(token, flask.request.headers.get('Cookie', '')):
        return "Forbidden", 403
    if flask.request.content_length and flask.request.content_length > VIDEO_MAX_BYTES + 1024 * 1024:
        return _video_error(413, "video_too_large", "Размер видео превышает допустимый лимит.")
    if 'file' not in flask.request.files:
        return "No file part", 400
    file = flask.request.files['file']
    if file.filename == '':
        return "No selected file", 400
    extension = file.filename.rsplit('.', 1)[-1].lower() if '.' in file.filename else ""
    if extension == "mov":
        if not VIDEO_MOV_ENABLED:
            return _video_error(415, "mov_upload_disabled", "Загрузка MOV временно отключена.")
        return _save_normalized_mov(file)
    filename = hashlib.md5(file.filename.encode() + str(datetime.now()).encode()).hexdigest() + "." + extension
    category = config["supported"].get(extension, "other")
    file_path = os.path.join(ROOT_PATH, config["paths"][category])
    os.makedirs(file_path, exist_ok=True)
    file.save(os.path.join(file_path, filename))
    return flask.jsonify(file="/" + os.path.join(config["paths"][category], filename).replace(os.sep, "/"))


@app.route('/avatar', methods=['POST'])
def upload_avatar():
    if flask.request.content_length and flask.request.content_length > MAX_AVATAR_SIZE + 1024 * 1024:
        return flask.jsonify(error="Файл аватара слишком большой"), 413
    if 'file' not in flask.request.files:
        return "No file part", 400
    file = flask.request.files['file']
    if file.filename == '':
        return "No selected file", 400
    token = flask.request.headers.get('Bearer', None)
    capabilities = api_get_upload_capabilities(
        token, flask.request.headers.get('Cookie', ''))
    if (capabilities is None or capabilities.get("avatar_status") != "t" or
            not isinstance(capabilities.get("username"), str) or not capabilities["username"]):
        return "Forbidden", 403
    supplied_extension = file.filename.rsplit('.', 1)[-1].lower() if '.' in file.filename else ""
    if supplied_extension not in {"png", "jpg", "jpeg", "webp"}:
        return flask.jsonify(error="Поддерживаются только PNG, JPEG и WebP"), 400
    data = file.read(MAX_AVATAR_SIZE + 1)
    if not data:
        return flask.jsonify(error="Файл аватара пуст"), 400
    if len(data) > MAX_AVATAR_SIZE:
        return flask.jsonify(error="Файл аватара слишком большой"), 413
    extension = _detect_image_extension(data)
    if extension is None or (extension == "jpg" and supplied_extension not in {"jpg", "jpeg"}) or (extension != "jpg" and extension != supplied_extension):
        return flask.jsonify(error="Содержимое файла не является допустимым изображением"), 400
    user_key = hashlib.sha256(capabilities["username"].encode("utf-8")).hexdigest()
    relative_dir = os.path.join(config.get("personal_ava_path", "images/personal_avatars"), user_key)
    file_path = os.path.join(ROOT_PATH, relative_dir)
    os.makedirs(file_path, exist_ok=True)
    filename = uuid.uuid4().hex + "." + extension
    with open(os.path.join(file_path, filename), "wb") as target:
        target.write(data)
    return flask.jsonify(file="/" + os.path.join(relative_dir, filename).replace(os.sep, "/"))

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=8880, debug=False, threaded=True, use_reloader=False)
