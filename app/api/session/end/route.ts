import { NextRequest, NextResponse } from 'next/server'
import { supabase } from '@/lib/supabase'

function checkSecret(req: NextRequest) {
  return (req.headers.get('x-relay-secret') ?? '') === process.env.RELAY_SECRET
}

// PATCH — relay syncs provider assignment mid-session
export async function PATCH(req: NextRequest) {
  if (!checkSecret(req)) return NextResponse.json({ error: 'Unauthorized' }, { status: 401 })
  const body = await req.json()
  const { dbSessionId, status, providerUserId } = body
  if (!dbSessionId) return NextResponse.json({ error: 'Missing dbSessionId' }, { status: 400 })

  const update: Record<string, unknown> = {}
  if (status !== undefined)        update.status           = status
  if (providerUserId !== undefined) update.provider_user_id = providerUserId

  const { error } = await supabase.from('peercam_sessions').update(update).eq('id', dbSessionId)
  if (error) return NextResponse.json({ error: error.message }, { status: 500 })
  return NextResponse.json({ ok: true })
}

// POST — relay reports session end
export async function POST(req: NextRequest) {
  if (!checkSecret(req)) return NextResponse.json({ error: 'Unauthorized' }, { status: 401 })
  const body = await req.json()
  const { sessionId, bytesUsed, providerUserId, requesterUserId, disconnectReason } = body
  if (!sessionId) return NextResponse.json({ error: 'Missing sessionId' }, { status: 400 })

  const { error } = await supabase
    .from('peercam_sessions')
    .update({
      status:            'ended',
      ended_at:          new Date().toISOString(),
      bytes_used:        bytesUsed ?? 0,
      provider_user_id:  providerUserId  ?? null,
      requester_user_id: requesterUserId ?? null,
      disconnect_reason: disconnectReason ?? null,
    })
    .eq('id', sessionId)

  if (error) return NextResponse.json({ error: error.message }, { status: 500 })
  return NextResponse.json({ ok: true })
}
