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

def no_dep_achivements():
    with open('Achivments.csv', 'r') as f:
        reader = csv.reader(f, delimiter=',')
        next(reader)  # Skip header
        for row in reader:
            if len(row) < 4:
                continue
            id, name, description, stages = row[0], row[1], row[2], row[3]
            cur.execute(
                "INSERT INTO achivements (achivement_name, achivement_decsription, departmentid, stages, achivement_pic) VALUES (%s, %s, -1, %s, %s)",
                (name, description, stages, file_no_dep(id))
            )
        con.commit()

def dep_achivements():
    with open('Dep_Achivments.csv', 'r') as f:
        reader = csv.reader(f, delimiter=',')
        next(reader)  # Skip header
        for row in reader:
            if len(row) < 5:
                continue
            id, name, description, stages, dep = row[0], row[1], row[2], row[3], row[4]
            cur.execute(
                "INSERT INTO achivements (achivement_name, achivement_decsription, departmentid, stages, achivement_pic) VALUES (%s, %s, %s, %s, %s)",
                (name, description, id[-1], stages, file_by_dep(id))
            )
        con.commit()

def roles():
    roles = {
        "Старший": 0
    }
    i = 1
    for role, exp in roles.items():
        for dep_id in range(1, 8):
            cur.execute(
                "INSERT INTO roles (rolename, rang, departmentid, payment) VALUES (%s, %s, %s, %s)",
                (role, i, dep_id, exp)
            )
        i += 1
    con.commit()

def elder_users():
    with open('teachers.csv', 'r') as f:
        elder_offset = 35
        reader = csv.reader(f, delimiter=',')
        next(reader)
        cur.execute("SELECT departmentid, departmentname FROM department")
        departments = {str(row[1]).lower(): row[0] for row in cur.fetchall()} 
        for row in reader:
            if len(row) < 4:
                continue
            useles_1, useles_2, name, password, dep_name, role = row[0], row[1], row[2], row[3], row[4], row[5]
            dep_name = dep_name.lower()
            dep_id = departments.get(dep_name)
            # if dep_name != 'дсо':
            #     continue
            if not dep_id:
                print(f"Department '{dep_name}' not found for user '{name}'. Skipping.")
                continue
            res = requests.post(
                "https://letovocorp.ru/api/auth/reg", 
                json={
                    "login": name,
                    "password": password
                },
                verify=False
            )
            if res.status_code != 200:
                print(f"Failed to create user {name} with login {name}: {res.text}")
                continue
            print(f"User {name} created, adding role {role}")
            requests.post(
                "https://letovocorp.ru/api/user/add_role",
                json={
                    "username": name,
                    "role_id": elder_offset + departments[dep_name]
                },
                headers={"Bearer": f"{ADMIN_TOKEN}"},
                verify=False
            )
            cur.execute(
                "update \"user\" set userrights = 'admin' where username = %s",
                (name,)
            )
            con.commit()
            print(f"Role {role} added to user {name} in department {dep_name}")
    
def children_users():
    with open("children.csv", 'r') as f:
        reader = csv.reader(f, delimiter=',')
        next(reader)
        cur.execute("SELECT departmentid, departmentname FROM department")
        departments = {str(row[1]).lower(): row[0] for row in cur.fetchall()} 
        for row in reader:
            if len(row) < 3:
                continue
            # print(row)
            name, useless, login, password, dep_name, counter, useless_2 = row[0], row[1], row[2], row[3], row[4], row[5].lower(), row[6]
            dep_id = departments.get(dep_name.lower())
            if not dep_id:
                print(f"Department '{dep_name}' not found for user '{name}'. Skipping.")
                continue
            res = requests.post(
                "https://letovocorp.ru/api/auth/reg", 
                json={
                    "login": login,
                    "password": password
                },
                verify=False
            )
            if res.status_code != 200:
                print(f"Failed to create user {name} with login {login}: {res.text}")
                continue
            print(f"User {name} created with login {login}, adding to department {dep_name}")
            requests.post(
                "https://letovocorp.ru/api/user/add_role",
                json={
                    "username": login,
                    "role_id": dep_id
                },
                headers={"Bearer": f"{ADMIN_TOKEN}"},
                verify=False
            )

def publishers():
    with open("publishers.csv", 'r') as f:
        reader = csv.reader(f, delimiter=',')
        next(reader)
        for row in reader:
            if len(row) < 3:
                continue
            name, password, login = row[0], row[1], row[2]
            requests.post(
                "https://letovocorp.ru/api/auth/reg", 
                json={
                    "login": login,
                    "password": password
                },
                verify=False
            )
            print(f"Publisher {name} created with login {login}")

def check():
    current_users = set()
    with open('admins_select.csv') as f:
        reader = csv.reader(f, delimiter=',')
        next(reader)
        for row in reader:
            current_users.add(row[1])

    with open('teachers.csv') as f:
        reader = csv.reader(f, delimiter=',')
        next(reader)
        for row in reader:
            if not current_users.__contains__(row[2]):
                print(f"User {row[2]} not found in admins_select.csv")

def check_children():
    current_users = []
    check = []
    with open('children_select.csv') as f:
        reader = csv.reader(f, delimiter=',')
        next(reader)
        for row in reader:
            current_users.append(row[1])
    with open('children.csv') as f:
        reader = csv.reader(f, delimiter=',')
        next(reader)
        for row in reader:
            if not row[2] in current_users:
                print(f"User {row[2]} not found in children_select.csv")

            check.append(row[2])

    print(len(check), len(current_users))
            # else:
            #     print(f"User {row[2]} found in children_select.csv")
    with open("dump.txt", "w") as f:
        f.write(str(sorted(check)) + "\n")
        f.write(str(sorted(current_users)) + "\n")

def get_publisher_ava_path(name):
    return f"/images/uploaded/publisher_avatars/{name}.png"

def publisher_avatars():
    with open("publishers.csv", 'r') as f:
        reader = csv.reader(f, delimiter=',')
        next(reader)
        for row in reader:
            cur.execute(
                f'UPDATE "user" set avatar_pic = \'{get_publisher_ava_path(row[0])}\' WHERE username = %s'
                , (row[2],)
            )
    con.commit()
        


if __name__ == "__main__":
    elder_users()

