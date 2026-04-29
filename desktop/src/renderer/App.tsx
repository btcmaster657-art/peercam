import { useEffect, useRef, useState } from 'react'
import { useWebRTC, Role, Status, ConnectParams } from './useWebRTC'

const API_BASE   = import.meta.env.VITE_API_BASE ?? 'https://peercam.vercel.app'
const STORAGE_KEY = 'peercam_auth'

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

interface AuthState { accessToken: string; userId: string; email: string }
interface CodeState  { code: string | null; enabled: boolean }

// ── Spinner ───────────────────────────────────────────────────────────────────
function Spinner({ size = 14 }: { size?: number }) {
  return (
    <span style={{
      display: 'inline-block', width: size, height: size,
      border: `2px solid rgba(255,255,255,0.2)`,
      borderTopColor: '#fff', borderRadius: '50%',
      animation: 'spin 0.6s linear infinite', flexShrink: 0,
    }} />
  )
}

export default function App() {
  const [auth, setAuth]                 = useState<AuthState | null>(null)
  const [role, setRole]                 = useState<Role>('provider')
  const [codeState, setCodeState]       = useState<CodeState>({ code: null, enabled: false })
  const [codeInput, setCodeInput]       = useState('')
  const [rememberMe, setRememberMe]     = useState(true)
  const [loginError, setLoginError]     = useState('')
  const [connectError, setConnectError] = useState('')
  const [loginLoading, setLoginLoading] = useState(false)
  const [codeLoading, setCodeLoading]   = useState(false)
  const [codeFetching, setCodeFetching] = useState(false)
  const [codeError, setCodeError]       = useState('')
  const [connecting, setConnecting]     = useState(false)
  const [disconnecting, setDisconnecting] = useState(false)
  const [isFullscreen, setIsFullscreen] = useState(false)
  const [logPath, setLogPath]           = useState<string | null>(null)
  const [logs, setLogs]                 = useState<string[]>([])

  const videoPreviewRef  = useRef<HTMLVideoElement>(null)
  const platform = window.peercam?.platform ?? 'win32'
  const { status, error, vcamOk, vcamObs, connect, disconnect, localStream } = useWebRTC()
  const isActive = ['connecting', 'waiting_peer', 'waiting_host', 'reconnecting', 'connected'].includes(status)

  // ── Spinner keyframe injection ────────────────────────────────────────────
  useEffect(() => {
    const style = document.createElement('style')
    style.textContent = '@keyframes spin { to { transform: rotate(360deg) } } @keyframes shimmer { 0%{background-position:200% 0} 100%{background-position:-200% 0} }'
    document.head.appendChild(style)
    return () => style.remove()
  }, [])

  // ── Log setup ─────────────────────────────────────────────────────────────
  useEffect(() => {
    window.peercam?.getLogPath().then(setLogPath).catch(() => {})
    const orig = { log: console.log, warn: console.warn, error: console.error }
    const push = (line: string) => setLogs(prev => [...prev.slice(-49), line])
    console.log   = (...a) => { orig.log(...a);   push(a.join(' ')) }
    console.warn  = (...a) => { orig.warn(...a);  push('⚠ ' + a.join(' ')) }
    console.error = (...a) => { orig.error(...a); push('✕ ' + a.join(' ')) }
    return () => { Object.assign(console, orig) }
  }, [])

  // ── Remember me — restore session from localStorage ───────────────────────
  useEffect(() => {
    try {
      const saved = localStorage.getItem(STORAGE_KEY)
      if (saved) {
        const parsed = JSON.parse(saved) as AuthState
        if (parsed.accessToken && parsed.userId) setAuth(parsed)
      }
    } catch { /* corrupt storage */ }
  }, [])

  // ── Fullscreen — Escape key handler ───────────────────────────────────────
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape' && isFullscreen) exitFullscreen()
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [isFullscreen])

  // ── Fullscreen change detection ───────────────────────────────────────────
  useEffect(() => {
    const onChange = () => setIsFullscreen(!!document.fullscreenElement)
    document.addEventListener('fullscreenchange', onChange)
    return () => document.removeEventListener('fullscreenchange', onChange)
  }, [])

  function enterFullscreen() {
    document.documentElement.requestFullscreen().then(() => setIsFullscreen(true)).catch(() => {})
  }
  function exitFullscreen() {
    document.exitFullscreen().then(() => setIsFullscreen(false)).catch(() => {})
  }

  // ── Fetch provider code ───────────────────────────────────────────────────
  useEffect(() => {
    if (!auth || role !== 'provider') return
    fetchCode(auth.accessToken)
  }, [auth, role])

  async function fetchCode(accessToken: string) {
    setCodeFetching(true)
    setCodeError('')
    try {
      const url = `${API_BASE}/api/provider/code`
      window.peercam?.log('INFO', `fetchCode GET ${url}`)
      const res = await fetch(url, { headers: { Authorization: `Bearer ${accessToken}` } })
      window.peercam?.log(res.ok ? 'INFO' : 'WARN', `fetchCode response status=${res.status}`)
      if (res.ok) setCodeState(await res.json())
      else setCodeError(`Server error (${res.status}) — tap retry`)
    } catch (err: unknown) {
      const msg = err instanceof Error ? err.message : String(err)
      window.peercam?.log('ERROR', `fetchCode failed: ${msg}`)
      setCodeError('Network error — check your connection')
    } finally {
      setCodeFetching(false)
    }
  }

  // ── Auth ──────────────────────────────────────────────────────────────────
  async function handleLogin(e: React.FormEvent<HTMLFormElement>) {
    e.preventDefault()
    setLoginError('')
    setLoginLoading(true)
    const fd = new FormData(e.currentTarget)
    try {
      window.peercam?.log('INFO', `handleLogin POST ${API_BASE}/api/auth/signin`)
      const res = await fetch(`${API_BASE}/api/auth/signin`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ email: fd.get('email'), password: fd.get('password') }),
      })
      const data = await res.json()
      if (!res.ok) throw new Error(data.error ?? 'Login failed')
      const authState: AuthState = { accessToken: data.sessionToken, userId: data.userId, email: fd.get('email') as string }
      setAuth(authState)
      if (rememberMe) localStorage.setItem(STORAGE_KEY, JSON.stringify(authState))
    } catch (err: unknown) {
      setLoginError(err instanceof Error ? err.message : 'Login failed')
    } finally {
      setLoginLoading(false)
    }
  }

  function handleLogout() {
    handleDisconnect()
    localStorage.removeItem(STORAGE_KEY)
    setAuth(null)
    setCodeState({ code: null, enabled: false })
    setCodeInput('')
    setConnectError('')
  }

  // ── Code management ───────────────────────────────────────────────────────
  async function handleGenerateCode() {
    if (!auth) return
    setCodeLoading(true)
    setCodeError('')
    try {
      const res = await fetch(`${API_BASE}/api/provider/code`, {
        method: 'POST',
        headers: { Authorization: `Bearer ${auth.accessToken}` },
      })
      if (res.ok) setCodeState(await res.json())
      else setCodeError(`Failed to generate code (${res.status})`)
    } catch {
      setCodeError('Network error — check your connection')
    } finally {
      setCodeLoading(false)
    }
  }

  async function handleToggleCode(enabled: boolean) {
    if (!auth) return
    setCodeLoading(true)
    setCodeError('')
    try {
      const res = await fetch(`${API_BASE}/api/provider/code`, {
        method: 'PATCH',
        headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${auth.accessToken}` },
        body: JSON.stringify({ enabled }),
      })
      if (res.ok) setCodeState(await res.json())
      else setCodeError(`Failed to update code (${res.status})`)
    } catch {
      setCodeError('Network error — check your connection')
    } finally {
      setCodeLoading(false)
    }
  }

  // ── Connect / Disconnect ──────────────────────────────────────────────────
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

      const params: ConnectParams = {
        relayUrl:    tokenData.relayUrl,
        authToken:   tokenData.token,
        userId:      tokenData.userId,
        joinCode:    role === 'provider' ? (codeState.code ?? '') : codeInput,
        role,
        dbSessionId: tokenData.dbSessionId ?? null,
      }
      connect(params)
    } catch (err: unknown) {
      const msg = err instanceof Error ? err.message : 'Connection failed'
      setConnectError(msg)
      window.peercam?.log('ERROR', `handleConnect failed: ${msg}`)
    } finally {
      setConnecting(false)
    }
  }

  async function handleDisconnect() {
    setDisconnecting(true)
    await new Promise<void>(resolve => {
      disconnect()
      setTimeout(resolve, 400)
    })
    setDisconnecting(false)
  }

  function stopPreview() {
    if (videoPreviewRef.current) videoPreviewRef.current.srcObject = null
  }

  useEffect(() => {
    if (status === 'idle' || status === 'error') stopPreview()
    if (role === 'provider' && status === 'connected' && videoPreviewRef.current && localStream.current) {
      videoPreviewRef.current.srcObject = localStream.current
      videoPreviewRef.current.play().catch(() => {})
    }
  }, [status]) // eslint-disable-line react-hooks/exhaustive-deps

  // ── Disabled state helpers ────────────────────────────────────────────────
  const connectDisabled =
    connecting ||
    (role === 'provider' && (!codeState.code || !codeState.enabled)) ||
    (role === 'requester' && codeInput.length !== 10)

  const connectTitle =
    role === 'provider' && !codeState.code    ? 'Generate a join code first' :
    role === 'provider' && !codeState.enabled ? 'Enable your code first' :
    role === 'requester' && codeInput.length !== 10 ? 'Enter the full 10-digit code' : ''

  // ── macOS stub ────────────────────────────────────────────────────────────
  if (platform === 'darwin') {
    return (
      <div style={s.center}>
        <p style={{ color: '#f87171', textAlign: 'center', padding: 24 }}>
          macOS is not supported.<br />Virtual camera requires Apple notarization.
        </p>
      </div>
    )
  }

  // ── Login screen ──────────────────────────────────────────────────────────
  if (!auth) {
    return (
      <div style={s.center}>
        <div style={s.card}>
          <h1 style={s.title}>PeerCam</h1>
          <form onSubmit={handleLogin} style={s.form}>
            <input name="email" type="email" required placeholder="Email"
              style={s.input} autoComplete="email" />
            <input name="password" type="password" required placeholder="Password"
              style={s.input} autoComplete="current-password" />

            {/* Remember me */}
            <label style={s.rememberRow}>
              <input
                type="checkbox"
                checked={rememberMe}
                onChange={e => setRememberMe(e.target.checked)}
                style={{ accentColor: '#4f46e5', width: 14, height: 14, cursor: 'pointer' }}
              />
              <span style={{ color: '#a1a1aa', fontSize: 13 }}>Remember me</span>
            </label>

            {loginError && <p style={s.err}>{loginError}</p>}
            <button type="submit" disabled={loginLoading} style={{ ...s.btn, display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 8 }}>
              {loginLoading && <Spinner />}
              {loginLoading ? 'Signing in…' : 'Sign in'}
            </button>
          </form>
        </div>
      </div>
    )
  }

  // ── Main screen ───────────────────────────────────────────────────────────
  return (
    <div style={{ ...s.center, position: 'relative' }}>

      {/* Fullscreen exit button */}
      {isFullscreen && (
        <button onClick={exitFullscreen} style={s.fsExit} title="Exit fullscreen (Esc)">
          ✕ Exit fullscreen
        </button>
      )}

      <div style={s.card}>
        <div style={s.titleRow}>
          <h1 style={s.title}>PeerCam</h1>
          <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
            <button onClick={enterFullscreen} style={s.iconBtn} title="Fullscreen">⛶</button>
            <button onClick={handleLogout} style={s.iconBtn} title="Log out">⏻</button>
          </div>
        </div>
        <p style={s.emailLabel}>{auth.email}</p>

        {/* Role selector */}
        {!isActive && (
          <div style={s.roleRow}>
            {(['provider', 'requester'] as Role[]).map(r => (
              <button key={r} onClick={() => { setRole(r); setConnectError('') }}
                style={{ ...s.roleBtn, ...(role === r ? s.roleBtnActive : {}) }}>
                {r === 'provider' ? '📷 Share camera' : '🖥️ Receive camera'}
              </button>
            ))}
          </div>
        )}

        {/* Provider: code management */}
        {role === 'provider' && !isActive && (
          <div style={s.codeBox}>
            {codeFetching ? (
              <>
                <p style={s.codeLabel}>Your join code</p>
                <div style={s.codeSkeleton} />
                <p style={{ color: '#52525b', fontSize: 12, margin: 0 }}>Loading…</p>
              </>
            ) : codeError && !codeState.code ? (
              <>
                <p style={{ color: '#f87171', fontSize: 13, textAlign: 'center', margin: 0 }}>{codeError}</p>
                <button
                  onClick={() => auth && fetchCode(auth.accessToken)}
                  style={{ ...s.smallBtn, display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 6, marginTop: 4 }}
                >
                  ↺ Retry
                </button>
              </>
            ) : codeState.code ? (
              <>
                <p style={s.codeLabel}>Your join code</p>
                <p style={{ ...s.code, color: codeState.enabled ? '#f4f4f5' : '#52525b' }}>
                  {codeState.code.slice(0, 5)}&thinsp;{codeState.code.slice(5)}
                </p>
                <div style={s.codeActions}>
                  <button
                    onClick={() => handleToggleCode(!codeState.enabled)}
                    disabled={codeLoading}
                    style={{ ...s.smallBtn, background: codeState.enabled ? '#7f1d1d' : '#14532d', display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 6 }}
                  >
                    {codeLoading ? <Spinner size={11} /> : null}
                    {codeLoading ? '…' : codeState.enabled ? 'Disable' : 'Enable'}
                  </button>
                  <button onClick={handleGenerateCode} disabled={codeLoading}
                    style={{ ...s.smallBtn, display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 6 }}>
                    {codeLoading ? <Spinner size={11} /> : null}
                    {codeLoading ? '…' : 'Refresh'}
                  </button>
                </div>
                {codeError && <p style={s.err}>{codeError}</p>}
                {!codeState.enabled && <p style={s.warn}>⚠ Code disabled — viewers cannot connect</p>}
              </>
            ) : (
              <button onClick={handleGenerateCode} disabled={codeLoading}
                style={{ ...s.btn, display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 8 }}>
                {codeLoading && <Spinner />}
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
          <video ref={videoPreviewRef} muted playsInline
            style={{ ...s.preview, display: isActive ? 'block' : 'none' }} />
        )}

        {/* Status badge */}
        <div style={{ ...s.statusBadge, color: STATUS_COLOR[status] }}>
          {STATUS_LABEL[status]}
        </div>

        {/* Errors */}
        {error && status !== 'idle' && <p style={s.err}>{error}</p>}
        {connectError && <p style={s.err}>{connectError}</p>}
        {(error || connectError) && logPath && (
          <p style={s.logPath}>Log: {logPath}</p>
        )}

        {/* Action buttons */}
        <div style={s.btnRow}>
          {!isActive ? (
            <button onClick={handleConnect} disabled={connectDisabled} title={connectTitle}
              style={{ ...s.btn, opacity: connectDisabled ? 0.4 : 1, cursor: connectDisabled ? 'not-allowed' : 'pointer', display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 8 }}>
              {connecting && <Spinner />}
              {connecting ? 'Connecting…' : 'Connect'}
            </button>
          ) : (
            <button onClick={handleDisconnect} disabled={disconnecting}
              style={{ ...s.btn, background: '#3f3f46', display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 8 }}>
              {disconnecting && <Spinner />}
              {disconnecting ? 'Disconnecting…' : 'Disconnect'}
            </button>
          )}
        </div>

        {/* Hints */}
        {status === 'connected' && role === 'requester' && (
          <>
            {vcamOk === true && (
              <p style={s.hint}>
                {vcamObs
                  ? <>Virtual camera active — select <strong style={{ color: '#f4f4f5' }}>OBS Virtual Camera</strong> in Chrome, Zoom, Teams, or any app.</>
                  : <>Virtual camera active — select <strong style={{ color: '#f4f4f5' }}>PeerCam Virtual Camera</strong> in Zoom, Teams, or native apps.</>
                }
              </p>
            )}
            {vcamOk === false && (
              <div style={s.warnBox}>
                <p style={{ fontWeight: 600, color: '#fbbf24', marginBottom: 4 }}>⚠ Virtual camera unavailable</p>
                <p>PeerCam Virtual Camera could not start. Try reinstalling PeerCam.</p>
                <p style={{ marginTop: 6, color: '#71717a' }}>The video feed is still connected.</p>
              </div>
            )}
            {vcamOk === null && (
              <p style={s.hint}>Video connected. Starting virtual camera…</p>
            )}
          </>
        )}
        {status === 'waiting_host' && role === 'provider' && (
          <p style={s.hint}>Share your code with the viewer. They enter it on the Receive camera screen.</p>
        )}

        {/* Live log panel */}
        {logs.length > 0 && (
          <div style={s.logPanel}>
            <div style={s.logHeader}>
              <span style={{ color: '#71717a', fontSize: 11 }}>Logs</span>
              <button onClick={() => setLogs([])} style={s.logClear}>clear</button>
            </div>
            <div style={s.logBody}>
              {logs.map((l, i) => (
                <div key={i} style={{ ...s.logLine, color: l.startsWith('✕') ? '#f87171' : l.startsWith('⚠') ? '#fbbf24' : '#52525b' }}>{l}</div>
              ))}
            </div>
            {logPath && <div style={{ ...s.logPath, padding: '4px 10px', borderTop: '1px solid #18181b' }}>File: {logPath}</div>}
          </div>
        )}
      </div>
    </div>
  )
}

const s: Record<string, React.CSSProperties> = {
  center:        { flex: 1, display: 'flex', alignItems: 'center', justifyContent: 'center', padding: 24, position: 'relative' },
  card:          { width: '100%', maxWidth: 360, display: 'flex', flexDirection: 'column', gap: 14 },
  titleRow:      { display: 'flex', justifyContent: 'space-between', alignItems: 'center' },
  title:         { fontSize: 22, fontWeight: 700, letterSpacing: '-0.5px', margin: 0 },
  emailLabel:    { color: '#52525b', fontSize: 12, marginTop: -8 },
  form:          { display: 'flex', flexDirection: 'column', gap: 10 },
  rememberRow:   { display: 'flex', alignItems: 'center', gap: 8, cursor: 'pointer' },
  input:         { background: '#18181b', border: '1px solid #3f3f46', borderRadius: 8, padding: '10px 14px', color: '#f4f4f5', fontSize: 14, outline: 'none' },
  btn:           { background: '#4f46e5', color: '#fff', border: 'none', borderRadius: 8, padding: '10px 0', fontWeight: 600, fontSize: 15, cursor: 'pointer', flex: 1 },
  btnRow:        { display: 'flex', gap: 8 },
  iconBtn:       { background: 'none', border: 'none', color: '#71717a', fontSize: 16, cursor: 'pointer', padding: '2px 4px', lineHeight: 1 },
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
  logPath:       { color: '#3f3f46', fontSize: 11, textAlign: 'center', wordBreak: 'break-all', lineHeight: 1.4 },
  logPanel:      { background: '#09090b', border: '1px solid #27272a', borderRadius: 8, overflow: 'hidden' },
  logHeader:     { display: 'flex', justifyContent: 'space-between', alignItems: 'center', padding: '6px 10px', borderBottom: '1px solid #18181b' },
  logClear:      { background: 'none', border: 'none', color: '#3f3f46', fontSize: 11, cursor: 'pointer', padding: 0 },
  logBody:       { maxHeight: 150, overflowY: 'auto', padding: '6px 10px', display: 'flex', flexDirection: 'column', gap: 2 },
  logLine:       { fontSize: 10, fontFamily: 'monospace', lineHeight: 1.4, wordBreak: 'break-all' },
  warnBox:       { background: '#1c1400', border: '1px solid #78350f', borderRadius: 8, padding: '12px 14px', fontSize: 12, color: '#d4d4d8', lineHeight: 1.6 },
  codeSkeleton:  { width: 180, height: 36, borderRadius: 6, background: 'linear-gradient(90deg,#27272a 25%,#3f3f46 50%,#27272a 75%)', backgroundSize: '200% 100%', animation: 'shimmer 1.4s infinite' },
  preview:       { width: '100%', borderRadius: 8, background: '#18181b', aspectRatio: '16/9', objectFit: 'cover' },
  fsExit:        { position: 'fixed', top: 12, right: 12, zIndex: 9999, background: 'rgba(0,0,0,0.7)', color: '#f4f4f5', border: '1px solid #3f3f46', borderRadius: 6, padding: '6px 12px', fontSize: 12, cursor: 'pointer', backdropFilter: 'blur(4px)' },
}
