import { NextRequest, NextResponse } from 'next/server'
import { supabaseAnon } from '@/lib/supabase'

export async function POST(req: NextRequest) {
  const { email, password } = await req.json()
  if (!email || !password)
    return NextResponse.json({ error: 'Missing credentials' }, { status: 400 })

  const { data, error } = await supabaseAnon.auth.signInWithPassword({ email, password })
  if (error || !data.session)
    return NextResponse.json({ error: 'Invalid email or password' }, { status: 401 })

  return NextResponse.json({ sessionToken: data.session.access_token, userId: data.user.id })
}
