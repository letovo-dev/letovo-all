import flask
import json, os
import requests
from datetime import datetime
import hashlib
import uuid

# docker build -f dockerfile.uploader -t flask-uploader:latest .

app = flask.Flask(__name__)
app.config['MAX_CONTENT_LENGTH'] = 100 * 1024 * 1024 * 10


ROOT_PATH = "/app/pages"
current_path = os.path.dirname(os.path.abspath(__file__))
with open(os.path.join(current_path, 'UploaderConfig.json'), 'r') as f:
    config = json.load(f)
MAX_AVATAR_SIZE = int(config.get("max_avatar_size", 5 * 1024 * 1024))

def api_get_upload_capabilities(token: str, cookie: str = ""):
    if not config["check_admin"]:
        return {"status": "t", "avatar_status": "t", "username": "local"}
    if not token and not cookie:
        return None
    auth_url = config.get("auth_check_url", "https://letovocorp.ru/letovo-api/auth/amiuploader")
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
        if result.get("status") not in {"t", "f"} or result.get("avatar_status") not in {"t", "f"}:
            return None
        if not isinstance(result.get("username"), str) or not result["username"]:
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

@app.route('/', methods=['POST'])
def upload_file():
    if 'file' not in flask.request.files:
        return "No file part", 400
    file = flask.request.files['file']
    if file.filename == '':
        return "No selected file", 400
    file.filename.replace(" ", "_")
    extention = file.filename.split('.')[-1].lower()
    file.filename = hashlib.md5(file.filename.encode() + str(datetime.now()).encode()).hexdigest() + "." + extention
    upload_category = config["supported"].get(extention, "other")
    file_path = os.path.join(ROOT_PATH, config["paths"][upload_category])
    token = flask.request.headers.get('Bearer', None)
    if not api_check_admin(token, flask.request.headers.get('Cookie', '')):
        return "Forbidden", 403
    
    file.save(os.path.join(file_path, file.filename))

    return '{"file": "/' + str(os.path.join(config["paths"][upload_category], file.filename)) + '"}'

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
    if capabilities is None or capabilities.get("avatar_status") != "t":
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
