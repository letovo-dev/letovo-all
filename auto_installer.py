# auto installer to check all configurations and install all dependencies for sub-projects
import os
import json
import subprocess

# colors

DEFAULT = '\033[0m'
BOLD = '\033[1m'
ITALIC = '\033[3m'
UNDERLINE = '\033[4m'
UNDERLINE_THICK = '\033[21m'
HIGHLIGHTED = '\033[7m'
HIGHLIGHTED_BLACK = '\033[40m'
HIGHLIGHTED_RED = '\033[41m'
HIGHLIGHTED_GREEN = '\033[42m'
HIGHLIGHTED_YELLOW = '\033[43m'
HIGHLIGHTED_BLUE = '\033[44m'
HIGHLIGHTED_PURPLE = '\033[45m'
HIGHLIGHTED_CYAN = '\033[46m'
HIGHLIGHTED_GREY = '\033[47m'
HIGHLIGHTED_GREY_LIGHT = '\033[100m'
HIGHLIGHTED_RED_LIGHT = '\033[101m'
HIGHLIGHTED_GREEN_LIGHT = '\033[102m'
HIGHLIGHTED_YELLOW_LIGHT = '\033[103m'
HIGHLIGHTED_BLUE_LIGHT = '\033[104m'
HIGHLIGHTED_PURPLE_LIGHT = '\033[105m'
HIGHLIGHTED_CYAN_LIGHT = '\033[106m'
HIGHLIGHTED_WHITE_LIGHT = '\033[107m'
STRIKE_THROUGH = '\033[9m'
MARGIN_1 = '\033[51m'
MARGIN_2 = '\033[52m'
BLACK = '\033[30m'
RED_DARK = '\033[31m'
GREEN_DARK = '\033[32m'
YELLOW_DARK = '\033[33m'
BLUE_DARK = '\033[34m'
PURPLE_DARK = '\033[35m'
CYAN_DARK = '\033[36m'
GREY_DARK = '\033[37m'
BLACK_LIGHT = '\033[90m'
RED = '\033[91m'
GREEN = '\033[92m'
YELLOW = '\033[93m'
BLUE = '\033[94m'
PURPLE = '\033[95m'
CYAN = '\033[96m'
WHITE = '\033[97m'
echo = lambda values, color: (
    print("%s%s%s" % (color, values, DEFAULT)) if color else print("%s%s" % (values, DEFAULT))
)  # noqa


with open('./BuildConfig.json') as f:
    config = json.load(f)


exit_code = 0
print(YELLOW + 'installing dependencies for sub-projects' + GREEN)

for project in config['required_sub_repos']:
    if not os.path.exists(os.path.join("src", project)):
        print(RED + "\nerror! missing sub-repo: " + project)
        exit_code = 1
    else:
        try:
            subprocess.run(["python3", "-m", "venv", "./venv"])
            if os.path.exists(os.path.join("src", project, "requirements.txt")):
                subprocess.run(["./venv/bin/pip3", "install", "-r", os.path.join("src", project, "requirements.txt")])
            elif os.path.exists(os.path.join("src", project, "auto_installer.py")):
                subprocess.run(["./venv/bin/python3", os.path.join("src", project, "auto_installer.py")])
            else:
                print(YELLOW + "warning! missing requirements.txt for sub-repo: " + project)
        except Exception as e:
            pass

print(WHITE + "\nchecking config files")

with open("./src/SqlConnectionConfig.json") as f:
    sql_config = json.load(f)

if "default" in sql_config["password"]:
    print(RED + "\nerror! please change the default password in SqlConnectionConfig.json")
    exit_code = 1

with open('./src/letovo-secrets/src/config.json') as f:
    bot_config = json.load(f)

if bot_config['token'] == '':
    print(
        RED
        + "\nerror! token missing in bot config!\n"
        + WHITE
        + "hint: you can add placeholder to bypass this error if you dont have an intention to use bot"
    )
    exit_code = 1

if exit_code == 0:
    print(WHITE + "all dependencies are installed")
else:
    print(RED + "\nerror! please check the logs above\n")

exit(exit_code)
