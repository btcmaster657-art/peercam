// ── Relay pool ────────────────────────────────────────────────────────────────
// Managed by build-save-relays.mjs — edit RELAY_ENDPOINTS in .env.local instead.

export const RELAY_ENDPOINTS: string[] = (
  process.env.RELAY_ENDPOINTS ?? 'wss://relay.peercam.app'
)
  .split(',')
  .map(s => s.trim())
  .filter(Boolean)

// ── Health cache ──────────────────────────────────────────────────────────────

interface RelayHealth {
  url: string
  alive: boolean
  peers: number
  sessions: number
  latencyMs: number
  checkedAt: number
}

const HEALTH_TTL = 15_000
const HEALTH_TIMEOUT = 4_000
const healthCache = new Map<string, RelayHealth>()

async function checkRelay(wsUrl: string): Promise<RelayHealth> {
  const httpUrl = wsUrl.replace(/^wss:\/\//, 'https://').replace(/^ws:\/\//, 'http://')
  const start = Date.now()
  try {
    const res = await fetch(`${httpUrl}/health`, { signal: AbortSignal.timeout(HEALTH_TIMEOUT) })
    const latencyMs = Date.now() - start
    if (!res.ok) throw new Error(`status=${res.status}`)
    const data = await res.json()
    return { url: wsUrl, alive: true, peers: data.peers ?? 0, sessions: data.sessions ?? 0, latencyMs, checkedAt: Date.now() }
  } catch {
    return { url: wsUrl, alive: false, peers: 0, sessions: 0, latencyMs: 9999, checkedAt: Date.now() }
  }
}

async function getHealth(wsUrl: string): Promise<RelayHealth> {
  const cached = healthCache.get(wsUrl)
  if (cached && Date.now() - cached.checkedAt < HEALTH_TTL) return cached
  const health = await checkRelay(wsUrl)
  healthCache.set(wsUrl, health)
  return health
}

function score(h: RelayHealth): number {
  return h.sessions * 10 + h.peers * 1 + h.latencyMs * 0.01
}

export async function pickRelay(): Promise<string> {
  const results = await Promise.all(RELAY_ENDPOINTS.map(getHealth))
  const alive = results.filter(h => h.alive)
  const pool = alive.length > 0 ? alive : results
  pool.sort((a, b) => score(a) - score(b))
  return pool[0].url
}

export async function getRelayFallbackList(): Promise<string[]> {
  const results = await Promise.all(RELAY_ENDPOINTS.map(getHealth))
  const alive = results.filter(h => h.alive)
  const pool = alive.length > 0 ? alive : results
  pool.sort((a, b) => score(a) - score(b))
  return pool.map(h => h.url)
}

export function relayHttpUrl(wsUrl: string): string {
  return wsUrl.replace(/^wss:\/\//, 'https://').replace(/^ws:\/\//, 'http://')
}
