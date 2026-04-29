import Link from 'next/link'

const features = [
  { icon: '📷', title: 'Any device as webcam', desc: 'Stream your phone or tablet camera directly into any app on your PC.' },
  { icon: '🔒', title: 'Peer-to-peer encrypted', desc: 'WebRTC end-to-end encryption — your video never touches our servers.' },
  { icon: '⚡', title: 'Ultra-low latency', desc: 'Direct peer connection keeps lag under 100 ms on a local network.' },
  { icon: '🖥️', title: 'Works everywhere', desc: 'Appears as a standard webcam in Zoom, Teams, OBS, and more.' },
]

export default function LandingPage() {
  return (
    <main className="flex flex-col items-center">
      {/* Nav */}
      <nav className="w-full flex justify-between items-center px-8 py-4 border-b border-zinc-800">
        <span className="font-bold text-lg tracking-tight">PeerCam</span>
        <div className="flex gap-4 text-sm">
          <Link href="/login" className="text-zinc-400 hover:text-white transition-colors">Log in</Link>
          <Link href="/signup" className="bg-indigo-600 hover:bg-indigo-500 text-white px-4 py-1.5 rounded-full transition-colors">Sign up</Link>
        </div>
      </nav>

      {/* Hero */}
      <section className="flex flex-col items-center text-center px-6 pt-24 pb-16 max-w-3xl">
        <h1 className="text-5xl font-extrabold tracking-tight leading-tight mb-4">
          Your phone is now<br />
          <span className="text-indigo-400">a virtual webcam</span>
        </h1>
        <p className="text-zinc-400 text-lg mb-8 max-w-xl">
          PeerCam streams any camera to your Windows PC over WebRTC and injects it as a real DirectShow device — no cables, no drivers to hunt down.
        </p>
        <div className="flex gap-4 flex-wrap justify-center">
          <Link
            href="/signup"
            className="bg-indigo-600 hover:bg-indigo-500 text-white font-semibold px-8 py-3 rounded-full transition-colors"
          >
            Get started free
          </Link>
          <Link
            href="#features"
            className="border border-zinc-700 hover:border-zinc-500 text-zinc-300 px-8 py-3 rounded-full transition-colors"
          >
            Learn more
          </Link>
        </div>
      </section>

      {/* Features */}
      <section id="features" className="grid grid-cols-1 sm:grid-cols-2 gap-6 px-8 pb-24 max-w-4xl w-full">
        {features.map(f => (
          <div key={f.title} className="bg-zinc-900 border border-zinc-800 rounded-2xl p-6">
            <div className="text-3xl mb-3">{f.icon}</div>
            <h3 className="font-semibold text-white mb-1">{f.title}</h3>
            <p className="text-zinc-400 text-sm">{f.desc}</p>
          </div>
        ))}
      </section>

      {/* Download CTA */}
      <section className="w-full bg-indigo-950 border-t border-indigo-900 flex flex-col items-center py-16 px-6 text-center">
        <h2 className="text-3xl font-bold mb-2">Ready to try it?</h2>
        <p className="text-zinc-400 mb-6">Create a free account and download the Windows app.</p>
        <Link
          href="/signup"
          className="bg-indigo-600 hover:bg-indigo-500 text-white font-semibold px-10 py-3 rounded-full transition-colors"
        >
          Sign up &amp; Download
        </Link>
      </section>
    </main>
  )
}
