import { useCallback, useEffect, useRef, useState } from 'react'
import SimplePeer, { Instance, SignalData } from 'simple-peer'

declare global {
  interface Window {
    peercam: {
      vcamStart: () => Promise<{ ok: boolean; error?: string }>
      vcamStop:  () => Promise<void>
      vcamPushFrame: (w: number, h: number, rgba: Uint8Array) => Promise<void>
      platform: string
    }
  }
}

export type Role = 'requester' | 'provider'
export type Status = 'idle' | 'connecting' | 'waiting_peer' | 'waiting_host' | 'reconnecting' | 'connected' | 'error'

export interface ConnectParams {
  relayUrl:    string
  authToken:   string
  userId:      string
  joinCode:    string
  role:        Role
  dbSessionId: string | null
}

const FRAME_MS = 1000 / 30  // 30 fps hard cap

export function useWebRTC() {
  const [status, setStatus] = useState<Status>('idle')
  const [error,  setError]  = useState<string | null>(null)

  // All mutable state lives in refs so WS/peer callbacks never capture stale closures
  const wsRef        = useRef<WebSocket | null>(null)
  const peerRef      = useRef<SimplePeer.Instance | null>(null)
  const rafRef       = useRef<number>(0)
  const lastFrameRef = useRef<number>(0)
  const frameBusyRef = useRef<boolean>(false)   // backpressure: skip frame if IPC still pending
  const canvasRef    = useRef<HTMLCanvasElement | null>(null)
  const ctxRef       = useRef<CanvasRenderingContext2D | null>(null)
  const videoRef     = useRef<HTMLVideoElement | null>(null)
  const statusRef    = useRef<Status>('idle')
  const paramsRef    = useRef<ConnectParams | null>(null)
  const vcamActiveRef   = useRef<boolean>(false)
  const localStreamRef  = useRef<MediaStream | null>(null)  // provider: reuse across sessions

  function updateStatus(s: Status) {
    statusRef.current = s
    setStatus(s)
  }

  // ── Frame pump (requester only) ───────────────────────────────────────────
  const stopFramePipe = useCallback(() => {
    cancelAnimationFrame(rafRef.current)
    rafRef.current = 0
    if (vcamActiveRef.current) {
      vcamActiveRef.current = false
      window.peercam?.vcamStop().catch(() => {/* IPC may be gone on app close */})
    }
    if (videoRef.current) {
      videoRef.current.srcObject = null
      videoRef.current = null
    }
  }, [])

  const startFramePipe = useCallback((stream: MediaStream) => {
    const video = document.createElement('video')
    video.srcObject = stream
    video.muted = true
    video.playsInline = true
    video.play().catch(() => {})
    videoRef.current = video

    const canvas = document.createElement('canvas')
    const ctx = canvas.getContext('2d', { willReadFrequently: true })!
    canvasRef.current = canvas
    ctxRef.current = ctx

    window.peercam?.vcamStart().then(({ ok, error: err }) => {
      if (ok) { vcamActiveRef.current = true }
      else    { console.warn('[vcam] start failed:', err) }
    }).catch(() => {})

    let lastW = 0, lastH = 0

    const pump = () => {
      rafRef.current = requestAnimationFrame(pump)
      const video = videoRef.current
      if (!video?.videoWidth) return

      const now = performance.now()
      if (now - lastFrameRef.current < FRAME_MS) return
      if (frameBusyRef.current) return  // backpressure: previous frame still in flight

      lastFrameRef.current = now

      if (video.videoWidth !== lastW || video.videoHeight !== lastH) {
        canvas.width  = video.videoWidth
        canvas.height = video.videoHeight
        lastW = canvas.width
        lastH = canvas.height
      }

      ctx.drawImage(video, 0, 0)
      const imageData = ctx.getImageData(0, 0, lastW, lastH)
      const rgba = new Uint8Array(imageData.data.buffer)

      frameBusyRef.current = true
      window.peercam?.vcamPushFrame(lastW, lastH, rgba)
        .then(() => { frameBusyRef.current = false })
        .catch(() => { frameBusyRef.current = false })
    }

    rafRef.current = requestAnimationFrame(pump)
  }, [])

  // ── WebRTC peer factory ───────────────────────────────────────────────────
  function buildPeer(ws: WebSocket, sessionId: string, initiator: boolean, stream?: MediaStream): Instance {
    // Destroy any existing peer cleanly first
    if (peerRef.current && !peerRef.current.destroyed) {
      peerRef.current.destroy()
    }

    const peer = new SimplePeer({ initiator, trickle: true, stream })
    peerRef.current = peer

    peer.on('signal', (data: SignalData) => {
      if (ws.readyState === WebSocket.OPEN)
        ws.send(JSON.stringify({ type: 'webrtc_signal', sessionId, signal: data }))
    })

    peer.on('stream', (remoteStream: MediaStream) => {
      if (paramsRef.current?.role === 'requester') {
        updateStatus('connected')
        startFramePipe(remoteStream)
      }
    })

    peer.on('connect', () => updateStatus('connected'))

    peer.on('error', (err: Error) => {
      // ICE failures are recoverable — don't surface STUN/TURN noise as fatal
      if (err.message.includes('Ice connection failed') || err.message.includes('ICE')) {
        console.warn('[peer] ICE error (may self-heal):', err.message)
        return
      }
      setError(err.message)
      updateStatus('error')
    })

    peer.on('close', () => {
      stopFramePipe()
      if (statusRef.current === 'connected') updateStatus('idle')
    })

    return peer
  }

  // ── Main connect — takes params directly, no closure capture ─────────────
  const connect = useCallback((params: ConnectParams) => {
    if (!params.relayUrl) { setError('No relay URL'); updateStatus('error'); return }

    paramsRef.current = params
    updateStatus('connecting')
    setError(null)

    const ws = new WebSocket(params.relayUrl)
    wsRef.current = ws

    ws.onopen = () => {
      if (params.role === 'requester') {
        ws.send(JSON.stringify({
          type:        'request_session',
          authToken:   params.authToken,
          userId:      params.userId,
          joinCode:    params.joinCode,
          dbSessionId: params.dbSessionId,
        }))
        updateStatus('waiting_peer')
      } else {
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
      catch { return }

      switch (msg.type) {

        case 'session_created':
        case 'agent_session_ready': {
          buildPeer(ws, msg.sessionId as string, true)
          break
        }

        case 'registered':
          // Provider confirmed in relay pool — status already 'waiting_host'
          break

        case 'session_request': {
          // Reuse existing stream if we already have camera access
          if (!localStreamRef.current) {
            try {
              localStreamRef.current = await navigator.mediaDevices.getUserMedia({ video: true, audio: false })
            } catch {
              ws.send(JSON.stringify({ type: 'end_session' }))
              setError('Camera access denied')
              updateStatus('error')
              return
            }
          }
          ws.send(JSON.stringify({ type: 'agent_ready', sessionId: msg.sessionId }))
          buildPeer(ws, msg.sessionId as string, false, localStreamRef.current)
          break
        }

        case 'webrtc_signal': {
          const peer = peerRef.current
          if (peer && !peer.destroyed) {
            try { peer.signal(msg.signal as SignalData) }
            catch { /* ignore signals after peer destroyed */ }
          }
          break
        }

        case 'reconnecting': {
          // Relay is trying to find a new provider — show reconnecting state
          stopFramePipe()
          if (peerRef.current && !peerRef.current.destroyed) peerRef.current.destroy()
          peerRef.current = null
          updateStatus('reconnecting')
          setError(`Reconnecting… (attempt ${msg.attempt}/${msg.maxAttempts})`)
          break
        }

        case 'session_ended': {
          stopFramePipe()
          if (peerRef.current && !peerRef.current.destroyed) peerRef.current.destroy()
          peerRef.current = null
          const reason = msg.reason as string
          if (reason === 'no_provider_available') {
            setError('Provider disconnected and could not reconnect')
            updateStatus('error')
          } else {
            updateStatus('idle')
          }
          break
        }

        case 'error': {
          setError(msg.message as string)
          updateStatus('error')
          break
        }
      }
    }

    ws.onclose = (ev) => {
      stopFramePipe()
      if (peerRef.current && !peerRef.current.destroyed) peerRef.current.destroy()
      peerRef.current = null
      // Don't overwrite a meaningful error state
      if (statusRef.current !== 'error' && statusRef.current !== 'idle') {
        if (ev.wasClean) {
          updateStatus('idle')
        } else {
          setError('Connection lost — check your network')
          updateStatus('error')
        }
      }
    }

    ws.onerror = () => {
      // onclose fires right after onerror, handle state there
    }
  }, [startFramePipe, stopFramePipe]) // eslint-disable-line react-hooks/exhaustive-deps

  const disconnect = useCallback(() => {
    stopFramePipe()
    // Stop provider camera stream
    if (localStreamRef.current) {
      localStreamRef.current.getTracks().forEach(t => t.stop())
      localStreamRef.current = null
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
