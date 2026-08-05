BEGIN;

ALTER TABLE public.transactions
  ADD COLUMN IF NOT EXISTS reason character varying NOT NULL DEFAULT 'wire transfer';

COMMIT;
