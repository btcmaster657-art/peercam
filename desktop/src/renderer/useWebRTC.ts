import { useCallback, useEffect, useRef, useState } from 'react'
import SimplePeer, { Instance, SignalData } from 'simple-peer'

declare global {
  interface Window {
    peercam: {
      vcamStart:    () => Promise<{ ok: boolean; error?: string }>
      vcamStop:     () => Promise<void>
      vcamPushFrame:(w: number, h: number, rgba: Uint8Array) => Promise<void>
      platform:     string
      log:          (level: string, message: string) => Promise<void>
      getLogPath:   () => Promise<string>
    }
  }
}

export type Role   = 'requester' | 'provider'
export type Status = 'idle' | 'connecting' | 'waiting_peer' | 'waiting_host' | 'reconnecting' | 'connected' | 'error'

export interface ConnectParams {
  relayUrl:    string
  authToken:   string
  userId:      string
  joinCode:    string
  role:        Role
  dbSessionId: string | null
}

const FRAME_MS = 1000 / 30

// ── Logger — writes to file via IPC and console ───────────────────────────────
function log(level: 'INFO' | 'WARN' | 'ERROR', ...parts: unknown[]) {
  const msg = parts.map(p => typeof p === 'string' ? p : JSON.stringify(p)).join(' ')
  const line = `[webrtc] ${msg}`
  if (level === 'ERROR') console.error(line)
  else if (level === 'WARN') console.warn(line)
  else console.log(line)
  window.peercam?.log(level, msg).catch(() => {})
}

export function useWebRTC() {
  const [status, setStatus] = useState<Status>('idle')
  const [error,  setError]  = useState<string | null>(null)

  const wsRef          = useRef<WebSocket | null>(null)
  const peerRef        = useRef<Instance | null>(null)
  const sessionIdRef   = useRef<string | null>(null)   // track active session to ignore duplicate triggers
  const rafRef         = useRef<number>(0)
  const lastFrameRef   = useRef<number>(0)
  const frameBusyRef   = useRef<boolean>(false)
  const canvasRef      = useRef<HTMLCanvasElement | null>(null)
  const videoRef       = useRef<HTMLVideoElement | null>(null)
  const statusRef      = useRef<Status>('idle')
  const paramsRef      = useRef<ConnectParams | null>(null)
  const vcamActiveRef  = useRef<boolean>(false)
  const localStreamRef = useRef<MediaStream | null>(null)

  function updateStatus(s: Status) {
    log('INFO', `status: ${statusRef.current} → ${s}`)
    statusRef.current = s
    setStatus(s)
  }

  // ── Frame pump (requester only) ───────────────────────────────────────────
  const stopFramePipe = useCallback(() => {
    cancelAnimationFrame(rafRef.current)
    rafRef.current = 0
    if (vcamActiveRef.current) {
      vcamActiveRef.current = false
      window.peercam?.vcamStop().catch(() => {})
    }
    if (videoRef.current) {
      videoRef.current.srcObject = null
      videoRef.current = null
    }
    log('INFO', 'frame pipe stopped')
  }, [])

  const startFramePipe = useCallback((stream: MediaStream) => {
    log('INFO', 'starting frame pipe', `tracks=${stream.getVideoTracks().length}`)
    const video = document.createElement('video')
    video.srcObject = stream
    video.muted = true
    video.playsInline = true
    video.play().catch(e => log('WARN', 'video.play() failed:', e.message))
    videoRef.current = video

    const canvas = document.createElement('canvas')
    const ctx = canvas.getContext('2d', { willReadFrequently: true })!
    canvasRef.current = canvas

    window.peercam?.vcamStart().then(({ ok, error: err }) => {
      if (ok) { vcamActiveRef.current = true; log('INFO', 'vcam started') }
      else    { log('WARN', 'vcam start failed:', err) }
    }).catch(e => log('WARN', 'vcam start error:', e.message))

    let lastW = 0, lastH = 0

    const pump = () => {
      rafRef.current = requestAnimationFrame(pump)
      const v = videoRef.current
      if (!v?.videoWidth) return
      const now = performance.now()
      if (now - lastFrameRef.current < FRAME_MS) return
      if (frameBusyRef.current) return
      lastFrameRef.current = now
      if (v.videoWidth !== lastW || v.videoHeight !== lastH) {
        canvas.width  = v.videoWidth
        canvas.height = v.videoHeight
        lastW = canvas.width
        lastH = canvas.height
        log('INFO', `frame size: ${lastW}×${lastH}`)
      }
      ctx.drawImage(v, 0, 0)
      const rgba = new Uint8Array(ctx.getImageData(0, 0, lastW, lastH).data.buffer)
      frameBusyRef.current = true
      window.peercam?.vcamPushFrame(lastW, lastH, rgba)
        .then(() => { frameBusyRef.current = false })
        .catch(() => { frameBusyRef.current = false })
    }
    rafRef.current = requestAnimationFrame(pump)
  }, [])

  // ── WebRTC peer factory ───────────────────────────────────────────────────
  function buildPeer(ws: WebSocket, sessionId: string, initiator: boolean, stream?: MediaStream): Instance {
    log('INFO', `building peer initiator=${initiator} stream=${!!stream} sessionId=${sessionId.slice(0,8)}`)

    if (peerRef.current && !peerRef.current.destroyed) {
      log('INFO', 'destroying previous peer')
      peerRef.current.destroy()
    }

    let peer: Instance
    try {
      peer = new SimplePeer({ initiator, trickle: true, stream })
    } catch (e: unknown) {
      const msg = e instanceof Error ? e.message : String(e)
      log('ERROR', 'SimplePeer constructor failed:', msg)
      setError(`WebRTC init failed: ${msg}`)
      updateStatus('error')
      throw e
    }
    peerRef.current = peer

    peer.on('signal', (data: SignalData) => {
      const type = (data as Record<string, unknown>).type ?? 'candidate'
      log('INFO', `signal out type=${type} sessionId=${sessionId.slice(0,8)}`)
      if (ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ type: 'webrtc_signal', sessionId, signal: data }))
      } else {
        log('WARN', `signal dropped — ws not open (state=${ws.readyState})`)
      }
    })

    peer.on('stream', (remoteStream: MediaStream) => {
      log('INFO', `remote stream received tracks=${remoteStream.getVideoTracks().length}`)
      if (paramsRef.current?.role === 'requester') {
        updateStatus('connected')
        startFramePipe(remoteStream)
      }
    })

    peer.on('connect', () => {
      log('INFO', 'peer data channel connected')
      updateStatus('connected')
    })

    peer.on('error', (err: Error) => {
      log('ERROR', 'peer error:', err.message)
      if (err.message.includes('Ice connection failed') || err.message.includes('ICE')) {
        log('WARN', 'ICE failure — may self-heal')
        return
      }
      setError(err.message)
      updateStatus('error')
    })

    peer.on('close', () => {
      log('INFO', 'peer closed')
      stopFramePipe()
      if (statusRef.current === 'connected') updateStatus('idle')
    })

    return peer
  }

  // ── Main connect ──────────────────────────────────────────────────────────
  const connect = useCallback((params: ConnectParams) => {
    if (!params.relayUrl) {
      log('ERROR', 'no relay URL')
      setError('No relay URL')
      updateStatus('error')
      return
    }

    log('INFO', `connecting role=${params.role} relay=${params.relayUrl} code=${params.joinCode}`)
    paramsRef.current = params
    updateStatus('connecting')
    setError(null)

    const ws = new WebSocket(params.relayUrl)
    wsRef.current = ws

    ws.onopen = () => {
      log('INFO', 'ws connected to relay')
      if (params.role === 'requester') {
        log('INFO', `sending request_session code=${params.joinCode}`)
        ws.send(JSON.stringify({
          type:        'request_session',
          authToken:   params.authToken,
          userId:      params.userId,
          joinCode:    params.joinCode,
          dbSessionId: params.dbSessionId,
        }))
        updateStatus('waiting_peer')
      } else {
        log('INFO', `sending register_provider code=${params.joinCode}`)
        ws.send(JSON.stringify({
          type:      'register_provider',
          authToken: params.authToken,
          userId:    params.userId,
          joinCode:  params.joinCode,
        }))
        updateStatus('waiting_host')
      }
    }

    ws.onmessage = async (ev) => {
      let msg: Record<string, unknown>
      try { msg = JSON.parse(ev.data as string) }
      catch { log('WARN', 'failed to parse relay message'); return }

      log('INFO', `relay msg type=${msg.type as string}`)

      switch (msg.type) {

        case 'connected':
          log('INFO', `relay assigned peerId=${msg.peerId}`)
          break

        case 'session_created':
          // Session matched — wait for agent_session_ready before starting WebRTC
          // (provider may not have camera ready yet)
          log('INFO', `session_created sessionId=${(msg.sessionId as string).slice(0,8)} — waiting for provider ready`)
          sessionIdRef.current = msg.sessionId as string
          break

        case 'agent_session_ready': {
          const sid = msg.sessionId as string
          // Guard: only build the peer once per session
          if (peerRef.current && !peerRef.current.destroyed) {
            log('INFO', `duplicate agent_session_ready for sessionId=${sid.slice(0,8)} — ignoring`)
            break
          }
          sessionIdRef.current = sid
          log('INFO', `agent ready sessionId=${sid.slice(0,8)} — creating initiator peer`)
          try { buildPeer(ws, sid, true) }
          catch { /* already logged in buildPeer */ }
          break
        }

        case 'registered':
          log('INFO', 'provider registered in relay pool')
          break

        case 'session_request': {
          log('INFO', `session_request received sessionId=${(msg.sessionId as string).slice(0,8)} — getting camera`)
          // Provider: get camera stream
          if (!localStreamRef.current) {
            try {
              localStreamRef.current = await navigator.mediaDevices.getUserMedia({ video: true, audio: false })
              log('INFO', `camera acquired tracks=${localStreamRef.current.getVideoTracks().length}`)
            } catch (e: unknown) {
              const msg2 = e instanceof Error ? e.message : String(e)
              log('ERROR', 'getUserMedia failed:', msg2)
              ws.send(JSON.stringify({ type: 'end_session' }))
              setError('Camera access denied')
              updateStatus('error')
              return
            }
          } else {
            log('INFO', 'reusing existing camera stream')
          }
          log('INFO', 'sending agent_ready')
          ws.send(JSON.stringify({ type: 'agent_ready', sessionId: msg.sessionId }))
          try { buildPeer(ws, msg.sessionId as string, false, localStreamRef.current) }
          catch { /* already logged */ }
          break
        }

        case 'webrtc_signal': {
          const sigType = ((msg.signal as Record<string, unknown>)?.type ?? 'candidate') as string
          log('INFO', `signal in type=${sigType} sessionId=${(msg.sessionId as string).slice(0,8)}`)
          const peer = peerRef.current
          if (peer && !peer.destroyed) {
            try { peer.signal(msg.signal as SignalData) }
            catch (e: unknown) {
              log('ERROR', 'peer.signal() threw:', e instanceof Error ? e.message : String(e))
            }
          } else {
            log('WARN', `signal dropped — peer not ready (destroyed=${peer?.destroyed ?? 'null'})`)
          }
          break
        }

        case 'reconnecting':
          log('WARN', `relay reconnecting attempt=${msg.attempt}/${msg.maxAttempts}`)
          stopFramePipe()
          if (peerRef.current && !peerRef.current.destroyed) peerRef.current.destroy()
          peerRef.current = null
          sessionIdRef.current = null
          updateStatus('reconnecting')
          setError(`Reconnecting… (attempt ${msg.attempt}/${msg.maxAttempts})`)
          break

        case 'session_ended': {
          log('INFO', `session ended reason=${msg.reason}`)
          stopFramePipe()
          if (peerRef.current && !peerRef.current.destroyed) peerRef.current.destroy()
          peerRef.current = null
          sessionIdRef.current = null
          if (msg.reason === 'no_provider_available') {
            setError('Provider disconnected and could not reconnect')
            updateStatus('error')
          } else {
            updateStatus('idle')
          }
          break
        }

        case 'error':
          log('ERROR', `relay error: ${msg.message}`)
          setError(msg.message as string)
          updateStatus('error')
          break

        default:
          log('WARN', `unknown relay message type=${msg.type}`)
      }
    }

    ws.onclose = (ev) => {
      log('INFO', `ws closed code=${ev.code} clean=${ev.wasClean} reason=${ev.reason || 'none'}`)
      stopFramePipe()
      if (peerRef.current && !peerRef.current.destroyed) peerRef.current.destroy()
      peerRef.current = null
      if (statusRef.current !== 'error' && statusRef.current !== 'idle') {
        if (ev.wasClean) updateStatus('idle')
        else {
          setError('Connection lost — check your network')
          updateStatus('error')
        }
      }
    }

    ws.onerror = (ev) => {
      log('ERROR', 'ws error', JSON.stringify(ev))
    }
  }, [startFramePipe, stopFramePipe]) // eslint-disable-line react-hooks/exhaustive-deps

  const disconnect = useCallback(() => {
    log('INFO', 'disconnect called')
    stopFramePipe()
    sessionIdRef.current = null
    if (localStreamRef.current) {
      localStreamRef.current.getTracks().forEach(t => t.stop())
      localStreamRef.current = null
      log('INFO', 'camera stream stopped')
    }
    if (peerRef.current && !peerRef.current.destroyed) peerRef.current.destroy()
    peerRef.current = null
    if (wsRef.current) {
      wsRef.current.onclose = null
      wsRef.current.close(1000, 'user_disconnect')
      wsRef.current = null
    }
    paramsRef.current = null
    updateStatus('idle')
    setError(null)
  }, [stopFramePipe])

  useEffect(() => () => { disconnect() }, [disconnect])

  return { status, error, connect, disconnect }
}
