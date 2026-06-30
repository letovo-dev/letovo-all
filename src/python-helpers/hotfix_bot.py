import telebot
import hashlib
import os
import psycopg2 as postgres
import json

with open("/app/configs/telebot_token.txt", 'r') as f:
    TELEGRAM_TOKEN = f.read().strip()

bot = telebot.TeleBot(TELEGRAM_TOKEN)

with open("/app/configs/db_config.json", 'r') as f:
    db_config = json.load(f)

con = postgres.connect(
    host=db_config["host"],
    port=db_config["port"],
    user=db_config["user"],
    password=db_config["password"],
    database=db_config["database"],
)

cur = con.cursor()

allowed_users = [361380081, 763104478, 1175315313]
media_path = "/app/pages/videos/uploaded"

cur_photoes = {}

@bot.message_handler(commands=['start'])
def start(message):
    if message.from_user.id not in allowed_users:
        bot.send_message(message.chat.id, f"{message.from_user.id} is not allowed to use this bot, chao.")
        return
    bot.send_message(message.chat.id, "Welcome to the manual bot! use /add_publisher to add publisher or send me a photo to add avatar to somebody.")

@bot.message_handler(commands=['add_publisher'])
def add_publisher(message):
    if message.from_user.id not in allowed_users:
        bot.send_message(message.chat.id, "You are not allowed to use this command.")
        return
    if len(message.text.split()) < 2:
        bot.send_message(message.chat.id, "Usage: /add_publisher <username>")
        return
    username = ' '.join(message.text.split()[1:])
    cur.execute('INSERT INTO "user" (username, passwdhash, userrights) VALUES (%s, 0, \'public_author\')', (username,))
    con.commit()
    bot.send_message(message.chat.id, f"Publisher {username} added successfully.")


@bot.message_handler(content_types=['photo'])
def handle_photo(message: telebot.types.Message):
    if message.from_user.id not in allowed_users:
        bot.send_message(message.chat.id, "You are not allowed to use this bot.")
        return

    file_info = bot.get_file(message.photo[-1].file_id)
    downloaded_file = bot.download_file(file_info.file_path)

    file_name = os.path.join("/app/pages/images/uploaded", f"{message.photo[-1].file_id}.jpg")
    with open(file_name, 'wb') as f:
        f.write(downloaded_file)
    file_name = os.path.join("/images/uploaded", f"{message.photo[-1].file_id}.jpg")
    bot.send_message(message.chat.id, f"Photo saved successfully to {file_name}.")
    bot.send_message(message.chat.id, "Please provide the user ID this photo is for.")
    global cur_photoes
    cur_photoes[message.chat.id] = file_name
    bot.register_next_step_handler(message, handle_user_id)

def handle_user_id(response: telebot.types.Message):
    username = response.text.strip()
    cur.execute (
        'update "user" set avatar_pic = %s where username = %s', (cur_photoes[response.chat.id], username)
    )
    con.commit()
    del cur_photoes[response.chat.id]
    bot.send_message(response.chat.id, f"User ID {username} recorded for photo.")

try:
    bot.polling(none_stop=True)
except Exception as e:
    print(f"Bot polling failed: {e}")
    bot.stop_polling()