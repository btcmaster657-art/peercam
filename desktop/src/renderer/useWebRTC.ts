import { useCallback, useEffect, useRef, useState } from 'react'
import SimplePeer, { Instance, SignalData } from 'simple-peer'

declare global {
  interface Window {
    peercam: {
      vcamStart:    () => Promise<{ ok: boolean; obs?: boolean; error?: string }>
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
const MAX_FRAME_WIDTH = 1920
const MAX_FRAME_HEIGHT = 1080

// ── Logger ────────────────────────────────────────────────────────────────────
function log(level: 'INFO' | 'WARN' | 'ERROR', ...parts: unknown[]) {
  const msg = parts.map(p => typeof p === 'string' ? p : JSON.stringify(p)).join(' ')
  const line = `[webrtc] ${msg}`
  if (level === 'ERROR') console.error(line)
  else if (level === 'WARN') console.warn(line)
  else console.log(line)
  window.peercam?.log(level, msg).catch(() => {})
}

function normalizeFrameSize(sourceWidth: number, sourceHeight: number) {
  const scale = Math.min(
    MAX_FRAME_WIDTH / sourceWidth,
    MAX_FRAME_HEIGHT / sourceHeight,
    1,
  )

  const width = Math.max(2, (Math.floor(sourceWidth * scale) & ~1))
  const height = Math.max(2, (Math.floor(sourceHeight * scale) & ~1))

  return { width, height }
}

export function useWebRTC() {
  const [status, setStatus] = useState<Status>('idle')
  const [error,  setError]  = useState<string | null>(null)
  const [vcamOk, setVcamOk]   = useState<boolean | null>(null)
  const [vcamObs, setVcamObs] = useState<boolean>(false)

  const wsRef          = useRef<WebSocket | null>(null)
  const peerRef        = useRef<Instance | null>(null)
  const sessionIdRef   = useRef<string | null>(null)
  const rafRef         = useRef<number>(0)
  const lastFrameRef   = useRef<number>(0)
  const frameBusyRef   = useRef<boolean>(false)
  const frameCountRef  = useRef<number>(0)
  const frameDropRef   = useRef<number>(0)
  const canvasRef      = useRef<HTMLCanvasElement | null>(null)
  const videoRef       = useRef<HTMLVideoElement | null>(null)
  const statusRef      = useRef<Status>('idle')
  const paramsRef      = useRef<ConnectParams | null>(null)
  const vcamActiveRef  = useRef<boolean>(false)
  const localStreamRef = useRef<MediaStream | null>(null)
  const connectTimeRef = useRef<number>(0)
  const pipeStartRef   = useRef<number>(0)

  function updateStatus(s: Status) {
    log('INFO', `status: ${statusRef.current} → ${s}`)
    statusRef.current = s
    setStatus(s)
  }

  // ── Frame pump (requester only) ───────────────────────────────────────────
  const stopFramePipe = useCallback(() => {
    if (rafRef.current === 0 && !vcamActiveRef.current && !videoRef.current) return
    cancelAnimationFrame(rafRef.current)
    rafRef.current = 0
    const elapsed = pipeStartRef.current ? ((Date.now() - pipeStartRef.current) / 1000).toFixed(1) : '?'
    log('INFO', `frame pipe stopping — ran=${elapsed}s frames_pushed=${frameCountRef.current} frames_dropped=${frameDropRef.current}`)
    frameCountRef.current = 0
    frameDropRef.current  = 0
    pipeStartRef.current  = 0
    if (vcamActiveRef.current) {
      vcamActiveRef.current = false
      log('INFO', 'vcam:stop — calling IPC')
      window.peercam?.vcamStop()
        .then(() => log('INFO', 'vcam:stop — done'))
        .catch(e => log('WARN', 'vcam:stop error:', e?.message ?? String(e)))
    }
    if (videoRef.current) {
      videoRef.current.srcObject = null
      videoRef.current = null
      log('INFO', 'frame pipe video element released')
    }
    log('INFO', 'frame pipe stopped')
  }, [])

  const startFramePipe = useCallback((stream: MediaStream) => {
    const tracks = stream.getVideoTracks()
    log('INFO', `frame pipe starting — video_tracks=${tracks.length}`)
    if (tracks.length > 0) {
      const t = tracks[0]
      const s = t.getSettings()
      log('INFO', `remote track — label="${t.label}" readyState=${t.readyState} enabled=${t.enabled} w=${s.width ?? '?'} h=${s.height ?? '?'} fps=${s.frameRate ?? '?'}`)
    }

    const video = document.createElement('video')
    video.srcObject = stream
    video.muted = true
    video.playsInline = true
    videoRef.current = video

    video.play()
      .then(() => log('INFO', 'frame pipe video.play() resolved'))
      .catch(e => log('WARN', 'frame pipe video.play() failed:', e?.message ?? String(e)))

    video.addEventListener('loadedmetadata', () =>
      log('INFO', `frame pipe video metadata loaded — ${video.videoWidth}×${video.videoHeight}`))
    video.addEventListener('stalled', () =>
      log('WARN', 'frame pipe video stalled'))
    video.addEventListener('ended', () =>
      log('WARN', 'frame pipe video ended'))

    const canvas = document.createElement('canvas')
    const ctx = canvas.getContext('2d', { willReadFrequently: true })!
    canvasRef.current = canvas

    setVcamOk(null)
    setVcamObs(false)
    log('INFO', 'vcam:start — calling IPC')
    window.peercam?.vcamStart()
      .then(({ ok, obs, error: err }) => {
        if (ok) {
          vcamActiveRef.current = true
          setVcamOk(true)
          setVcamObs(obs ?? false)
          log('INFO', `vcam:start — ok=true obs=${obs ?? false}`)
        } else {
          setVcamOk(false)
          log('WARN', `vcam:start — ok=false error="${err ?? 'unknown'}"`)
        }
      })
      .catch(e => {
        setVcamOk(false)
        log('ERROR', 'vcam:start — IPC threw:', e?.message ?? String(e))
      })

    pipeStartRef.current = Date.now()
    let lastW = 0, lastH = 0
    let lastStatLog = Date.now()

    const pump = () => {
      rafRef.current = requestAnimationFrame(pump)
      const v = videoRef.current
      if (!v?.videoWidth || !v.videoHeight) return
      const now = performance.now()
      if (now - lastFrameRef.current < FRAME_MS) return
      if (frameBusyRef.current) { frameDropRef.current++; return }
      lastFrameRef.current = now

      const { width, height } = normalizeFrameSize(v.videoWidth, v.videoHeight)
      if (width !== lastW || height !== lastH) {
        lastW = width
        lastH = height
        canvas.width  = lastW
        canvas.height = lastH
        if (lastW !== v.videoWidth || lastH !== v.videoHeight) {
          log('INFO', `frame pipe resolution: ${v.videoWidth}×${v.videoHeight} → ${lastW}×${lastH}`)
        } else {
          log('INFO', `frame pipe resolution: ${lastW}×${lastH}`)
        }
      }
      if (!lastW || !lastH) return

      ctx.drawImage(v, 0, 0, lastW, lastH)
      if (!vcamActiveRef.current) return

      const rgba = new Uint8Array(ctx.getImageData(0, 0, lastW, lastH).data.buffer)
      frameBusyRef.current = true
      frameCountRef.current++

      // Log frame stats every 10s
      if (Date.now() - lastStatLog >= 10_000) {
        lastStatLog = Date.now()
        log('INFO', `frame pipe stats — pushed=${frameCountRef.current} dropped=${frameDropRef.current} size=${lastW}×${lastH}`)
      }

      window.peercam?.vcamPushFrame(lastW, lastH, rgba)
        .then(() => { frameBusyRef.current = false })
        .catch(e => {
          frameBusyRef.current = false
          log('WARN', 'vcam:pushFrame error:', e?.message ?? String(e))
        })
    }
    rafRef.current = requestAnimationFrame(pump)
    log('INFO', 'frame pipe RAF pump started')
  }, [])

  // ── WebRTC peer factory ───────────────────────────────────────────────────
  function buildPeer(ws: WebSocket, sessionId: string, initiator: boolean, stream?: MediaStream): Instance {
    const sid8 = sessionId.slice(0, 8)
    log('INFO', `buildPeer — initiator=${initiator} stream=${!!stream} sessionId=${sid8}`)

    if (peerRef.current && !peerRef.current.destroyed) {
      log('WARN', 'buildPeer — destroying existing peer before rebuild')
      peerRef.current.destroy()
    }

    let peer: Instance
    try {
      peer = new SimplePeer({ initiator, trickle: true, stream })
      log('INFO', `buildPeer — SimplePeer constructed ok initiator=${initiator}`)
    } catch (e: unknown) {
      const msg = e instanceof Error ? e.message : String(e)
      log('ERROR', 'buildPeer — SimplePeer constructor threw:', msg)
      setError(`WebRTC init failed: ${msg}`)
      updateStatus('error')
      throw e
    }
    peerRef.current = peer

    peer.on('signal', (data: SignalData) => {
      const type = (data as Record<string, unknown>).type ?? 'candidate'
      log('INFO', `signal out type=${type} sessionId=${sid8} ws_state=${ws.readyState}`)
      if (ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ type: 'webrtc_signal', sessionId, signal: data }))
      } else {
        log('WARN', `signal out DROPPED — ws not open state=${ws.readyState}`)
      }
    })

    peer.on('stream', (remoteStream: MediaStream) => {
      const vtracks = remoteStream.getVideoTracks()
      log('INFO', `peer stream received — video_tracks=${vtracks.length} audio_tracks=${remoteStream.getAudioTracks().length}`)
      if (vtracks.length > 0) {
        const t = vtracks[0]
        const s = t.getSettings()
        log('INFO', `stream video track — label="${t.label}" readyState=${t.readyState} w=${s.width ?? '?'} h=${s.height ?? '?'} fps=${s.frameRate ?? '?'}`)
      }
      if (paramsRef.current?.role === 'requester') {
        updateStatus('connected')
        startFramePipe(remoteStream)
      }
    })

    peer.on('connect', () => {
      const elapsed = connectTimeRef.current ? `${Date.now() - connectTimeRef.current}ms` : '?'
      log('INFO', `peer data channel connected — time_to_connect=${elapsed}`)
      updateStatus('connected')
    })

    peer.on('error', (err: Error) => {
      log('ERROR', `peer error — message="${err.message}"`)
      if (err.message.includes('Ice connection failed') || err.message.includes('ICE')) {
        log('WARN', 'ICE failure — connection may self-heal')
        return
      }
      setError(err.message)
      updateStatus('error')
    })

    peer.on('close', () => {
      log('INFO', 'peer closed')
      stopFramePipe()
      peerRef.current = null
      if (statusRef.current === 'connected') updateStatus('idle')
    })

    // Log ICE connection state changes via the underlying RTCPeerConnection
    // simple-peer exposes it as peer._pc
    try {
      const pc = (peer as unknown as { _pc?: RTCPeerConnection })._pc
      if (pc) {
        pc.addEventListener('iceconnectionstatechange', () =>
          log('INFO', `ICE connection state: ${pc.iceConnectionState}`))
        pc.addEventListener('icegatheringstatechange', () =>
          log('INFO', `ICE gathering state: ${pc.iceGatheringState}`))
        pc.addEventListener('connectionstatechange', () =>
          log('INFO', `peer connection state: ${pc.connectionState}`))
        pc.addEventListener('signalingstatechange', () =>
          log('INFO', `signaling state: ${pc.signalingState}`))
      } else {
        log('WARN', 'buildPeer — _pc not available, skipping RTCPeerConnection state logging')
      }
    } catch { /* non-critical */ }

    return peer
  }

  // ── Main connect ──────────────────────────────────────────────────────────
  const connect = useCallback((params: ConnectParams) => {
    if (!params.relayUrl) {
      log('ERROR', 'connect — no relay URL provided')
      setError('No relay URL')
      updateStatus('error')
      return
    }

    log('INFO', `connect — role=${params.role} relay=${params.relayUrl} code=${params.joinCode} userId=${params.userId.slice(0,8)} dbSessionId=${params.dbSessionId ?? 'null'}`)
    paramsRef.current = params
    connectTimeRef.current = Date.now()

    // Tear down any existing connection before opening a new one
    if (wsRef.current) {
      log('INFO', `connect — closing existing ws (readyState=${wsRef.current.readyState}) before reconnect`)
      wsRef.current.onclose = null
      wsRef.current.onerror = null
      wsRef.current.onmessage = null
      if (wsRef.current.readyState === WebSocket.OPEN || wsRef.current.readyState === WebSocket.CONNECTING) {
        wsRef.current.close(1000, 'reconnect')
      }
      wsRef.current = null
    }
    if (peerRef.current && !peerRef.current.destroyed) {
      log('INFO', 'connect — destroying existing peer before reconnect')
      peerRef.current.destroy()
      peerRef.current = null
    }
    stopFramePipe()

    updateStatus('connecting')
    setError(null)

    log('INFO', `ws — opening connection to ${params.relayUrl}`)
    const ws = new WebSocket(params.relayUrl)
    wsRef.current = ws

    ws.onopen = () => {
      log('INFO', `ws — connected to relay (readyState=${ws.readyState})`)
      if (params.role === 'requester') {
        log('INFO', `ws — sending request_session code=${params.joinCode}`)
        ws.send(JSON.stringify({
          type:        'request_session',
          authToken:   params.authToken,
          userId:      params.userId,
          joinCode:    params.joinCode,
          dbSessionId: params.dbSessionId,
        }))
        updateStatus('waiting_peer')
      } else {
        log('INFO', `ws — sending register_provider code=${params.joinCode}`)
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
      catch { log('WARN', 'ws — failed to parse relay message:', String(ev.data).slice(0, 100)); return }

      log('INFO', `relay msg type=${msg.type as string}`)

      switch (msg.type) {

        case 'connected':
          log('INFO', `relay assigned peerId=${msg.peerId}`)
          break

        case 'session_created':
          log('INFO', `session_created sessionId=${(msg.sessionId as string).slice(0,8)} — waiting for provider ready`)
          sessionIdRef.current = msg.sessionId as string
          break

        case 'agent_session_ready': {
          const sid = msg.sessionId as string
          if (peerRef.current && !peerRef.current.destroyed) {
            log('WARN', `agent_session_ready — duplicate for sessionId=${sid.slice(0,8)}, peer already exists, ignoring`)
            break
          }
          sessionIdRef.current = sid
          log('INFO', `agent_session_ready sessionId=${sid.slice(0,8)} — building initiator peer`)
          try { buildPeer(ws, sid, true) }
          catch { /* already logged */ }
          break
        }

        case 'registered':
          log('INFO', 'provider registered in relay pool')
          break

        case 'session_request': {
          const sid = (msg.sessionId as string)
          log('INFO', `session_request sessionId=${sid.slice(0,8)} — acquiring camera`)
          try {
            if (localStreamRef.current) {
              const old = localStreamRef.current.getVideoTracks()
              log('INFO', `session_request — stopping previous stream tracks=${old.length}`)
              old.forEach(t => { t.stop(); log('INFO', `track stopped label="${t.label}"`) })
              localStreamRef.current.getTracks().forEach(t => t.stop())
              localStreamRef.current = null
            }
            log('INFO', 'getUserMedia — requesting video')
            localStreamRef.current = await navigator.mediaDevices.getUserMedia({ video: true, audio: false })
            const vtracks = localStreamRef.current.getVideoTracks()
            log('INFO', `getUserMedia — ok tracks=${vtracks.length}`)
            if (vtracks.length > 0) {
              const t = vtracks[0]
              const s = t.getSettings()
              log('INFO', `camera track — label="${t.label}" w=${s.width ?? '?'} h=${s.height ?? '?'} fps=${s.frameRate ?? '?'} deviceId=${s.deviceId?.slice(0,8) ?? '?'}`)
            }
          } catch (e: unknown) {
            const msg2 = e instanceof Error ? e.message : String(e)
            log('ERROR', `getUserMedia — failed: ${msg2}`)
            ws.send(JSON.stringify({ type: 'end_session' }))
            setError('Camera access denied')
            updateStatus('error')
            return
          }
          log('INFO', `session_request — sending agent_ready sessionId=${sid.slice(0,8)}`)
          ws.send(JSON.stringify({ type: 'agent_ready', sessionId: msg.sessionId }))
          try { buildPeer(ws, sid, false, localStreamRef.current) }
          catch { /* already logged */ }
          break
        }

        case 'webrtc_signal': {
          const sigType = ((msg.signal as Record<string, unknown>)?.type ?? 'candidate') as string
          const sid8 = (msg.sessionId as string).slice(0, 8)
          log('INFO', `signal in type=${sigType} sessionId=${sid8} peer_exists=${!!peerRef.current} peer_destroyed=${peerRef.current?.destroyed ?? 'null'}`)
          const peer = peerRef.current
          if (peer && !peer.destroyed) {
            try { peer.signal(msg.signal as SignalData) }
            catch (e: unknown) {
              log('ERROR', `peer.signal() threw type=${sigType}:`, e instanceof Error ? e.message : String(e))
            }
          } else {
            log('WARN', `signal in DROPPED type=${sigType} — peer not ready`)
          }
          break
        }

        case 'reconnecting':
          log('WARN', `relay reconnecting attempt=${msg.attempt}/${msg.maxAttempts}`)
          stopFramePipe()
          if (peerRef.current && !peerRef.current.destroyed) {
            log('INFO', 'reconnecting — destroying peer')
            peerRef.current.destroy()
          }
          peerRef.current = null
          sessionIdRef.current = null
          updateStatus('reconnecting')
          setError(`Reconnecting… (attempt ${msg.attempt}/${msg.maxAttempts})`)
          break

        case 'session_ended': {
          log('INFO', `session_ended reason=${msg.reason}`)
          stopFramePipe()
          const endedPeer = peerRef.current
          peerRef.current = null
          sessionIdRef.current = null
          if (endedPeer && !endedPeer.destroyed) {
            log('INFO', 'session_ended — destroying peer')
            endedPeer.destroy()
          }
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
      log(ev.wasClean ? 'INFO' : 'WARN',
        `ws closed — code=${ev.code} clean=${ev.wasClean} reason="${ev.reason || 'none'}" status_at_close=${statusRef.current}`)
      stopFramePipe()
      const closedPeer = peerRef.current
      peerRef.current = null
      if (closedPeer && !closedPeer.destroyed) {
        log('INFO', 'ws closed — destroying peer')
        closedPeer.destroy()
      }
      if (statusRef.current !== 'error' && statusRef.current !== 'idle') {
        if (ev.wasClean) updateStatus('idle')
        else {
          setError('Connection lost — check your network')
          updateStatus('error')
        }
      }
    }

    ws.onerror = () => {
      // onerror gives no useful detail in browsers; the close event that follows has the code
      log('ERROR', `ws error event fired (readyState=${ws.readyState})`)
    }
  }, [startFramePipe, stopFramePipe]) // eslint-disable-line react-hooks/exhaustive-deps

  const disconnect = useCallback(() => {
    log('INFO', `disconnect called — current status=${statusRef.current}`)
    stopFramePipe()
    sessionIdRef.current = null
    if (localStreamRef.current) {
      const tracks = localStreamRef.current.getTracks()
      log('INFO', `disconnect — stopping camera stream tracks=${tracks.length}`)
      tracks.forEach(t => { t.stop(); log('INFO', `track stopped label="${t.label}"`) })
      localStreamRef.current = null
    }
    if (peerRef.current && !peerRef.current.destroyed) {
      log('INFO', 'disconnect — destroying peer')
      peerRef.current.destroy()
    }
    peerRef.current = null
    if (wsRef.current) {
      log('INFO', `disconnect — closing ws (readyState=${wsRef.current.readyState})`)
      wsRef.current.onclose = null
      wsRef.current.close(1000, 'user_disconnect')
      wsRef.current = null
    }
    paramsRef.current = null
    updateStatus('idle')
    setError(null)
    log('INFO', 'disconnect complete')
  }, [stopFramePipe])

  useEffect(() => () => {
    log('INFO', 'useWebRTC unmounting — calling disconnect')
    disconnect()
  }, [disconnect])

  return { status, error, vcamOk, vcamObs, connect, disconnect, localStream: localStreamRef }
}
