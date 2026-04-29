import { getServerSession } from 'next-auth'
import { authOptions } from '@/lib/auth'
import { redirect } from 'next/navigation'

const downloads = [
  { platform: 'Windows', file: '/downloads/PeerCam-Setup.exe', icon: '🪟', note: 'Fully supported' },
  { platform: 'Linux', file: '/downloads/PeerCam-Setup.AppImage', icon: '🐧', note: 'Fully supported' },
  { platform: 'macOS', file: null, icon: '🍎', note: 'Not available — see FAQ' },
]

export default async function DashboardPage() {
  const session = await getServerSession(authOptions)
  if (!session) redirect('/login')

  return (
    <main className="flex flex-col items-center px-6 py-16">
      <div className="w-full max-w-2xl">
        <h1 className="text-3xl font-bold mb-1">Welcome back</h1>
        <p className="text-zinc-400 mb-10">{session.user?.email}</p>

        <h2 className="text-lg font-semibold mb-4">Download PeerCam</h2>
        <div className="flex flex-col gap-4">
          {downloads.map(({ platform, file, icon, note }) => (
            <div key={platform} className="flex items-center justify-between bg-zinc-900 border border-zinc-800 rounded-xl px-6 py-4">
              <div className="flex items-center gap-3">
                <span className="text-2xl">{icon}</span>
                <div>
                  <p className="font-medium">{platform}</p>
                  <p className="text-zinc-500 text-xs">{note}</p>
                </div>
              </div>
              {file ? (
                <a
                  href={`/downloads${file}`}
                  download
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
