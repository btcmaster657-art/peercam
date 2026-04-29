import { getServerSession } from 'next-auth'
import { authOptions } from '@/lib/auth'
import { redirect } from 'next/navigation'
import LogoutButton from './LogoutButton'

const downloads = [
  { platform: 'Windows', platform_key: 'win',   icon: '🪟', note: 'Fully supported' },
  { platform: 'Linux',   platform_key: 'linux', icon: '🐧', note: 'Fully supported' },
  { platform: 'macOS',   platform_key: null,    icon: '🍎', note: 'Not available — see FAQ' },
]

export default async function DashboardPage() {
  const session = await getServerSession(authOptions)
  if (!session) redirect('/')

  return (
    <main className="flex flex-col items-center">
      <nav className="w-full flex justify-between items-center px-8 py-4 border-b border-zinc-800">
        <span className="font-bold text-lg tracking-tight">PeerCam</span>
        <div className="flex items-center gap-4 text-sm">
          <span className="text-zinc-500">{session.user?.email}</span>
          <LogoutButton />
        </div>
      </nav>

      <div className="w-full max-w-2xl px-6 py-12">
        <h1 className="text-3xl font-bold mb-1">Welcome back</h1>
        <p className="text-zinc-400 mb-10 text-sm">Download the desktop app and connect using a 10-digit code.</p>

        <h2 className="text-lg font-semibold mb-4">Download PeerCam</h2>
        <div className="flex flex-col gap-4">
          {downloads.map(({ platform, platform_key, icon, note }) => (
            <div key={platform} className="flex items-center justify-between bg-zinc-900 border border-zinc-800 rounded-xl px-6 py-4">
              <div className="flex items-center gap-3">
                <span className="text-2xl">{icon}</span>
                <div>
                  <p className="font-medium">{platform}</p>
                  <p className="text-zinc-500 text-xs">{note}</p>
                </div>
              </div>
              {platform_key ? (
                <a
                  href={`/api/download?platform=${platform_key}`}
                  className="bg-indigo-600 hover:bg-indigo-500 text-white text-sm font-semibold px-5 py-2 rounded-lg transition-colors"
                >
                  Download
                </a>
              ) : (
                <span className="text-zinc-600 text-sm">Unavailable</span>
              )}
            </div>
          ))}
        </div>

        <div className="mt-10 bg-zinc-900 border border-zinc-800 rounded-xl p-6 text-sm text-zinc-400">
          <p className="font-semibold text-white mb-2">macOS — why not supported?</p>
          <p>Apple&apos;s CoreMediaIO DAL plugins require notarization, a hardened runtime, and an Apple Developer account. Safari and Chrome enforce strict camera sandboxing on macOS 13+. We may add support in the future.</p>
        </div>
      </div>
    </main>
  )
}
