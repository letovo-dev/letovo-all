import flask
import json, os
import requests
from datetime import datetime
import hashlib

# docker build -f dockerfile.uploader -t flask-uploader:latest .

app = flask.Flask(__name__)
app.config['MAX_CONTENT_LENGTH'] = 100 * 1024 * 1024 * 10

ROOT_PATH = "/app/pages"
current_path = os.path.dirname(os.path.abspath(__file__))
with open(os.path.join(current_path, 'UploaderConfig.json'), 'r') as f:
    config = json.load(f)

def api_check_admin(token: str):
    if not config["check_admin"]:
        return True
    if not token:
        return False
    r = requests.get("https://letovocorp.ru/api/auth/amiadmin", headers={"Bearer": token}, verify=False)
    return json.loads(r.text)["status"] == 't'

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
    if extention in config["supported"]:
        file_path = os.path.join(ROOT_PATH, config["paths"][config["supported"][extention]])
    else: 
        file_path = os.path.join(ROOT_PATH, config["paths"]["other"])
    token = flask.request.headers.get('Bearer', None)
    if not api_check_admin(token):
        return f"You are not admin, your token: {token}", 403
    
    file.save(os.path.join(file_path, file.filename))

    return '{"file": "/' + str(os.path.join(config["paths"][config["supported"][extention]], file.filename)) + '"}'

@app.route('/avatar', methods=['POST'])
def upload_avatar():
    if 'file' not in flask.request.files:
        return "No file part", 400
    file = flask.request.files['file']
    if file.filename == '':
        return "No selected file", 400
    file_path = os.path.join(ROOT_PATH, config["ava_path"])
    token = flask.request.headers.get('Bearer', None)
    if not api_check_admin(token):
        return f"You are not admin, your token: {token}", 403
    file.filename.replace(" ", "_")
    file.filename = str(datetime.now().timestamp()).replace('.', '_') + "_" + file.filename
    file.save(os.path.join(file_path, file.filename))
    return '{"file": "/' + str(os.path.join(config["ava_path"], file.filename)) + '"}'

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=8880, debug=True, threaded=True, use_reloader=False)