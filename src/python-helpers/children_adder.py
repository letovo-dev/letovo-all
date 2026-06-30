import pandas as pd
import psycopg2 as postgres
import csv
import requests
import urllib3

urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

ADMIN_TOKEN = input("Enter admin token: ")

def file_by_dep(dep):
    return f"/images/achivements/dep/{dep}.png"

def file_no_dep(id):
    return f"/images/achivements/nodep/{id}.png"

con = postgres.connect(
    host="10.8.0.1",
    port=5432,
    user="scv",
    password=input("ur password: "),
    database="letovo_db",
)

cur = con.cursor()

def get_deps():
    cur.execute("select * from department;")
    rows = cur.fetchall()
    deps = {}
    for row in rows:
        deps[row[1].lower()] = row[0]
    return deps

DEPS = get_deps()

ROLES = {
    'стажер': 1, 
    'джун': 2,
    'мидл': 3,
    'сеньор': 4,
    'тимлид': 5,
    'старший': 0
}

def get_pswd_hash(pswd):
    r = requests.post(
        "http://localhost:8080/token_getter",
        json={"token": pswd},
        verify=False,
    )
    return r.text

def existing_usernames():
    cur.execute('SELECT username FROM \"user\";')
    return [row[0] for row in cur.fetchall()]

EXISTING_USERNAMES = existing_usernames()
    
print("deps:", DEPS)

def add_one(username, password, role, dep, rights):
    if username == '' or password == '':
        print(f"Username or password cannot be empty, user {username}.")
        return
    password_hash = get_pswd_hash(password)
    dep_id = DEPS.get(dep.lower(), None)
    role_id = ROLES.get(role.lower(), None)
    
    if dep_id is None or role_id is None:
        print(f"Department '{dep}' role '{role}' not found, user {username}.")
        return
    role_id = (role_id - 1) * 7 + dep_id
    if role == 'старший':
        role_id = 35 + dep_id
    if role_id > 44:
        role_id = 0
    if username in EXISTING_USERNAMES:
        cur.execute("""
            UPDATE "user" 
            SET passwdhash = %s, role = %s, userrights = %s, active = true
            WHERE username = %s;
        """, (password_hash, role_id, rights, username))
    else:
        try:
            cur.execute("""
            INSERT INTO "user" (username, passwdhash, role,times_visited, userrights) 
            VALUES (%s, %s, %s, %s, %s);
        """, (username, password_hash, role_id, 0, rights))
        except postgres.errors.UniqueViolation:
            print(f"User '{username}' already exists.")
            exit(1)
    

def read_children_csv(file_path):
    with open(file_path, 'r') as f:
        reader = csv.reader(f, delimiter=',')
        next(reader)  # Skip header
        for row in reader:
            if len(row) < 6:
                continue
            useless, useless2, username, password, dep, useless3, role = row[0], row[1], row[2], row[3], row[4].lower(), row[5].lower(), row[6].lower()
            # if dep == 'сотрудник':
            #     dep = ' '
            # if role == 'сотрудник':
            #     role = 'старший'
            add_one(username, password, role, dep, 'child')
    con.commit()

if __name__ == "__main__":
    file_path = input("Enter the path to the children CSV file: ")
    read_children_csv(file_path)
    print("Children data added successfully.")
    con.close()