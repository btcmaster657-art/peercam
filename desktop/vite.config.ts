import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

export default defineConfig({
  plugins: [react()],
  root: 'src/renderer',
  base: './',
  build: {
    outDir: '../../dist/renderer',
    emptyOutDir: true,
    rollupOptions: {
      // Prevent Rollup from warning about circular deps in polyfills
      onwarn(warning, warn) {
        if (warning.code === 'CIRCULAR_DEPENDENCY') return
        warn(warning)
      },
    },
  },
  server: { port: 5173 },
  define: {
    // SimplePeer checks process.env.NODE_ENV and process.nextTick
    'process.env.NODE_ENV': JSON.stringify('production'),
    'process.nextTick': 'globalThis.queueMicrotask',
    global: 'globalThis',
  },
  resolve: {
    alias: {
      // Stub out Node built-ins SimplePeer imports
      stream: 'stream-browserify',
      events: 'eventemitter3',
      buffer: 'buffer',
    },
  },
  optimizeDeps: {
    include: ['simple-peer', 'stream-browserify', 'buffer', 'eventemitter3'],
  },
})
