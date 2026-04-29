import { NextRequest, NextResponse } from 'next/server'

// NextAuth sets this cookie on login — no DB call needed at the edge
const SESSION_COOKIE = process.env.NEXTAUTH_URL?.startsWith('https')
  ? '__Secure-next-auth.session-token'
  : 'next-auth.session-token'

export function proxy(req: NextRequest) {
  const { pathname } = req.nextUrl
  const isAuthed = !!req.cookies.get(SESSION_COOKIE)?.value

  // Authenticated user hitting auth pages → dashboard
  if (isAuthed && (pathname.startsWith('/login') || pathname.startsWith('/signup'))) {
    return NextResponse.redirect(new URL('/dashboard', req.url))
  }

  // Unauthenticated user hitting dashboard → landing page
  if (!isAuthed && pathname.startsWith('/dashboard')) {
    return NextResponse.redirect(new URL('/', req.url))
  }

  return NextResponse.next()
}

export const config = {
  matcher: ['/login', '/signup', '/dashboard/:path*'],
}
