# Security deployment checklist

Apply `docs/security_sessions_migration.sql` to the production database before
starting a binary built from the security hardening PR.

The migration creates:

- `public.user_sessions` for opaque cookie-backed auth sessions.
- password hash metadata columns on `public."user"`.
- `public.post_reveal_tokens` for admin-generated secret post reveal links and
  QR codes.

Do not deploy the new backend binary before this migration succeeds. Login,
registration, session validation, and admin-generated reveal links depend on
these database objects.
