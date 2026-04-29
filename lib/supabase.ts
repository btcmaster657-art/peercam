import { createClient } from '@supabase/supabase-js'

const url        = (process.env.NEXT_PUBLIC_SUPABASE_URL        ?? process.env.SUPABASE_URL)!
const anonKey    = (process.env.NEXT_PUBLIC_SUPABASE_ANON_KEY   ?? process.env.SUPABASE_ANON_KEY)!
const serviceKey = (process.env.SUPABASE_SERVICE_ROLE            ?? process.env.SUPABASE_SERVICE_ROLE_KEY)!

// Server-side admin client (never exposed to browser)
export const supabase = createClient(url, serviceKey)

// Auth operations that need the anon key
export const supabaseAnon = createClient(url, anonKey)
