ALTER TABLE public.role
ADD COLUMN IF NOT EXISTS whireable boolean DEFAULT false;
