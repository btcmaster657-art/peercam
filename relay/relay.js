import { WebSocketServer, WebSocket } from 'ws'
import { createServer } from 'http'
import { randomUUID } from 'crypto'

const PORT         = parseInt(process.env.PORT ?? '8080')
const API_BASE     = process.env.API_BASE ?? ''
const RELAY_SECRET = process.env.RELAY_SECRET ?? ''

// peerId → WebSocket (with extra fields)
const peers    = new Map()
// sessionId → session object
const sessions = new Map()

const MAX_RECONNECT_ATTEMPTS = 8

function log(tag, msg, extra = '') {
  const ts = new Date().toISOString().slice(11, 23)
  console.log(`[${ts}] [${tag}] ${msg}${extra ? ' | ' + extra : ''}`)
}
function logErr(tag, msg, err) {
  const ts = new Date().toISOString().slice(11, 23)
  console.error(`[${ts}] [${tag}] ERROR ${msg} | ${err?.message ?? err}`)
}

// ── Auth — validates relay token against Next.js API ─────────────────────────

async function verifyToken(token, role) {
  if (!API_BASE || !RELAY_SECRET) return { ok: false, error: 'Relay not configured' }
  if (!token) return { ok: false, error: 'Missing token' }
  try {
    const res = await fetch(`${API_BASE}/api/relay/auth`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', 'x-relay-secret': RELAY_SECRET },
      body: JSON.stringify({ token, role }),
      signal: AbortSignal.timeout(5000),
    })
    const data = await res.json().catch(() => ({}))
    if (!res.ok) return { ok: false, error: data.error ?? `Auth failed (${res.status})` }
    return { ok: true, userId: data.userId, joinCode: data.joinCode ?? null }
  } catch (err) {
    logErr('AUTH', 'verifyToken failed', err)
    return { ok: false, error: 'Auth request failed' }
  }
}

// ── Session lifecycle ─────────────────────────────────────────────────────────

function createSession(requesterWs, providerWs, joinCode, dbSessionId) {
  const sessionId = randomUUID()
  requesterWs.sessionId = sessionId
  providerWs.sessionId  = sessionId

  sessions.set(sessionId, {
    requesterId:     requesterWs.peerId,
    providerId:      providerWs.peerId,
    requesterUserId: requesterWs.userId,
    providerUserId:  providerWs.userId,
    joinCode,
    dbSessionId:     dbSessionId ?? null,
    agentReady:      false,
    reconnectAttempts: 0,
    lastActivity:    Date.now(),
  })

  send(providerWs,  { type: 'session_request', sessionId })
  send(requesterWs, { type: 'session_created', sessionId })
  log(requesterWs.peerId.slice(0,8), `SESSION_CREATED id=${sessionId.slice(0,8)} provider=${providerWs.peerId.slice(0,8)} code=${joinCode}`)
  reportSessionStart(sessions.get(sessionId))

  // Provider must respond with agent_ready within 10s or we abort
  setTimeout(() => {
    const session = sessions.get(sessionId)
    if (!session || session.agentReady) return
    if (requesterWs.readyState !== WebSocket.OPEN) return
    log(requesterWs.peerId.slice(0,8), `AGENT_READY_TIMEOUT sessionId=${sessionId.slice(0,8)}`)
    providerWs.sessionId  = null
    requesterWs.sessionId = null
    sessions.delete(sessionId)
    send(requesterWs, { type: 'error', message: 'Provider did not respond in time' })
  }, 10_000)
}

function reportSessionStart(session) {
  if (!API_BASE || !RELAY_SECRET || !session?.dbSessionId) return
  fetch(`${API_BASE}/api/session/end`, {
    method: 'PATCH',
    headers: { 'Content-Type': 'application/json', 'x-relay-secret': RELAY_SECRET },
    body: JSON.stringify({
      dbSessionId:    session.dbSessionId,
      status:         'active',
      providerUserId: session.providerUserId ?? null,
    }),
  }).catch(err => logErr('RELAY', 'reportSessionStart failed', err))
}

function reportSessionEnd(session, sessionId) {
  if (!API_BASE || !RELAY_SECRET || !session?.dbSessionId) return
  fetch(`${API_BASE}/api/session/end`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', 'x-relay-secret': RELAY_SECRET },
    body: JSON.stringify({
      sessionId:       session.dbSessionId,
      bytesUsed:       0,
      providerUserId:  session.providerUserId ?? null,
      requesterUserId: session.requesterUserId ?? null,
      disconnectReason: session.disconnectReason ?? null,
    }),
  }).catch(err => logErr('RELAY', 'reportSessionEnd failed', err))
}

// ── Auto-reconnect when provider drops ───────────────────────────────────────

async function attemptReconnect(requesterWs, droppedSession) {
  const { joinCode, dbSessionId, reconnectAttempts, providerId } = droppedSession

  if ((reconnectAttempts ?? 0) >= MAX_RECONNECT_ATTEMPTS) {
    send(requesterWs, { type: 'session_ended', reason: 'no_provider_available' })
    return
  }

  // Find another provider with the same join code (not the one that just dropped)
  let nextProvider = null
  for (const [, peer] of peers) {
    if (
      peer.role === 'provider' &&
      peer.joinCode === joinCode &&
      !peer.sessionId &&
      peer.readyState === WebSocket.OPEN &&
      peer.peerId !== providerId
    ) {
      nextProvider = peer
      break
    }
  }

  if (!nextProvider) {
    const attempt = (reconnectAttempts ?? 0) + 1
    log(requesterWs.peerId.slice(0,8), `RECONNECT no provider for code=${joinCode} attempt=${attempt}/${MAX_RECONNECT_ATTEMPTS}`)
    // Notify requester so UI can show reconnecting state
    send(requesterWs, { type: 'reconnecting', attempt, maxAttempts: MAX_RECONNECT_ATTEMPTS })
    const delay = Math.min(2000 * Math.pow(2, attempt - 1), 30_000) // 2s, 4s, 8s … 30s cap
    setTimeout(() => {
      if (requesterWs.readyState !== WebSocket.OPEN || requesterWs.sessionId) return
      attemptReconnect(requesterWs, { ...droppedSession, reconnectAttempts: attempt })
    }, delay)
    return
  }

  log(requesterWs.peerId.slice(0,8), `RECONNECT → provider=${nextProvider.peerId.slice(0,8)} attempt=${(reconnectAttempts ?? 0) + 1}`)
  createSession(requesterWs, nextProvider, joinCode, dbSessionId)
}

// ── HTTP server ───────────────────────────────────────────────────────────────

const server = createServer((req, res) => {
  const url = new URL(req.url, 'http://localhost')

  if (url.pathname === '/health') {
    res.writeHead(200, { 'Content-Type': 'application/json' })
    res.end(JSON.stringify({ status: 'ok', peers: peers.size, sessions: sessions.size }))
    return
  }

  // /check-code?code=1234567890&secret=... — used by relay-token API to verify
  // a provider with this code is actually online before issuing a requester token
  if (url.pathname === '/check-code') {
    const secret = req.headers['x-relay-secret'] ?? ''
    if (RELAY_SECRET && secret !== RELAY_SECRET) {
      res.writeHead(401); res.end(JSON.stringify({ error: 'Unauthorized' })); return
    }
    const code = url.searchParams.get('code')
    let online = false
    for (const [, peer] of peers) {
      if (peer.role === 'provider' && peer.joinCode === code && !peer.sessionId && peer.readyState === WebSocket.OPEN) {
        online = true; break
      }
    }
    res.writeHead(200, { 'Content-Type': 'application/json' })
    res.end(JSON.stringify({ online }))
    return
  }

  res.writeHead(404); res.end()
})

// ── WebSocket server ──────────────────────────────────────────────────────────

const wss = new WebSocketServer({ server })

wss.on('connection', (ws, req) => {
  const peerId    = randomUUID()
  const clientIp  = req.headers['x-forwarded-for'] ?? req.socket.remoteAddress
  const host      = req.headers['host'] ?? ''
  const relayUrl  = host ? `wss://${host}` : ''

  Object.assign(ws, {
    peerId, role: null, userId: null, joinCode: null,
    sessionId: null, bytesTransferred: 0, isAlive: true, relayUrl,
  })

  log(peerId.slice(0,8), `CONNECTED from ${clientIp}`)
  send(ws, { type: 'connected', peerId })

  ws.on('pong', () => { ws.isAlive = true })

  ws.on('message', async (data) => {
    try {
      ws.bytesTransferred += data.length
      if (ws.bytesTransferred > 1_073_741_824) {
        send(ws, { type: 'error', message: 'Byte limit reached' })
        ws.terminate()
        return
      }
      const msg = JSON.parse(data.toString())
      if (msg.type !== 'ping') log(peerId.slice(0,8), `MSG_IN type=${msg.type}`)
      await handleMessage(ws, msg)
    } catch (e) {
      log(peerId.slice(0,8), `PARSE_ERROR ${e.message}`)
    }
  })

  ws.on('close', (code) => {
    log(peerId.slice(0,8), `DISCONNECTED code=${code} role=${ws.role}`)
    peers.delete(peerId)
    cleanupSession(ws)
  })

  ws.on('error', (err) => log(peerId.slice(0,8), `SOCKET_ERROR ${err.message}`))
})

// ── Message handler ───────────────────────────────────────────────────────────

async function handleMessage(ws, msg) {
  switch (msg.type) {

    // Provider registers with their join code
    case 'register_provider': {
      const auth = await verifyToken(msg.authToken ?? '', 'provider')
      if (!auth.ok) {
        send(ws, { type: 'error', message: auth.error ?? 'Unauthorized' })
        ws.close(1008, 'Unauthorized')
        return
      }

      // Evict any existing provider connection for the same user+code
      for (const [id, peer] of peers) {
        if (peer.userId === auth.userId && peer.role === 'provider' && id !== ws.peerId) {
          if (peer.sessionId) {
            const old = sessions.get(peer.sessionId)
            if (old) { old.providerId = ws.peerId; ws.sessionId = peer.sessionId; peer.sessionId = null }
          }
          send(peer, { type: 'error', message: 'Replaced by new connection' })
          peer.terminate()
          peers.delete(id)
        }
      }

      ws.role     = 'provider'
      ws.userId   = auth.userId
      ws.joinCode = auth.joinCode  // authoritative from DB — never trust client-sent value
      peers.set(ws.peerId, ws)
      send(ws, { type: 'registered', peerId: ws.peerId })
      log(ws.peerId.slice(0,8), `REGISTERED_PROVIDER userId=${auth.userId.slice(0,8)} code=${msg.joinCode}`)
      break
    }

    // Requester connects using a join code
    case 'request_session': {
      const auth = await verifyToken(msg.authToken ?? '', 'requester')
      if (!auth.ok) {
        send(ws, { type: 'error', message: auth.error ?? 'Unauthorized' })
        ws.close(1008, 'Unauthorized')
        return
      }

      const joinCode = msg.joinCode
      if (!joinCode) {
        send(ws, { type: 'error', message: 'Missing join code' })
        return
      }

      // Find a free provider advertising this code
      let provider = null
      for (const [, peer] of peers) {
        if (
          peer.role === 'provider' &&
          peer.joinCode === joinCode &&
          !peer.sessionId &&
          peer.readyState === WebSocket.OPEN
        ) {
          provider = peer
          break
        }
      }

      if (!provider) {
        send(ws, { type: 'error', message: 'Code not found or provider is busy' })
        return
      }

      ws.role   = 'requester'
      ws.userId = auth.userId
      peers.set(ws.peerId, ws)

      createSession(ws, provider, joinCode, msg.dbSessionId ?? null)
      break
    }

    // Provider signals it's ready to start WebRTC
    case 'agent_ready': {
      const session = sessions.get(msg.sessionId)
      if (!session) break
      const requester = peers.get(session.requesterId)
      session.agentReady      = true
      session.providerUserId  = ws.userId
      session.lastActivity    = Date.now()
      if (requester) send(requester, { type: 'agent_session_ready', sessionId: msg.sessionId })
      log(ws.peerId.slice(0,8), `AGENT_READY session=${msg.sessionId.slice(0,8)}`)
      break
    }

    // WebRTC signaling — forward SDP/ICE between the two peers
    case 'webrtc_signal': {
      const session = sessions.get(msg.sessionId)
      if (!session) break
      const targetId = ws.role === 'requester' ? session.providerId : session.requesterId
      const target   = peers.get(targetId)
      if (target) send(target, { type: 'webrtc_signal', sessionId: msg.sessionId, signal: msg.signal })
      if (session) session.lastActivity = Date.now()
      break
    }

    case 'end_session':
      cleanupSession(ws)
      break

    case 'ping':
      ws.isAlive = true
      break

    default:
      log(ws.peerId.slice(0,8), `UNKNOWN_MSG type=${msg.type}`)
  }
}

// ── Session cleanup ───────────────────────────────────────────────────────────

function cleanupSession(ws) {
  if (!ws.sessionId) return
  const sessionId = ws.sessionId
  const session   = sessions.get(sessionId)
  ws.sessionId    = null
  if (!session) return

  const otherId = ws.role === 'provider' ? session.requesterId : session.providerId
  const other   = peers.get(otherId)

  reportSessionEnd(session, sessionId)
  sessions.delete(sessionId)
  log(ws.peerId.slice(0,8), `SESSION_CLEANED id=${sessionId.slice(0,8)}`)

  if (!other) return
  other.sessionId = null

  if (ws.role === 'provider' && other.readyState === WebSocket.OPEN) {
    session.disconnectReason = 'provider_disconnected'
    log(other.peerId.slice(0,8), `PROVIDER_DROPPED — attempting reconnect code=${session.joinCode}`)
    attemptReconnect(other, { ...session, reconnectAttempts: 0 })
  } else {
    session.disconnectReason = 'peer_disconnected'
    send(other, { type: 'session_ended', reason: 'peer_disconnected' })
  }
}

function send(ws, data) {
  if (ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(data))
}

// ── Heartbeat ─────────────────────────────────────────────────────────────────

const heartbeat = setInterval(() => {
  wss.clients.forEach((ws) => {
    if (!ws.isAlive) { ws.terminate(); return }
    ws.isAlive = false
    ws.ping()
  })
}, 30_000)

// ── Session idle watchdog ─────────────────────────────────────────────────────

const sessionWatchdog = setInterval(() => {
  const now = Date.now()
  for (const [sessionId, session] of sessions) {
    if (now - session.lastActivity < 90_000) continue
    const provider  = peers.get(session.providerId)
    const requester = peers.get(session.requesterId)
    if (!requester || requester.readyState !== WebSocket.OPEN) continue
    if (!provider  || provider.readyState  !== WebSocket.OPEN) {
      log(requester.peerId.slice(0,8), `SESSION_WATCHDOG provider gone sessionId=${sessionId.slice(0,8)}`)
      requester.sessionId = null
      sessions.delete(sessionId)
      attemptReconnect(requester, { ...session, reconnectAttempts: 0 })
    }
  }
}, 30_000)

wss.on('close', () => { clearInterval(heartbeat); clearInterval(sessionWatchdog) })

server.listen(PORT, () => log('RELAY', `PeerCam relay on port ${PORT}`))
