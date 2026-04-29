import { NextRequest, NextResponse } from 'next/server'
import { supabase } from '@/lib/supabase'

export async function GET(req: NextRequest) {
  const secret = req.headers.get('x-relay-secret') ?? ''
  if (secret !== process.env.RELAY_SECRET)
    return NextResponse.json({ error: 'Unauthorized' }, { status: 401 })

  const { searchParams } = new URL(req.url)
  const providerUserId = searchParams.get('providerUserId')
  if (!providerUserId) return NextResponse.json({ error: 'Missing providerUserId' }, { status: 400 })

  const { data, error } = await supabase
    .from('provider_share_status')
    .select('can_accept_sessions, total_bytes_today, daily_limit_bytes, private_share')
    .eq('user_id', providerUserId)
    .single()

  if (error || !data) {
    return NextResponse.json({ can_accept_sessions: true, total_bytes_today: 0, daily_limit_bytes: null })
  }

  return NextResponse.json(data)
}
