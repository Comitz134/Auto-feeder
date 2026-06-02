-- Smart Pet Feeder cloud accounts/profile setup
-- Run this in Supabase SQL Editor.
-- Passwords are managed by Supabase Auth in auth.users. Do not store raw passwords yourself.

create table if not exists public.pet_profiles (
  user_id uuid primary key references auth.users(id) on delete cascade,
  profile jsonb not null default '{}'::jsonb,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now()
);

alter table public.pet_profiles enable row level security;

drop policy if exists "Users can read own pet profile" on public.pet_profiles;
create policy "Users can read own pet profile"
on public.pet_profiles
for select
to authenticated
using (auth.uid() = user_id);

drop policy if exists "Users can insert own pet profile" on public.pet_profiles;
create policy "Users can insert own pet profile"
on public.pet_profiles
for insert
to authenticated
with check (auth.uid() = user_id);

drop policy if exists "Users can update own pet profile" on public.pet_profiles;
create policy "Users can update own pet profile"
on public.pet_profiles
for update
to authenticated
using (auth.uid() = user_id)
with check (auth.uid() = user_id);

create or replace function public.set_updated_at()
returns trigger
language plpgsql
as $$
begin
  new.updated_at = now();
  return new;
end;
$$;

drop trigger if exists pet_profiles_set_updated_at on public.pet_profiles;
create trigger pet_profiles_set_updated_at
before update on public.pet_profiles
for each row
execute function public.set_updated_at();
