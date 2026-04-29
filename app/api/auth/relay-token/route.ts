import { NextRequest, NextResponse } from 'next/server'
import { supabase, supabaseAnon } from '@/lib/supabase'
import { pickRelay } from '@/lib/relay-endpoints'
import { randomBytes } from 'crypto'

export async function POST(req: NextRequest) {
  // Read body FIRST — stream can only be consumed once
  const body = await req.json().catch(() => ({}))

  const authHeader = req.headers.get('authorization') ?? ''
  const accessToken = authHeader.startsWith('Bearer ') ? authHeader.slice(7) : null
  if (!accessToken)
    return NextResponse.json({ error: 'Missing token' }, { status: 401 })

  const { data: { user }, error } = await supabaseAnon.auth.getUser(accessToken)
  if (error || !user)
    return NextResponse.json({ error: 'Invalid session' }, { status: 401 })

  const role: 'provider' | 'requester' = body.role === 'requester' ? 'requester' : 'provider'
  const joinCode: string | null = body.joinCode ?? null

  if (role === 'requester' && (!joinCode || joinCode.length !== 10))
    return NextResponse.json({ error: 'joinCode (10 digits) required for requester' }, { status: 400 })

  // Verify code is enabled before issuing token — fail fast before touching relay
  if (role === 'requester') {
    const { data: codeRow } = await supabase
      .from('peercam_provider_codes')
      .select('enabled')
      .eq('code', joinCode)
      .single()
    if (!codeRow || !codeRow.enabled)
      return NextResponse.json({ error: 'Join code not found or disabled' }, { status: 404 })
  }

  // For provider: verify they have an active code before issuing token
  if (role === 'provider') {
    const { data: codeRow } = await supabase
      .from('peercam_provider_codes')
      .select('code, enabled')
      .eq('user_id', user.id)
      .single()
    if (!codeRow || !codeRow.enabled)
      return NextResponse.json({ error: 'Enable your join code before connecting' }, { status: 403 })
  }

  const token = randomBytes(32).toString('hex')
  const expiresAt = new Date(Date.now() + 60 * 60 * 1000).toISOString()

  const { error: insertError } = await supabase.from('peercam_relay_tokens').insert({
    token,
    user_id:    user.id,
    role,
    join_code:  role === 'requester' ? joinCode : null,
    expires_at: expiresAt,
  })
  if (insertError)
    return NextResponse.json({ error: 'Failed to create relay token' }, { status: 500 })

  // Pre-create session row for requesters
  let dbSessionId: string | null = null
  if (role === 'requester') {
    const { data: sessionRow } = await supabase
      .from('peercam_sessions')
      .insert({ requester_user_id: user.id, status: 'active', join_code: joinCode })
      .select('id')
      .single()
    dbSessionId = sessionRow?.id ?? null
  }

  const relayUrl = await pickRelay()
  return NextResponse.json({ token, userId: user.id, relayUrl, dbSessionId, role })
}
