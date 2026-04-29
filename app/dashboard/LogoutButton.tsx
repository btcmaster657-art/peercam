'use client'

import { signOut } from 'next-auth/react'

export default function LogoutButton() {
  return (
    <button
      onClick={() => signOut({ callbackUrl: '/' })}
      className="text-sm text-zinc-400 hover:text-white transition-colors"
    >
      Log out
    </button>
  )
}
