import { NextRequest, NextResponse } from 'next/server'
import { supabase } from '@/lib/supabase'

export async function POST(req: NextRequest) {
  if ((req.headers.get('x-relay-secret') ?? '') !== process.env.RELAY_SECRET)
    return NextResponse.json({ error: 'Unauthorized' }, { status: 401 })

  const { token, role } = await req.json()
  if (!token || !role)
    return NextResponse.json({ error: 'Missing token or role' }, { status: 400 })

  const { data, error } = await supabase
    .from('peercam_relay_tokens')
    .select('user_id, role, join_code, expires_at')
    .eq('token', token)
    .single()

  if (error || !data)
    return NextResponse.json({ error: 'Invalid token' }, { status: 401 })
  if (new Date(data.expires_at) < new Date())
    return NextResponse.json({ error: 'Token expired' }, { status: 401 })
  if (data.role !== role)
    return NextResponse.json({ error: 'Role mismatch' }, { status: 401 })

  // Provider: fetch their authoritative join code from DB — never trust client-sent value
  let joinCode: string | null = data.join_code ?? null
  if (role === 'provider') {
    const { data: codeRow } = await supabase
      .from('peercam_provider_codes')
      .select('code, enabled')
      .eq('user_id', data.user_id)
      .single()
    if (!codeRow || !codeRow.enabled)
      return NextResponse.json({ error: 'No active join code' }, { status: 403 })
    joinCode = codeRow.code
  }

  // Requester: join_code was validated at token-issue time; just return it
  if (role === 'requester' && !joinCode)
    return NextResponse.json({ error: 'No join code on token' }, { status: 401 })

  return NextResponse.json({ ok: true, userId: data.user_id, joinCode })
}
