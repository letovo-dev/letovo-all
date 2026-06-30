import telebot
import asyncio
import websockets
import json
import subprocess
import time
# docker build -f dockerfile.latency_bot -t latency_bot:latest .

TELEGRAM_BOT_TOKEN = CHAT_ID = DEBUG_CHAT_ID = MONITOR_WS_URL = ALLOWED_LATENCY = None

def load_config():
    global TELEGRAM_BOT_TOKEN, CHAT_ID, DEBUG_CHAT_ID, MONITOR_WS_URL, ALLOWED_LATENCY, SOCKET_ADRESS
    with open("/app/configs/latency_bot_config.json", 'r') as f:
        config = json.load(f)
        TELEGRAM_BOT_TOKEN = config['bot_token']
        CHAT_ID = config['chat_id']
        DEBUG_CHAT_ID = config.get('debug_chat_id', CHAT_ID)
        MONITOR_WS_URL = config['monitor_ws_url']
        ALLOWED_LATENCY = config.get('allowed_latency', 500)

load_config()
bot = telebot.TeleBot(TELEGRAM_BOT_TOKEN)
loop = asyncio.get_event_loop()

current_latency = {}
current_status = {}

async def listen_monitor():
    while True:
        try:
            async with websockets.connect(MONITOR_WS_URL) as websocket:
                print("Connected to monitor websocket, allowed latency:", ALLOWED_LATENCY)
                bot.send_message(DEBUG_CHAT_ID, "Connected to latency monitor websocket, allowed latency: " + str(ALLOWED_LATENCY))
                failed = False
                failed_request = None
                while True:
                    message = await websocket.recv()
                    data = json.loads(message)
                    points = data.get('data', [])
                    if points:
                        latest = points[-1]
                        latency = latest.get('latency', None)
                        req_info = latest.get('reqInfo', '').replace('https://letovocorp.ru/', '').replace('\u2192', '').split()[1]
                        status = latest.get('status', None)
                        if latency is None:
                            print("No latency data in the message:", latest)
                            continue
                        global current_latency
                        current_latency[req_info] = latency
                        global current_status
                        current_status[req_info] = status
                        if latency > ALLOWED_LATENCY or status is None or status >= 300 or status < 200:
                            alert = f'🚨 ALERT 🚨\n{req_info}\nLatency: {latency:.1f} ms\nstatus: {status}\nplots at http://sergei-scv.ru:8080/\n@Lunitarik'
                            if not failed and failed_request != req_info:
                                failed = True
                                failed_request = req_info
                                bot.send_message(CHAT_ID, alert)
                        elif latency > ALLOWED_LATENCY / 2 and not failed:
                            warning = f'⚠️ WARNING\n{req_info}\nLatency: {latency:.1f} ms'
                            if not failed and failed_request != req_info:
                                bot.send_message(CHAT_ID, warning)
                        elif failed and failed_request == req_info:
                            failed = False
                            failed_request = None
                            alert = f'✅ OK\n{req_info}\nLatency: {latency:.1f} ms'
                            bot.send_message(CHAT_ID, alert)
        except Exception as e:
            print(f"Error: {e}, reconnecting in 5s...")
            await asyncio.sleep(5)

@bot.message_handler(commands=['restart'])
def handle_restart(message: telebot.types.Message):
    if str(message.chat.id) != CHAT_ID:
        bot.reply_to(message, "⛔️ Unauthorized")
        return
    bot.reply_to(message, "🔄 Restarting the server...")
    subprocess.run(["docker-compose", "restart", "letovo-server"])
    time.sleep(5)
    logs = subprocess.run(["docker-compose", "logs", "--tail 10", "letovo-server"], capture_output=True, text=True)
    bot.send_message(CHAT_ID, f"Server restarted. Last logs:\n```\n{logs.stdout}\n```", parse_mode='MarkdownV2')
    
@bot.message_handler(commands=['status'])
def handle_status(message: telebot.types.Message):
    lines = []
    for url in current_latency.keys():
        latency = current_latency.get(url, 'n/a')
        status = current_status.get(url, 'n/a')
        msg = f"URL: {url} \n - Latency: {latency:.1f} ms \n - Status: {status}\n"
        if latency < ALLOWED_LATENCY and status is not None and status == 200:
            msg = "✅ " + msg
        elif latency >= ALLOWED_LATENCY / 2 or status < 300:
            msg = "⚠️ " + msg
        else:
            msg = "❌ " + msg
        lines.append(msg)

    output = "ℹ️ status:\n\n" + "\n".join(lines) + "\n\n" + "plots at http://sergei-scv.ru:8080/"

    bot.send_message(message.chat.id, output)

@bot.message_handler(commands=['config'])
def reread_config(message: telebot.types.Message):
    if str(message.chat.id) != CHAT_ID:
        bot.reply_to(message, "⛔️ Unauthorized")
        return
    load_config()
    bot.send_message(CHAT_ID, "🔄 Config reloaded")


if __name__ == '__main__':
    ws_task = loop.create_task(listen_monitor())
    import threading
    def run_bot():
        bot.infinity_polling()
    threading.Thread(target=run_bot, daemon=True).start()
    loop.run_forever()

