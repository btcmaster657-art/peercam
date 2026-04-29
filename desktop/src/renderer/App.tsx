import { useEffect, useRef, useState } from 'react'
import { useWebRTC, Role, Status, ConnectParams } from './useWebRTC'

const API_BASE = import.meta.env.VITE_API_BASE ?? 'https://peercam.vercel.app'

const STATUS_LABEL: Record<Status, string> = {
  idle:         'Disconnected',
  connecting:   'Connecting…',
  waiting_peer: 'Waiting for viewer to connect…',
  waiting_host: 'Ready — share your code',
  reconnecting: 'Reconnecting…',
  connected:    '● Live',
  error:        'Error',
}
const STATUS_COLOR: Record<Status, string> = {
  idle:         '#71717a',
  connecting:   '#a78bfa',
  waiting_peer: '#fbbf24',
  waiting_host: '#34d399',
  reconnecting: '#f97316',
  connected:    '#34d399',
  error:        '#f87171',
}

interface AuthState {
  accessToken: string
  userId: string
}
interface CodeState {
  code: string | null
  enabled: boolean
}

export default function App() {
  const [auth, setAuth]               = useState<AuthState | null>(null)
  const [role, setRole]               = useState<Role>('provider')
  const [codeState, setCodeState]     = useState<CodeState>({ code: null, enabled: false })
  const [codeInput, setCodeInput]     = useState('')
  const [loginError, setLoginError]   = useState('')
  const [connectError, setConnectError] = useState('')
  const [loginLoading, setLoginLoading] = useState(false)
  const [codeLoading, setCodeLoading]   = useState(false)
  const [connecting, setConnecting]     = useState(false)
  const [logPath, setLogPath]           = useState<string | null>(null)
  const videoPreviewRef = useRef<HTMLVideoElement>(null)
  const previewStreamRef = useRef<MediaStream | null>(null)

  const platform = window.peercam?.platform ?? 'win32'
  const { status, error, connect, disconnect } = useWebRTC()

  // Fetch log path once on mount
  useEffect(() => {
    window.peercam?.getLogPath().then(setLogPath).catch(() => {})
  }, [])

  const isActive = ['connecting', 'waiting_peer', 'waiting_host', 'reconnecting', 'connected'].includes(status)

  // Fetch provider code whenever auth or role changes to provider
  useEffect(() => {
    if (!auth || role !== 'provider') return
    fetchCode(auth.accessToken)
  }, [auth, role])

  async function fetchCode(accessToken: string) {
    const res = await fetch(`${API_BASE}/api/provider/code`, {
      headers: { Authorization: `Bearer ${accessToken}` },
    })
    if (res.ok) setCodeState(await res.json())
  }

  async function handleLogin(e: React.FormEvent<HTMLFormElement>) {
    e.preventDefault()
    setLoginError('')
    setLoginLoading(true)
    const fd = new FormData(e.currentTarget)
    try {
      const res = await fetch(`${API_BASE}/api/auth/signin`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ email: fd.get('email'), password: fd.get('password') }),
      })
      const data = await res.json()
      if (!res.ok) throw new Error(data.error ?? 'Login failed')
      setAuth({ accessToken: data.sessionToken, userId: data.userId })
    } catch (err: unknown) {
      setLoginError(err instanceof Error ? err.message : 'Login failed')
    } finally {
      setLoginLoading(false)
    }
  }

  async function handleGenerateCode() {
    if (!auth) return
    setCodeLoading(true)
    const res = await fetch(`${API_BASE}/api/provider/code`, {
      method: 'POST',
      headers: { Authorization: `Bearer ${auth.accessToken}` },
    })
    if (res.ok) setCodeState(await res.json())
    setCodeLoading(false)
  }

  async function handleToggleCode(enabled: boolean) {
    if (!auth) return
    setCodeLoading(true)
    const res = await fetch(`${API_BASE}/api/provider/code`, {
      method: 'PATCH',
      headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${auth.accessToken}` },
      body: JSON.stringify({ enabled }),
    })
    if (res.ok) setCodeState(await res.json())
    setCodeLoading(false)
  }

  async function handleConnect() {
    if (!auth) return
    setConnectError('')
    setConnecting(true)

    try {
      const body: Record<string, string> = { role }
      if (role === 'requester') body.joinCode = codeInput

      const tokenRes = await fetch(`${API_BASE}/api/auth/relay-token`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${auth.accessToken}` },
        body: JSON.stringify(body),
      })
      const tokenData = await tokenRes.json()
      if (!tokenRes.ok) throw new Error(tokenData.error ?? 'Failed to connect')

      // Start camera preview for provider before connecting
      if (role === 'provider' && videoPreviewRef.current) {
        try {
          const stream = await navigator.mediaDevices.getUserMedia({ video: true, audio: false })
          previewStreamRef.current = stream
          videoPreviewRef.current.srcObject = stream
          videoPreviewRef.current.play().catch(() => {})
        } catch { /* preview is optional */ }
      }

      const params: ConnectParams = {
        relayUrl:    tokenData.relayUrl,
        authToken:   tokenData.token,
        userId:      tokenData.userId,
        joinCode:    role === 'provider' ? (codeState.code ?? '') : codeInput,
        role,
        dbSessionId: tokenData.dbSessionId ?? null,
      }

      // Pass params directly — no closure race condition
      connect(params)
    } catch (err: unknown) {
      const msg = err instanceof Error ? err.message : 'Connection failed'
      setConnectError(msg)
      window.peercam?.log('ERROR', `handleConnect failed: ${msg}`)
    } finally {
      setConnecting(false)
    }
  }

  function handleDisconnect() {
    disconnect()
    stopPreview()
  }

  function stopPreview() {
    if (previewStreamRef.current) {
      previewStreamRef.current.getTracks().forEach(t => t.stop())
      previewStreamRef.current = null
    }
    if (videoPreviewRef.current) videoPreviewRef.current.srcObject = null
  }

  function handleLogout() {
    handleDisconnect()
    setAuth(null)
    setCodeState({ code: null, enabled: false })
    setCodeInput('')
    setConnectError('')
  }

  // Stop preview when session ends unexpectedly
  useEffect(() => {
    if (status === 'idle' || status === 'error') stopPreview()
  }, [status]) // eslint-disable-line react-hooks/exhaustive-deps

  if (platform === 'darwin') {
    return (
      <div style={s.center}>
        <p style={{ color: '#f87171', textAlign: 'center', padding: 24 }}>
          macOS is not supported.<br />Virtual camera requires Apple notarization.
        </p>
      </div>
    )
  }

  if (!auth) {
    return (
      <div style={s.center}>
        <div style={s.card}>
          <h1 style={s.title}>PeerCam</h1>
          <form onSubmit={handleLogin} style={s.form}>
            <input name="email" type="email" required placeholder="Email" style={s.input} autoComplete="email" />
            <input name="password" type="password" required placeholder="Password" style={s.input} autoComplete="current-password" />
            {loginError && <p style={s.err}>{loginError}</p>}
            <button type="submit" disabled={loginLoading} style={s.btn}>
              {loginLoading ? 'Signing in…' : 'Sign in'}
            </button>
          </form>
        </div>
      </div>
    )
  }

  return (
    <div style={s.center}>
      <div style={s.card}>
        <h1 style={s.title}>PeerCam</h1>

        {/* Role selector — hidden while active */}
        {!isActive && (
          <div style={s.roleRow}>
            {(['provider', 'requester'] as Role[]).map(r => (
              <button
                key={r}
                onClick={() => { setRole(r); setConnectError('') }}
                style={{ ...s.roleBtn, ...(role === r ? s.roleBtnActive : {}) }}
              >
                {r === 'provider' ? '📷 Share camera' : '🖥️ Receive camera'}
              </button>
            ))}
          </div>
        )}

        {/* Provider: code management */}
        {role === 'provider' && !isActive && (
          <div style={s.codeBox}>
            {codeState.code ? (
              <>
                <p style={s.codeLabel}>Your join code</p>
                <p style={{ ...s.code, color: codeState.enabled ? '#f4f4f5' : '#52525b' }}>
                  {codeState.code.slice(0, 5)}&thinsp;{codeState.code.slice(5)}
                </p>
                <div style={s.codeActions}>
                  <button
                    onClick={() => handleToggleCode(!codeState.enabled)}
                    disabled={codeLoading}
                    style={{ ...s.smallBtn, background: codeState.enabled ? '#7f1d1d' : '#14532d' }}
                  >
                    {codeState.enabled ? 'Disable' : 'Enable'}
                  </button>
                  <button onClick={handleGenerateCode} disabled={codeLoading} style={s.smallBtn}>
                    Refresh
                  </button>
                </div>
                {!codeState.enabled && (
                  <p style={s.warn}>⚠ Code disabled — viewers cannot connect</p>
                )}
              </>
            ) : (
              <button onClick={handleGenerateCode} disabled={codeLoading} style={s.btn}>
                {codeLoading ? 'Generating…' : 'Generate join code'}
              </button>
            )}
          </div>
        )}

        {/* Requester: code input */}
        {role === 'requester' && !isActive && (
          <input
            value={codeInput}
            onChange={e => setCodeInput(e.target.value.replace(/\D/g, '').slice(0, 10))}
            placeholder="Enter 10-digit code"
            maxLength={10}
            style={{ ...s.input, textAlign: 'center', letterSpacing: '0.25em', fontSize: 20, fontWeight: 600 }}
          />
        )}

        {/* Camera preview (provider, while active) */}
        {role === 'provider' && (
          <video
            ref={videoPreviewRef}
            muted
            playsInline
            style={{ ...s.preview, display: isActive ? 'block' : 'none' }}
          />
        )}

        {/* Status */}
        <div style={{ ...s.statusBadge, color: STATUS_COLOR[status] }}>
          {STATUS_LABEL[status]}
        </div>

        {/* Error from hook */}
        {error && status !== 'idle' && <p style={s.err}>{error}</p>}
        {/* Error from connect attempt */}
        {connectError && <p style={s.err}>{connectError}</p>}
        {/* Log path — shown when there's an error so user can find the log */}
        {(error || connectError) && logPath && (
          <p style={s.logPath}>Log: {logPath}</p>
        )}

        {/* Action buttons */}
        <div style={s.btnRow}>
          {!isActive ? (
            <button
              onClick={handleConnect}
              disabled={
                connecting ||
                (role === 'provider' && (!codeState.code || !codeState.enabled)) ||
                (role === 'requester' && codeInput.length !== 10)
              }
              title={
                role === 'provider' && !codeState.code ? 'Generate a join code first' :
                role === 'provider' && !codeState.enabled ? 'Enable your code first' :
                role === 'requester' && codeInput.length !== 10 ? 'Enter the full 10-digit code' :
                ''
              }
              style={{ ...s.btn, opacity: (connecting || (role === 'provider' && (!codeState.code || !codeState.enabled)) || (role === 'requester' && codeInput.length !== 10)) ? 0.4 : 1, cursor: (role === 'provider' && (!codeState.code || !codeState.enabled)) || (role === 'requester' && codeInput.length !== 10) ? 'not-allowed' : 'pointer' }}
            >
              {connecting ? 'Connecting…' : 'Connect'}
            </button>
          ) : (
            <button onClick={handleDisconnect} style={{ ...s.btn, background: '#3f3f46' }}>
              Disconnect
            </button>
          )}
          <button onClick={handleLogout} style={{ ...s.btn, background: '#27272a', fontSize: 13 }}>
            Log out
          </button>
        </div>

        {status === 'connected' && role === 'requester' && (
          <p style={s.hint}>
            Virtual camera is active. Select "PeerCam" in Zoom, Teams, OBS, or any webcam app.
          </p>
        )}
        {status === 'waiting_host' && role === 'provider' && (
          <p style={s.hint}>
            Share your code with the viewer. They enter it on the Receive camera screen.
          </p>
        )}
      </div>
    </div>
  )
}

const s: Record<string, React.CSSProperties> = {
  center:        { flex: 1, display: 'flex', alignItems: 'center', justifyContent: 'center', padding: 24 },
  card:          { width: '100%', maxWidth: 360, display: 'flex', flexDirection: 'column', gap: 14 },
  title:         { fontSize: 24, fontWeight: 700, textAlign: 'center', letterSpacing: '-0.5px' },
  form:          { display: 'flex', flexDirection: 'column', gap: 10 },
  input:         { background: '#18181b', border: '1px solid #3f3f46', borderRadius: 8, padding: '10px 14px', color: '#f4f4f5', fontSize: 14, outline: 'none' },
  btn:           { background: '#4f46e5', color: '#fff', border: 'none', borderRadius: 8, padding: '10px 0', fontWeight: 600, fontSize: 15, cursor: 'pointer', flex: 1 },
  btnRow:        { display: 'flex', gap: 8 },
  roleRow:       { display: 'flex', gap: 8 },
  roleBtn:       { flex: 1, background: '#27272a', color: '#a1a1aa', border: '1px solid #3f3f46', borderRadius: 8, padding: '8px 0', fontSize: 13, cursor: 'pointer' },
  roleBtnActive: { background: '#1e1b4b', color: '#a5b4fc', borderColor: '#4f46e5' },
  codeBox:       { background: '#18181b', border: '1px solid #3f3f46', borderRadius: 10, padding: '20px 24px', display: 'flex', flexDirection: 'column', gap: 12, alignItems: 'center' },
  codeLabel:     { color: '#71717a', fontSize: 12, margin: 0 },
  code:          { fontSize: 30, fontWeight: 700, letterSpacing: '0.15em', margin: 0, fontVariantNumeric: 'tabular-nums' },
  codeActions:   { display: 'flex', gap: 8, width: '100%' },
  smallBtn:      { flex: 1, background: '#27272a', color: '#d4d4d8', border: '1px solid #3f3f46', borderRadius: 7, padding: '7px 0', fontSize: 13, cursor: 'pointer' },
  warn:          { color: '#fbbf24', fontSize: 12, margin: 0 },
  statusBadge:   { textAlign: 'center', fontWeight: 600, fontSize: 14 },
  err:           { color: '#f87171', fontSize: 13, textAlign: 'center' },
  hint:          { color: '#71717a', fontSize: 12, textAlign: 'center', lineHeight: 1.5 },
  logPath:       { color: '#52525b', fontSize: 11, textAlign: 'center' as const, wordBreak: 'break-all' as const, lineHeight: 1.4 },
  preview:       { width: '100%', borderRadius: 8, background: '#18181b', aspectRatio: '16/9', objectFit: 'cover' },
}
