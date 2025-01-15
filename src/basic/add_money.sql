UPDATE "user"
SET balance = balance + "roles".payment
FROM "roles"
WHERE "user".role = "roles".roleid;