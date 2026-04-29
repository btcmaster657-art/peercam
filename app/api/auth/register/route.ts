import { NextRequest, NextResponse } from 'next/server'
import { supabaseAnon } from '@/lib/supabase'

export async function POST(req: NextRequest) {
  const { email, password } = await req.json()
  if (!email || !password || password.length < 8)
    return NextResponse.json({ error: 'Invalid email or password (min 8 chars).' }, { status: 400 })

  const { error } = await supabaseAnon.auth.signUp({ email, password })
  if (error) {
    const status = error.message.includes('already registered') ? 409 : 400
    return NextResponse.json({ error: error.message }, { status })
  }
  return NextResponse.json({ ok: true }, { status: 201 })
}
