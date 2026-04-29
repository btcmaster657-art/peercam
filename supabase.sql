-- ─────────────────────────────────────────────────────────────────────────────
-- PeerCam — Supabase schema
-- Safe to run multiple times (idempotent).
-- Disable "Confirm email" in Supabase Dashboard → Auth → Settings.
--
-- COEXISTENCE RULE:
--   This schema shares a Supabase project with another application.
--   Every object owned by PeerCam MUST be prefixed with "peercam_" —
--   tables, indexes, policies, and functions — so nothing ever collides
--   with the main app's tables (profiles, sessions, provider_devices, etc.).
--   Never drop or alter any table that does not start with "peercam_".
-- ─────────────────────────────────────────────────────────────────────────────

-- ── Drop PeerCam objects only (reverse dependency order) ─────────────────────
drop table if exists peercam_sessions       cascade;
drop table if exists peercam_relay_tokens   cascade;
drop table if exists peercam_provider_codes cascade;

-- ── peercam_provider_codes ────────────────────────────────────────────────────
-- One row per provider. The 10-digit code is what requesters type to connect.
-- Provider can enable/disable/refresh their code from the desktop app.
create table peercam_provider_codes (
  user_id    uuid        primary key references auth.users(id) on delete cascade,
  code       char(10)    not null unique,       -- e.g. '3847291056'
  enabled    boolean     not null default true, -- false = paused, relay rejects connections
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now()
);

create index peercam_provider_codes_code_idx
  on peercam_provider_codes(code) where enabled = true;

-- ── peercam_relay_tokens ──────────────────────────────────────────────────────
-- Short-lived tokens issued to desktop clients (provider and requester).
-- For requesters the join_code they want to connect to is stored here so
-- the relay auth endpoint can validate it server-side without trusting the client.
create table peercam_relay_tokens (
  token      text        primary key,
  user_id    uuid        not null references auth.users(id) on delete cascade,
  role       text        not null check (role in ('provider', 'requester')),
  join_code  char(10),                          -- set for requester tokens only
  expires_at timestamptz not null default (now() + interval '1 hour'),
  created_at timestamptz not null default now()
);

create index peercam_relay_tokens_user_idx    on peercam_relay_tokens(user_id);
create index peercam_relay_tokens_expires_idx on peercam_relay_tokens(expires_at);

-- ── peercam_sessions ──────────────────────────────────────────────────────────
-- One row per requester connection. Created at relay-token issue time,
-- updated by the relay when the session ends.
create table peercam_sessions (
  id                uuid        primary key default gen_random_uuid(),
  requester_user_id uuid        references auth.users(id) on delete set null,
  provider_user_id  uuid        references auth.users(id) on delete set null,
  join_code         char(10),
  relay_endpoint    text,
  bytes_used        bigint      not null default 0,
  status            text        not null default 'active' check (status in ('active', 'ended')),
  disconnect_reason text,
  started_at        timestamptz not null default now(),
  ended_at          timestamptz,
  created_at        timestamptz not null default now()
);

create index peercam_sessions_requester_idx on peercam_sessions(requester_user_id);
create index peercam_sessions_provider_idx  on peercam_sessions(provider_user_id);
create index peercam_sessions_status_idx    on peercam_sessions(status);

-- ── RLS ───────────────────────────────────────────────────────────────────────
alter table peercam_provider_codes enable row level security;
alter table peercam_relay_tokens   enable row level security;
alter table peercam_sessions       enable row level security;

-- Authenticated users can read/write their own provider code row
create policy "peercam_provider_codes_owner"
  on peercam_provider_codes
  for all
  using (auth.uid() = user_id)
  with check (auth.uid() = user_id);

-- relay_tokens and sessions are service-role only — no public policies

-- ── pg_cron: cleanup expired tokens (optional) ───────────────────────────────
-- Enable pg_cron in Supabase Dashboard → Database → Extensions first.
-- select cron.schedule('peercam-cleanup-relay-tokens', '0 * * * *', $$
--   delete from peercam_relay_tokens where expires_at < now();
-- $$);
