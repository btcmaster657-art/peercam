import { NextRequest, NextResponse } from 'next/server'
import { supabase, supabaseAnon } from '@/lib/supabase'

function randomCode(): string {
  // 10 numeric digits, no leading zero
  return String(Math.floor(1_000_000_000 + Math.random() * 9_000_000_000))
}

async function getUser(req: NextRequest) {
  const authHeader = req.headers.get('authorization') ?? ''
  const accessToken = authHeader.startsWith('Bearer ') ? authHeader.slice(7) : null
  if (!accessToken) return null
  const { data: { user } } = await supabaseAnon.auth.getUser(accessToken)
  return user ?? null
}

// GET — return current code (or null if none)
export async function GET(req: NextRequest) {
  const user = await getUser(req)
  if (!user) return NextResponse.json({ error: 'Unauthorized' }, { status: 401 })

  const { data } = await supabase
    .from('peercam_provider_codes')
    .select('code, enabled, updated_at')
    .eq('user_id', user.id)
    .single()

  return NextResponse.json(data ?? { code: null, enabled: false })
}

// POST — generate or refresh code (creates row if not exists)
export async function POST(req: NextRequest) {
  const user = await getUser(req)
  if (!user) return NextResponse.json({ error: 'Unauthorized' }, { status: 401 })

  // Keep generating until we get a unique code (collision is astronomically rare)
  let code = randomCode()
  for (let i = 0; i < 5; i++) {
    const { data: existing } = await supabase
      .from('peercam_provider_codes').select('user_id').eq('code', code).single()
    if (!existing) break
    code = randomCode()
  }

  const { data, error } = await supabase
    .from('peercam_provider_codes')
    .upsert({ user_id: user.id, code, enabled: true, updated_at: new Date().toISOString() })
    .select('code, enabled, updated_at')
    .single()

  if (error) return NextResponse.json({ error: error.message }, { status: 500 })
  return NextResponse.json(data)
}

// PATCH — enable or disable without changing the code
export async function PATCH(req: NextRequest) {
  const user = await getUser(req)
  if (!user) return NextResponse.json({ error: 'Unauthorized' }, { status: 401 })

  const { enabled } = await req.json()
  if (typeof enabled !== 'boolean')
    return NextResponse.json({ error: 'enabled (boolean) required' }, { status: 400 })

  const { data, error } = await supabase
    .from('peercam_provider_codes')
    .update({ enabled, updated_at: new Date().toISOString() })
    .eq('user_id', user.id)
    .select('code, enabled, updated_at')
    .single()

  if (error || !data) return NextResponse.json({ error: 'No code found — generate one first' }, { status: 404 })
  return NextResponse.json(data)
}
