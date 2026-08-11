-- Issue #182: prevent duplicate economic/department roles without deleting history.
-- The migration fails closed if duplicates appeared after the audit snapshot.
DO $$
BEGIN
  IF EXISTS (
    SELECT 1
    FROM public.roles
    GROUP BY rolename, departmentid, rang, payment
    HAVING COUNT(*) > 1
  ) THEN
    RAISE EXCEPTION
      'roles natural key contains duplicates; review historical assignments before merging';
  END IF;

  IF NOT EXISTS (
    SELECT 1
    FROM pg_constraint
    WHERE conrelid = 'public.roles'::regclass
      AND conname = 'roles_natural_key_unique'
  ) THEN
    ALTER TABLE public.roles
      ADD CONSTRAINT roles_natural_key_unique
      UNIQUE NULLS NOT DISTINCT (rolename, departmentid, rang, payment);
  END IF;
END
$$;

-- Read-only audit report. Unused rows are candidates for business review, not deletion.
SELECT
  r.roleid,
  r.rolename,
  r.departmentid,
  r.rang,
  r.payment,
  COUNT(DISTINCT ur.username) AS historical_assignments,
  COUNT(DISTINCT ur.username) FILTER (WHERE u.active AND u.registered) AS active_registered_assignments
FROM public.roles r
LEFT JOIN public.useroles ur ON ur.roleid = r.roleid
LEFT JOIN public."user" u ON u.username = ur.username
GROUP BY r.roleid, r.rolename, r.departmentid, r.rang, r.payment
ORDER BY active_registered_assignments, historical_assignments, r.departmentid, r.rang, r.roleid;
